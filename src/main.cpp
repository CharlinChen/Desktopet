#include "behavior.h"
#include "model.h"
#include "renderer.h"
#include "singleton.h"
#include "splash.h"
#include "tray.h"
#include "window.h"

#ifdef HAVE_LIVE2D_SDK
#include "model_live2d.h"
#endif

#include <algorithm>
#include <chrono>
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

struct App {
  Window* win;
  Renderer* r;
  Model* model;
  Behavior* b;
  std::mt19937* rng;
  std::uniform_real_distribution<float>* r01;
  int bottom_y;          // 工作区底边(屏幕坐标)
  int top_y = 0;         // 工作区顶边(屏幕坐标)
  bool dragging = false;     // 用户正在拖动窗口
  bool pinned_bottom = true; // 拖拽前强制贴底;拖动后置 false,停在用户放置处
  bool first_frame = true;
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
    model->Load("assets/Haru");  // Live2D 模型目录(model3.json 等)
    splash.Hide();               // 未显示时为 no-op

    Behavior b;
    b.SetBounds(static_cast<float>(work.left), static_cast<float>(work.left + work_w - 400));
    b.x = static_cast<float>(work.left + (work_w - 400) / 2);  // 与窗口初始位置同步
    b.y = static_cast<float>(bottom_y - 400);

    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> r01(0.f, 1.f);

    App app{&win, &r, model.get(), &b, &rng, &r01, bottom_y};
    app.top_y = work.top;

#ifdef HAVE_LIVE2D_SDK
    if (auto* l2d = dynamic_cast<Live2DModel*>(model.get())) l2d->SetBehavior(&b);
#endif

    win.SetFrameCallback(Frame, &app);
    win.SetMouseCallback(Mouse, &app);
    win.SetDragCallback(Drag, &app);
    win.SetKeyCallback(Key, &app);

    // 正式关闭通道:系统托盘右键「退出」。托盘不可用时静默降级(仍有 Esc 兜底)。
    TrayIcon tray;
    tray.Init(win.Hwnd());

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
