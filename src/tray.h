#pragma once
// 系统托盘图标:右键弹出菜单(切换模型 / 开机自启 / 退出)。
// 桌宠窗口 NOACTIVATE 常态无焦点,Esc 之外的正式关闭通道。
#include <windows.h>

#include <string>
#include <vector>

class TrayIcon {
 public:
  TrayIcon() = default;
  ~TrayIcon();

  // 添加托盘图标,并把 cbWnd 作为消息目标(切换模型投递 kModelSwitchMsg,退出投 WM_CLOSE)。
  // 失败返回 false(托盘不可用时不应阻止启动,仍有 Esc 兜底)。
  bool Init(HWND cbWnd);
  // 移除图标与隐藏回调窗口(析构自动调用)。
  void Remove();
  // 记录当前选中的模型名(托盘菜单用它打勾)。主窗口切换成功后调用。
  void SetCurrentModel(const std::string& name) { current_model_ = name; }
  // 鼠标追随开关的内存态(托盘菜单打勾用)。main 启动/切换后回写。
  void SetMouseFollow(bool on) { mouse_follow_ = on; }
  // 随机移动开关的内存态(托盘菜单打勾用)。
  void SetWander(bool on) { wander_ = on; }
  // 随机动画开关的内存态(托盘菜单打勾用)。
  void SetRandomAnimation(bool on) { random_animation_ = on; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static HICON MakePetIcon();
  void ShowMenu();

  static constexpr UINT kCallbackMsg = WM_APP + 1;
  static constexpr UINT kCmdQuit = 1;
  static constexpr UINT kCmdAutostart = 2;
  static constexpr UINT kCmdMouseFollow = 3;
  static constexpr UINT kCmdWander = 4;
  static constexpr UINT kCmdRandomAnimation = 5;
  static constexpr UINT kCmdModelBase = 100;  // 模型菜单项 ID 从 100 起(下标 = id - base)

  HWND hwnd_ = nullptr;     // 隐藏的托盘回调窗口
  HWND cb_wnd_ = nullptr;   // 主窗口(切换模型/关闭目标)
  HICON icon_ = nullptr;    // 需在 NIM_DELETE 前保持有效
  bool added_ = false;
  bool mouse_follow_ = false;         // 鼠标追随开关内存态(打勾)
  bool wander_ = true;                // 随机移动开关内存态(打勾)
  bool random_animation_ = true;      // 随机动画开关内存态(打勾)
  std::vector<std::string> models_;  // 当前模型目录名(弹出菜单时刷新)
  std::string current_model_;        // 已选中模型目录名(打勾)
};
