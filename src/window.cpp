#include "window.h"

#include <cstdio>

namespace {

constexpr wchar_t kClassName[] = L"DesktopetPetWindow";
constexpr COLORREF kColorKey = RGB(255, 0, 255);  // 品红:色键透明

LARGE_INTEGER g_qpcFreq;

struct Ctx {
  Window* self;
};

// WM_TIMER 驱动帧循环;DT 用 QPC 测量,与 D3D Present 解耦(反正都是 60Hz 顶格)
constexpr UINT_PTR kFrameTimer = 1;

void Win32ErrorMessage(const char* what, DWORD err) {
  char* buf = nullptr;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, err, 0, reinterpret_cast<char*>(&buf), 0, nullptr);
  char msg[512];
  snprintf(msg, sizeof msg, "%s (win32 error %lu: %s)", what, static_cast<unsigned long>(err),
           buf ? buf : "unknown");
  if (buf) LocalFree(buf);
  throw WindowInitError{msg};
}

}  // namespace

Window::~Window() {
  if (hwnd_) DestroyWindow(hwnd_);
}

void Window::Init(HINSTANCE hInstance, int width, int height) {
  width_ = width;
  height_ = height;
  QueryPerformanceFrequency(&g_qpcFreq);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = &Window::WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  if (!RegisterClassExW(&wc)) Win32ErrorMessage("RegisterClassExW failed", GetLastError());

  // 分层窗口 + 色键透明(LWA_COLORKEY)。
  // 本机不支持 ULW(UpdateLayeredWindow 返回 ERROR_GEN_FAILURE)与预乘
  // alpha 交换链(DXGI_ERROR_INVALID_CALL),色键是唯一可用路径:
  // 品红 (255,0,255) 像素被 DWM 完全抠掉,其余像素正常合成。
  const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
  hwnd_ = CreateWindowExW(exStyle, kClassName, L"Desktopet", WS_POPUP,
                          CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                          nullptr, nullptr, hInstance, this);
  if (!hwnd_) Win32ErrorMessage("CreateWindowExW failed", GetLastError());

  // 关键:先注册色键再 Show。渲染器每帧用 GDI 把(阈值化+反预乘后的)
  // 帧图 BitBlt 到窗口 DC,品红区即透明。
  if (!SetLayeredWindowAttributes(hwnd_, kColorKey, 0, LWA_COLORKEY))
    Win32ErrorMessage("SetLayeredWindowAttributes failed", GetLastError());
}

void Window::RunLoop() {
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetTimer(hwnd_, kFrameTimer, 16, nullptr);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void Window::SetClickThrough(bool on) {
  if (on == click_through_) return;
  click_through_ = on;
  LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  if (on)
    ex |= WS_EX_TRANSPARENT;
  else
    ex &= ~WS_EX_TRANSPARENT;
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void Window::GetPos(int* x, int* y) {
  RECT r;
  GetWindowRect(hwnd_, &r);
  *x = r.left;
  *y = r.top;
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = reinterpret_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

  switch (msg) {
    case WM_TIMER:
      if (wp == kFrameTimer && self->frame_cb_) {
        static LARGE_INTEGER last{};
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = last.QuadPart ? static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(g_qpcFreq.QuadPart)
                                       : 1.0f / 60.0f;
        last = now;
        self->frame_cb_(dt, self->frame_user_);
      }
      return 0;

    case WM_LBUTTONDOWN:
      self->HandleMouse(true, lp);
      return 0;
    case WM_LBUTTONUP:
      self->HandleMouse(false, lp);
      return 0;

    case WM_KEYDOWN:
      if (self->key_cb_) self->key_cb_(static_cast<int>(wp), true, self->key_user_);
      return 0;
    case WM_KEYUP:
      if (self->key_cb_) self->key_cb_(static_cast<int>(wp), false, self->key_user_);
      return 0;

    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void Window::HandleMouse(bool down, LPARAM lp) {
  if (!mouse_cb_) return;
  const int x = static_cast<int>(static_cast<short>(LOWORD(lp)));
  const int y = static_cast<int>(static_cast<short>(HIWORD(lp)));
  mouse_cb_(down, x, y, mouse_user_);
}
