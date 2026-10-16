#pragma once
// 启动提示小窗口:首次启动编译着色器时显示,编译完关闭。
// 不依赖色键透明(WS_POPUP 不透明),避免"编译发生在 Show 前、主窗口又全透明"导致的空白。
#include <windows.h>

class Splash {
 public:
  Splash() = default;
  ~Splash() { Hide(); }

  // 显示提示窗口(静态文字)。重复调用幂等。
  void Show(const wchar_t* text);
  // 关闭窗口。未显示时为 no-op。
  void Hide();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  HWND hwnd_ = nullptr;
};
