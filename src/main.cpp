#include "behavior.h"
#include "model.h"
#include "models.h"
#include "renderer.h"
#include "settings.h"
#include "singleton.h"
#include "splash.h"
#include "tray.h"
#include "window.h"

#ifdef HAVE_LIVE2D_SDK
#include "model_live2d.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

static void DumpError(const char* what, const char* msg) {
  // 日志写 exe 目录(而非工作目录,启动方式无关)
  wchar_t buf[MAX_PATH];
  std::string log;
  if (GetModuleFileNameW(nullptr, buf, MAX_PATH)) {
    std::wstring w(buf);
    const size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
      char mb[MAX_PATH * 3];
      const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(pos + 1), mb,
                                        sizeof mb, nullptr, nullptr);
      log.assign(mb, n);
    }
  }
  log += "startup_error.log";
  FILE* f = fopen(log.c_str(), "a");
  if (f) {
    fprintf(f, "%s: %s\n", what, msg);
    fclose(f);
  }
  MessageBoxA(nullptr, msg, "Desktopet 启动失败", MB_ICONERROR);
}

// 桌宠主程序:窗口沿屏幕边缘趴着,透明置顶,鼠标穿透。
// 点击宠物触发 Happy;按 Esc 退出(调试阶段保留)。

namespace {

constexpr int kBaseSize = 400;       // 1x 时窗口边长(逻辑像素)
constexpr float kMinZoom = 0.5f;
constexpr float kMaxZoom = 2.0f;
constexpr float kZoomStep = 0.1f;    // 每 120 滚轮增量缩放一档

struct App {
  Window* win;
  Renderer* r;
  Model* model;
  Behavior* b;
  TrayIcon* tray;           // 切换成功后回写选中项,供托盘菜单打勾
  std::string current;      // 当前模型目录名
  std::mt19937* rng;
  std::uniform_real_distribution<float>* r01;
  int bottom_y;          // 工作区底边(屏幕坐标)
  int top_y = 0;         // 工作区顶边(屏幕坐标)
  int work_left = 0;     // 工作区左/右边界(缩放后重算行为边界用)
  int work_w = 1920;
  bool dragging = false;     // 用户正在拖动窗口
  bool pinned_bottom = true; // 拖拽前强制贴底;拖动后置 false,停在用户放置处
  float zoom = 1.0f;
  int wheel_accum = 0;       // 滚轮增量累加(高分辨率滚轮以 120 为满档)
  bool first_frame = true;
  // 鼠标追随的平滑目标(避免眼球瞬移)。拖拽时清零以免拉视角。
  float look_smooth_x = 0.f;
  float look_smooth_y = 0.f;
};

void Frame(float dt, void* user) {
  auto* a = static_cast<App*>(user);

  // 首帧:DT 会因窗口创建耗时而异常大,清零行为时钟,避免首帧跳变
  if (a->first_frame) {
    a->first_frame = false;
    a->b->ResetClock();
  }

  // 拖动中:位置由 Window 的拖拽逻辑接管,暂停行为状态机(避免自动爬行抢位置)
  if (!a->dragging) {
    a->b->MaybeSwitch((*a->r01)(*a->rng));
    a->b->Update(dt);
    const int y = a->pinned_bottom ? (a->bottom_y - a->win->Height())
                                   : static_cast<int>(a->b->y);
    a->win->Move(static_cast<int>(a->b->x), y);
  }

  // 鼠标追随:光标相对宠物脸中心的归一化偏移(±1),做指数平滑后写入行为层。
  if (a->b->follow_mouse) {
    POINT c{};
    GetCursorPos(&c);
    c = Window::VirtualizeCursor(c);  // 物理像素→逻辑,与 GetWindowRect 对齐
    int wx = 0, wy = 0;
    a->win->GetPos(&wx, &wy);
    // 脸中心在窗口横向中心、约 40% 高处(宠物锚点在底部)。
    const float cx = static_cast<float>(wx) + a->win->Width() * 0.5f;
    const float cy = static_cast<float>(wy) + a->win->Height() * 0.4f;
    const float range = static_cast<float>(a->win->Width()) * 0.5f;
    // clamp 到 ±1:光标超出半个窗口宽后眼球停在极限角。
    const float tx = std::clamp((static_cast<float>(c.x) - cx) / range, -1.f, 1.f);
    const float ty = std::clamp((static_cast<float>(cy) - c.y) / range, -1.f, 1.f);
    const float k = 1.f - std::exp(-dt * 12.f);  // 平滑系数(约 80ms 收敛)
    a->look_smooth_x += (tx - a->look_smooth_x) * k;
    a->look_smooth_y += (ty - a->look_smooth_y) * k;
    a->b->look_x = a->look_smooth_x;
    a->b->look_y = a->look_smooth_y;
  } else {
    a->look_smooth_x = 0.f;
    a->look_smooth_y = 0.f;
    a->b->look_x = 0.f;
    a->b->look_y = 0.f;
  }

  // 模型动画参数 → 渲染
  a->model->Update(dt);
  a->r->Begin();
  a->model->Draw();
  a->r->End();
}

void Mouse(bool down, int, int, void* user) {
  auto* a = static_cast<App*>(user);
  if (down) a->b->OnClick();
}

// 拖拽回调:窗口位置已由 Window 内部 Move,这里只同步行为层坐标。
// Begin → Move* → End;End 后脱离贴底,宠物停在用户放置处继续原有行为。
void Drag(int x, int y, DragPhase phase, void* user) {
  auto* a = static_cast<App*>(user);
  switch (phase) {
    case DragPhase::Begin:
      a->dragging = true;
      a->b->state = Behavior::State::Idle;  // 打断当前爬行
      break;
    case DragPhase::Move:
      a->b->x = static_cast<float>(x);
      a->b->y = static_cast<float>(y);
      break;
    case DragPhase::End: {
      a->dragging = false;
      a->pinned_bottom = false;
      // 钳制到工作区内,避免拖出可视范围
      const int h = a->win->Height();
      a->b->x = static_cast<float>(x);
      a->b->y = static_cast<float>(std::clamp(y, a->top_y, a->bottom_y - h));
      a->b->state = Behavior::State::Idle;
      break;
    }
  }
}

void Key(int vk, bool down, void* user) {
  auto* a = static_cast<App*>(user);
  if (down && vk == VK_ESCAPE) a->win->RequestClose();
}

// 切换模型:销毁重建 Live2DModel 并重载对应 assets/<name>。仅在真实 SDK 构建可用。
#ifdef HAVE_LIVE2D_SDK
static void SwitchModel(App* a, const std::string& name) {
  auto* l2d = dynamic_cast<Live2DModel*>(a->model);
  if (!l2d) return;

  // Load 内部会销毁旧 Impl 并重建(释放旧 moc/渲染器/贴图/运动等),再用保存的
  // 设备与当前画布尺寸重新注入。之后重新绑定行为状态机。
  l2d->Load(ModelDir(name));
  l2d->SetBehavior(a->b);

  a->current = name;
  Settings::SetModelName(name);  // 记住选择,下次启动直接加载
  if (a->tray) a->tray->SetCurrentModel(name);
}

// 模型切换消息(WPARAM = ListModels() 下标),由托盘菜单投递。
void ModelSwitch(WPARAM wp, void* user) {
  auto* a = static_cast<App*>(user);
  const std::vector<std::string> models = ListModels();
  const size_t idx = static_cast<size_t>(wp);
  if (idx >= models.size()) return;
  SwitchModel(a, models[idx]);
}
#else
void ModelSwitch(WPARAM, void*) {}  // Stub 构建:无真实模型可切,忽略
#endif

// 应用缩放:重算尺寸、保持窗口底边锚点不动、重建渲染目标与模型画布。
static void ApplyZoom(App* a) {
  const int size = static_cast<int>(kBaseSize * a->zoom + 0.5f);
  int x = 0, y = 0;
  a->win->GetPos(&x, &y);
  const int old_bottom = y + a->win->Height();
  const int new_y = old_bottom - size;

  a->win->Resize(size, size);
  a->win->Move(x, new_y);
  a->r->Resize(size, size);
  a->model->Resize(size, size);

  // 行为边界按新窗口宽重算(爬行范围 = 工作区宽 - 窗口宽)
  a->b->SetBounds(static_cast<float>(a->work_left),
                  static_cast<float>(a->work_left + a->work_w - size));
  a->b->x = std::clamp(a->b->x, static_cast<float>(a->work_left),
                       static_cast<float>(a->work_left + a->work_w - size));
  if (!a->pinned_bottom) {
    a->b->y = std::clamp(static_cast<float>(new_y), static_cast<float>(a->top_y),
                         static_cast<float>(a->bottom_y - size));
  }
}

void Wheel(int delta, void* user) {
  auto* a = static_cast<App*>(user);
  a->wheel_accum += delta;
  const int full = a->wheel_accum / 120;  // 满档数(高分辨率滚轮不足 120 时累积)
  if (full == 0) return;
  a->wheel_accum -= full * 120;

  const float nz = std::clamp(a->zoom + full * kZoomStep, kMinZoom, kMaxZoom);
  if (nz == a->zoom) return;
  a->zoom = nz;
  ApplyZoom(a);
}

// 托盘「设置 → 鼠标追随」切换:翻转内存态(模型层读 b->follow_mouse),回写托盘打勾态。
// cfg 已在 tray 内写盘,这里只同步运行时状态,避免每帧读盘。
void MouseFollowToggle(void* user) {
  auto* a = static_cast<App*>(user);
  a->b->follow_mouse = !a->b->follow_mouse;
  a->look_smooth_x = 0.f;
  a->look_smooth_y = 0.f;
  if (a->tray) a->tray->SetMouseFollow(a->b->follow_mouse);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  // 单实例:第二个实例静默退出(命名互斥体,进程生命周期内持有)。
  Singleton singleton;
  if (!singleton.TryAcquire(L"DesktopetPet_Singleton")) return 0;

  // WIC 贴图加载需要 COM(WIC 工厂 CoCreateInstance 要求初始化公寓)
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  try {
    // 取主显示器工作区(排除任务栏)
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int work_w = work.right - work.left;
    const int bottom_y = work.bottom;  // 屏幕坐标系的底边

    Window win;
    win.Init(hInstance, 400, 400);
    win.Move(work.left + (work_w - 400) / 2, bottom_y - 400);  // 初始:底边中间

    Renderer r;
    r.Init(win.Hwnd(), win.Width(), win.Height());

    auto model = CreateModel();
    model->SetDevice(r.Device(), r.Context());

    // 首次启动需现场编译着色器(~30s):显示提示窗口;缓存就绪后跳过。
    // 提示窗口不依赖色键透明,编译发生在主窗口 Show 之前也能看见。
    Splash splash;
#ifdef HAVE_LIVE2D_SDK
    if (Live2DShaderCacheNeedsCompile()) {
      splash.Show(L"Desktopet 首次启动\n正在编译着色器,请稍候…");
    }
#endif

    // 模型目录:上次选择优先,无效则回退第一个(字典序)。
    std::vector<std::string> models = ListModels();
    std::string current = Settings::ModelName();
    if (std::find(models.begin(), models.end(), current) == models.end()) {
      current = models.empty() ? "Haru" : models[0];
    }
    model->Load(models.empty() ? "assets/Haru" : ModelDir(current));
    splash.Hide();               // 未显示时为 no-op

    Behavior b;
    b.SetBounds(static_cast<float>(work.left), static_cast<float>(work.left + work_w - 400));
    b.x = static_cast<float>(work.left + (work_w - 400) / 2);  // 与窗口初始位置同步
    b.y = static_cast<float>(bottom_y - 400);
    b.follow_mouse = Settings::MouseFollow();  // 启动即读取 cfg

    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> r01(0.f, 1.f);

    // 正式关闭通道:系统托盘右键菜单(切换模型/自启/退出)。托盘不可用时静默降级。
    TrayIcon tray;
    tray.Init(win.Hwnd());
    tray.SetCurrentModel(current);
    tray.SetMouseFollow(b.follow_mouse);  // 初始打勾态与 cfg 一致

    App app{};
    app.win = &win;
    app.r = &r;
    app.model = model.get();
    app.b = &b;
    app.tray = &tray;
    app.current = current;
    app.rng = &rng;
    app.r01 = &r01;
    app.bottom_y = bottom_y;
    app.top_y = work.top;
    app.work_left = work.left;
    app.work_w = work_w;

#ifdef HAVE_LIVE2D_SDK
    if (auto* l2d = dynamic_cast<Live2DModel*>(model.get())) l2d->SetBehavior(&b);
#endif

    win.SetFrameCallback(Frame, &app);
    win.SetMouseCallback(Mouse, &app);
    win.SetDragCallback(Drag, &app);
    win.SetWheelCallback(Wheel, &app);
    win.SetKeyCallback(Key, &app);
    win.SetModelSwitchCallback(ModelSwitch, &app);
    win.SetMouseFollowToggleCallback(MouseFollowToggle, &app);

    win.RunLoop();
    r.Shutdown();
    return 0;
  } catch (const WindowInitError& e) {
    DumpError("WindowInitError", e.msg);
    return 1;
  } catch (const RendererError& e) {
    DumpError("RendererError", e.msg);
    return 1;
  } catch (const ModelLoadError& e) {
    DumpError("ModelLoadError", e.msg);
    return 1;
  } catch (const std::exception& e) {
    DumpError("std::exception", e.what());
    return 1;
  }
}
