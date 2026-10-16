#pragma once
// 透明置顶窗口封装:分层窗口 + 逐像素透明 + 鼠标穿透可切换
#include <windows.h>

// 窗口初始化失败时抛出(消息文本为英文系统错误描述)
struct WindowInitError {
  const char* msg;
};

class Window {
 public:
  Window() = default;
  ~Window();

  // 创建透明、无边框、置顶窗口(不显示,由 RunLoop 内部 Show 显示)
  void Init(HINSTANCE hInstance, int width, int height);
  // 显示并启动消息循环,直到窗口关闭
  void RunLoop();

  // 鼠标穿透开关:true = 点击穿过窗口;false = 窗口接收鼠标
  void SetClickThrough(bool on);
  // 移动窗口(屏幕坐标)。预乘交换链方案下 SetWindowPos 正常工作。
  void Move(int x, int y) {
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  void GetPos(int* x, int* y);
  // 窗口客户区尺寸(模型渲染目标尺寸,逻辑像素)
  int Width() const { return width_; }
  int Height() const { return height_; }

  HWND Hwnd() const { return hwnd_; }

  // 传入的渲染回调,每帧调用。dt 为上一帧到本帧的秒数。
  void SetFrameCallback(void (*cb)(float dt, void* user), void* user) {
    frame_cb_ = cb;
    frame_user_ = user;
  }
  // 传入的鼠标回调。down: 左键按下;x/y: 客户区坐标
  void SetMouseCallback(void (*cb)(bool down, int x, int y, void* user), void* user) {
    mouse_cb_ = cb;
    mouse_user_ = user;
  }
  // 键盘回调(down = 按下)。仅在有焦点时收到;本窗口 NOACTIVATE 常态无焦点,
  // Esc 退出在原型阶段通过临时激活窗口使用,正式版走托盘菜单。
  void SetKeyCallback(void (*cb)(int vk, bool down, void* user), void* user) {
    key_cb_ = cb;
    key_user_ = user;
  }
  // 请求关闭窗口(消息循环结束后 RunLoop 返回)
  void RequestClose() { PostMessage(hwnd_, WM_CLOSE, 0, 0); }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  void HandleMouse(bool down, LPARAM lp);

  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  bool click_through_ = false;
  void (*frame_cb_)(float, void*) = nullptr;
  void* frame_user_ = nullptr;
  void (*mouse_cb_)(bool, int, int, void*) = nullptr;
  void* mouse_user_ = nullptr;
  void (*key_cb_)(int, bool, void*) = nullptr;
  void* key_user_ = nullptr;
};
