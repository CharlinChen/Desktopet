#pragma once
// 系统托盘图标:右键弹出退出菜单。桌宠窗口 NOACTIVATE 常态无焦点,Esc 之外的正式关闭通道。
#include <windows.h>

class TrayIcon {
 public:
  TrayIcon() = default;
  ~TrayIcon();

  // 添加托盘图标,并把 cbWnd 作为关闭目标(菜单「退出」向它投递 WM_CLOSE)。
  // 失败返回 false(托盘不可用时不应阻止启动,仍有 Esc 兜底)。
  bool Init(HWND cbWnd);
  // 移除图标与隐藏回调窗口(析构自动调用)。
  void Remove();

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static HICON MakePetIcon();
  void ShowMenu();

  static constexpr UINT kCallbackMsg = WM_APP + 1;
  static constexpr UINT kCmdQuit = 1;
  static constexpr UINT kCmdAutostart = 2;

  HWND hwnd_ = nullptr;     // 隐藏的托盘回调窗口
  HWND cb_wnd_ = nullptr;   // 主窗口(关闭目标)
  HICON icon_ = nullptr;    // 需在 NIM_DELETE 前保持有效
  bool added_ = false;
};
