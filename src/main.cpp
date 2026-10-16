#include "behavior.h"
#include "model.h"
#include "renderer.h"
#include "window.h"

#ifdef HAVE_LIVE2D_SDK
#include "model_live2d.h"
#endif

#include <chrono>
#include <cstdio>
#include <random>

static void DumpError(const char* what, const char* msg) {
  FILE* f = fopen("E:/Project/Desktopet/startup_error.log", "a");
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
  int bottom_y;  // 屏幕坐标系的底边 y
  bool first_frame = true;
};

void Frame(float dt, void* user) {
  auto* a = static_cast<App*>(user);

  // 首帧:DT 会因窗口创建耗时而异常大,清零行为时钟,避免首帧跳变
  if (a->first_frame) {
    a->first_frame = false;
    a->b->ResetClock();
  }

  a->b->MaybeSwitch((*a->r01)(*a->rng));
  a->b->Update(dt);

  // 窗口跟随行为位置(贴底边;换边逻辑后续按 edge 处理 y)
  a->win->Move(static_cast<int>(a->b->x), a->bottom_y - a->win->Height());

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

void Key(int vk, bool down, void* user) {
  auto* a = static_cast<App*>(user);
  if (down && vk == VK_ESCAPE) a->win->RequestClose();
}

}  // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
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
    model->Load("assets/Haru");  // Live2D 模型目录(model3.json 等)

    Behavior b;
    b.SetBounds(static_cast<float>(work.left), static_cast<float>(work.left + work_w - 400));
    b.x = static_cast<float>(work.left + (work_w - 400) / 2);  // 与窗口初始位置同步
    b.y = static_cast<float>(bottom_y - 400);

    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> r01(0.f, 1.f);

    App app{&win, &r, model.get(), &b, &rng, &r01, bottom_y};

#ifdef HAVE_LIVE2D_SDK
    if (auto* l2d = dynamic_cast<Live2DModel*>(model.get())) l2d->SetBehavior(&b);
#endif

    win.SetFrameCallback(Frame, &app);
    win.SetMouseCallback(Mouse, &app);
    win.SetKeyCallback(Key, &app);

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
