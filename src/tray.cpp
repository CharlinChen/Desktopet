#include "tray.h"

#include "autostart.h"

namespace {

constexpr wchar_t kClassName[] = L"DesktopetTrayWnd";
constexpr UINT kTaskbarRestartMsg = 0x02A8;  // TaskbarCreated 广播(资源管理器重启后重建托盘)

}  // namespace

TrayIcon::~TrayIcon() { Remove(); }

bool TrayIcon::Init(HWND cbWnd) {
  cb_wnd_ = cbWnd;

  // 注册隐藏回调窗口(仅用于接收托盘消息,不 Show)
  HINSTANCE hInst = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = &TrayIcon::WndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);  // 重复注册返回 ERROR_CLASS_ALREADY_EXISTS,忽略

  hwnd_ = CreateWindowExW(0, kClassName, L"", WS_POPUP, 0, 0, 0, 0,
                          nullptr, nullptr, hInst, this);
  if (!hwnd_) return false;

  icon_ = MakePetIcon();

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof nid;
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = kCallbackMsg;
  nid.hIcon = icon_;
  wcscpy_s(nid.szTip, L"Desktopet 桌宠");  // szTip 是 128 宽;固定文案远小于上限

  added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
  return added_;
}

void TrayIcon::Remove() {
  if (added_) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd_;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
  }
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (icon_) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
}

void TrayIcon::ShowMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, kCmdAutostart,
              AutoStart::IsEnabled() ? L"开机自启(已开启)" : L"开机自启(已关闭)");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCmdQuit, L"退出");

  // 弹出菜单后需 SetForegroundWindow,否则菜单无法随点击外部自动关闭(托盘菜单经典坑)
  POINT pt{};
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                 pt.x, pt.y, 0, hwnd_, nullptr);
  PostMessage(hwnd_, WM_NULL, 0, 0);  // 让取消菜单的消息有处可去

  DestroyMenu(menu);

  switch (cmd) {
    case kCmdAutostart:
      AutoStart::SetEnabled(!AutoStart::IsEnabled());
      break;
    case kCmdQuit:
      if (cb_wnd_) PostMessage(cb_wnd_, WM_CLOSE, 0, 0);
      break;
  }
}

// 宠物图标:5×5 窗口内画一只 3×3 的"爪子"(橙点 = 身体,其余透明)。
// 单色掩码 bit=1 表示透明。系统托盘会缩放到合适尺寸,保持轻量。
HICON TrayIcon::MakePetIcon() {
  constexpr int N = 5;
  constexpr COLORREF kBody = RGB(255, 140, 0);

  HDC hdc = GetDC(nullptr);
  if (!hdc) return nullptr;

  // 颜色位图(32bpp,自上而下)
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof bi.bmiHeader;
  bi.bmiHeader.biWidth = N;
  bi.bmiHeader.biHeight = -N;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bmpColor = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmpColor) {
    ReleaseDC(nullptr, hdc);
    return nullptr;
  }
  auto* px = static_cast<COLORREF*>(bits);
  for (int i = 0; i < N * N; ++i) px[i] = RGB(0, 0, 0);
  for (int row = 1; row <= 3; ++row)
    for (int col = 1; col <= 3; ++col)
      px[row * N + col] = kBody;

  // 掩码位图:bit=0 处显示颜色,bit=1 处透明(实点位置保持 0)
  BITMAPINFO mbi{};
  mbi.bmiHeader.biSize = sizeof mbi.bmiHeader;
  mbi.bmiHeader.biWidth = N;
  mbi.bmiHeader.biHeight = -N;
  mbi.bmiHeader.biPlanes = 1;
  mbi.bmiHeader.biBitCount = 1;
  mbi.bmiHeader.biCompression = BI_RGB;
  void* mbits = nullptr;
  HBITMAP bmpMask = CreateDIBSection(hdc, &mbi, DIB_RGB_COLORS, &mbits, nullptr, 0);
  if (bmpMask && mbits) {
    auto* mp = static_cast<unsigned char*>(mbits);
    const int stride = ((N + 31) / 32) * 4;  // 单色位图行字节数,4 字节对齐
    for (int r = 0; r < N; ++r) {
      unsigned char mask = 0;
      for (int c = 0; c < N; ++c) {
        const bool opaque = (r >= 1 && r <= 3 && c >= 1 && c <= 3);
        if (!opaque) mask |= static_cast<unsigned char>(0x80 >> c);
      }
      for (int b = 0; b < stride; ++b) mp[r * stride + b] = (b == 0) ? mask : 0;
    }
  }

  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmColor = bmpColor;
  ii.hbmMask = bmpMask;
  HICON icon = CreateIconIndirect(&ii);

  DeleteObject(bmpColor);
  if (bmpMask) DeleteObject(bmpMask);
  ReleaseDC(nullptr, hdc);
  return icon;
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = reinterpret_cast<TrayIcon*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

  switch (msg) {
    case kCallbackMsg:
      if (LOWORD(lp) == WM_RBUTTONUP) self->ShowMenu();
      return 0;
    case kTaskbarRestartMsg:
      // 资源管理器重启后托盘图标消失,重新添加
      if (self->added_ && self->hwnd_) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof nid;
        nid.hWnd = self->hwnd_;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kCallbackMsg;
        nid.hIcon = self->icon_;
        wcscpy_s(nid.szTip, L"Desktopet 桌宠");
        Shell_NotifyIconW(NIM_ADD, &nid);
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
