#include "splash.h"

#include <cstdio>

namespace {
constexpr wchar_t kClassName[] = L"DesktopetSplashWindow";
constexpr int kWidth = 360;
constexpr int kHeight = 120;

// 每个实例的提示文本存这里(GWLP_USERDATA 存 this,再取 m_text)。简单起见存窗口属性。
const wchar_t* kTextProp = L"DesktopetSplashText";
}  // namespace

void Splash::Show(const wchar_t* text) {
  if (hwnd_) return;  // 幂等

  HINSTANCE hInst = GetModuleHandleW(nullptr);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = &Splash::WndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kClassName;
  RegisterClassExW(&wc);  // 重复注册返回 ERROR_CLASS_ALREADY_EXISTS,忽略

  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int x = work.left + ((work.right - work.left) - kWidth) / 2;
  const int y = work.top + ((work.bottom - work.top) - kHeight) / 2;

  hwnd_ = CreateWindowExW(WS_EX_TOPMOST, kClassName, L"Desktopet", WS_POPUP,
                          x, y, kWidth, kHeight, nullptr, nullptr, hInst, nullptr);
  if (!hwnd_) return;

  // 文本存窗口属性,WM_PAINT 里取用
  SetPropW(hwnd_, kTextProp, const_cast<HANDLE>(static_cast<const void*>(text)));

  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd_);  // 同步画一次;之后主线程阻塞编译期间静态文字无需重绘
}

void Splash::Hide() {
  if (!hwnd_) return;
  RemovePropW(hwnd_, kTextProp);
  DestroyWindow(hwnd_);
  hwnd_ = nullptr;
}

LRESULT CALLBACK Splash::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);

      const wchar_t* text =
          static_cast<const wchar_t*>(static_cast<const void*>(GetPropW(hwnd, kTextProp)));

      // 系统消息字体(中文环境即中文字体)
      NONCLIENTMETRICSW ncm{};
      ncm.cbSize = sizeof ncm;
      HFONT font = nullptr;
      if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) {
        font = CreateFontIndirectW(&ncm.lfMessageFont);
      }
      HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;

      RECT rc{0, 0, kWidth, kHeight};
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(60, 60, 60));
      if (text) {
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_NOPREFIX);
      }

      // 边框
      FrameRect(dc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

      if (old) SelectObject(dc, old);
      if (font) DeleteObject(font);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_NCHITTEST:
      return HTNOWHERE;  // 不拦截鼠标点击
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
