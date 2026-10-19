#pragma once
// 透明置顶窗口封装:分层窗口 + 逐像素透明 + 鼠标穿透可切换
#include <windows.h>

#include <cstdint>

// 窗口初始化失败时抛出(消息文本为英文系统错误描述)
struct WindowInitError {
  const char* msg;
};

// 拖拽回调的阶段
enum class DragPhase { Begin, Move, End };

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
  // 调整窗口尺寸(缩放用)。SWP_NOACTIVATE 保持不抢焦点。
  void Resize(int width, int height);

  HWND Hwnd() const { return hwnd_; }

  // 传入的渲染回调,每帧调用。dt 为上一帧到本帧的秒数。
  void SetFrameCallback(void (*cb)(float dt, void* user), void* user) {
    frame_cb_ = cb;
    frame_user_ = user;
  }
  // 点击回调:按下并原地释放(未越过拖动阈值)时触发,down 恒为 true。
  void SetMouseCallback(void (*cb)(bool down, int x, int y, void* user), void* user) {
    mouse_cb_ = cb;
    mouse_user_ = user;
  }
  // 拖拽回调:x/y 为窗口新左上角(屏幕坐标);phase 依次 Begin → Move* → End。
  // 拖拽期间窗口位置已由 Window 内部 Move,回调只需同步行为层坐标。
  void SetDragCallback(void (*cb)(int x, int y, DragPhase phase, void* user), void* user) {
    drag_cb_ = cb;
    drag_user_ = user;
  }
  // 滚轮回调(经 WM_INPUT/RIDEV_INPUTSINK 接收,窗口无焦点时也投递)。
  // delta > 0 向上滚,< 0 向下滚(通常为 ±120 的倍数)。
  void SetWheelCallback(void (*cb)(int delta, void* user), void* user) {
    wheel_cb_ = cb;
    wheel_user_ = user;
  }
  // 键盘回调(down = 按下)。仅在有焦点时收到;本窗口 NOACTIVATE 常态无焦点,
  // Esc 退出在原型阶段通过临时激活窗口使用,正式版走托盘菜单。
  void SetKeyCallback(void (*cb)(int vk, bool down, void* user), void* user) {
    key_cb_ = cb;
    key_user_ = user;
  }
  // 模型切换回调(托盘菜单投递 kModelSwitchMsg 时触发;wp = 模型下标)。
  void SetModelSwitchCallback(void (*cb)(std::uintptr_t wp, void* user), void* user) {
    modelswitch_cb_ = cb;
    modelswitch_user_ = user;
  }
  // 鼠标追随开关回调(托盘「设置」项投递 kMouseFollowToggleMsg 时触发)。
  void SetMouseFollowToggleCallback(void (*cb)(void* user), void* user) {
    mousefollow_cb_ = cb;
    mousefollow_user_ = user;
  }
  // 随机移动开关回调(托盘「设置」项投递 kWanderToggleMsg 时触发)。
  void SetWanderToggleCallback(void (*cb)(void* user), void* user) {
    wander_cb_ = cb;
    wander_user_ = user;
  }
  // 随机动画开关回调(托盘「设置」项投递 kRandomAnimationToggleMsg 时触发)。
  void SetRandomAnimationToggleCallback(void (*cb)(void* user), void* user) {
    random_animation_cb_ = cb;
    random_animation_user_ = user;
  }
  // 请求关闭窗口(消息循环结束后 RunLoop 返回)
  void RequestClose() { PostMessage(hwnd_, WM_CLOSE, 0, 0); }

  // 物理屏幕坐标 → 窗口逻辑坐标。DPI 未感知进程下 GetCursorPos 返回物理像素,
  // 而 GetWindowRect 返回被系统 DPI 虚拟化的逻辑像素,直接比较会差一个缩放因子。
  // 供 main(鼠标追随)等需要"光标 vs 窗口位置"对齐的场景调用。100% 缩放下原样返回。
  static POINT VirtualizeCursor(POINT physical);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  POINT CursorPos() const;

  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  bool click_through_ = false;
  void (*frame_cb_)(float, void*) = nullptr;
  void* frame_user_ = nullptr;
  void (*mouse_cb_)(bool, int, int, void*) = nullptr;
  void* mouse_user_ = nullptr;
  void (*drag_cb_)(int, int, DragPhase, void*) = nullptr;
  void* drag_user_ = nullptr;
  void (*wheel_cb_)(int, void*) = nullptr;
  void* wheel_user_ = nullptr;
  void (*key_cb_)(int, bool, void*) = nullptr;
  void* key_user_ = nullptr;
  void (*modelswitch_cb_)(std::uintptr_t, void*) = nullptr;
  void* modelswitch_user_ = nullptr;
  void (*mousefollow_cb_)(void*) = nullptr;
  void* mousefollow_user_ = nullptr;
  void (*wander_cb_)(void*) = nullptr;
  void* wander_user_ = nullptr;
  void (*random_animation_cb_)(void*) = nullptr;
  void* random_animation_user_ = nullptr;

  // 拖拽状态:按下时的光标/窗口位置,以及是否已越过拖动阈值
  POINT drag_grab_cursor_{};
  POINT drag_grab_win_{};
  POINT drag_cursor_{};
  bool drag_active_ = false;    // 左键按住(尚未判定点击/拖动)
  bool drag_started_ = false;   // 已越过阈值,确认为拖动
};
