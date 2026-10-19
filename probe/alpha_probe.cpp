// 独立透明方案探针:不依赖主程序,只验证「本机到底能不能做逐像素 alpha 透明」。
// 编译:见 probe/build_probe.sh。运行:双击或命令行,窗口会依次弹出各 2.5 秒。
//
// 四个探针:
//   A) CreateSwapChainForHwnd + FLIP_DISCARD + PREMULTIPLIED
//      —— 重测记忆里「DXGI_ERROR_INVALID_CALL」那次,但换成翻转模型(当时可能是
//         discard,而 D3D11 规定 discard 模式下禁用 per-pixel alpha)。
//   B) DirectComposition + CreateSwapChainForComposition + PREMULTIPLIED
//      —— Win8+ 官方推荐的逐像素 alpha 现代路径。
//   C) UpdateLayeredWindow + AC_SRC_ALPHA(top-down 32bpp 预乘 DIB)
//      —— 重测记忆里「ERROR_GEN_FAILURE」那次,补全格式要求。
//   D) LWA_COLORKEY 色键基线(对照)—— 当前主程序方案,确认探针壳本身有效。
//
// 每步 HRESULT 打印到 stdout 并追加到 alpha_probe.log(位于 exe 同目录)。
// 视觉判据:中央的红色圆盘边缘应是平滑渐隐(逐像素 alpha 生效);若边缘硬切、
// 出现黑框或品红毛边,则该方案在本机不可用。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <dwmapi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 最小 COM 指针(不依赖 wrl,自成一体)
template <typename T>
class Ptr {
  T* p_ = nullptr;

 public:
  Ptr() = default;
  ~Ptr() { Reset(); }
  Ptr(const Ptr&) = delete;
  Ptr& operator=(const Ptr&) = delete;
  T* Get() const { return p_; }
  T** GetAddressOf() { Reset(); return &p_; }
  T* operator->() const { return p_; }
  explicit operator bool() const { return p_ != nullptr; }
  void Reset() {
    if (p_) { p_->Release(); p_ = nullptr; }
  }
};

// ---------- 通用工具 ----------

static constexpr int kW = 320;
static constexpr int kH = 320;

static const wchar_t* ExeDir() {
  static wchar_t buf[MAX_PATH];
  static const wchar_t* dir = []() -> const wchar_t* {
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (!n) return L"";
    wchar_t* p = buf + n;
    while (p > buf && *p != L'\\' && *p != L'/') --p;
    *(p + 1) = L'\0';
    return buf;
  }();
  return dir;
}

static void Log(const char* fmt, ...) {
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  printf("%s\n", line);
  wchar_t path[MAX_PATH];
  swprintf(path, MAX_PATH, L"%salpha_probe.log", ExeDir());
  FILE* f = _wfopen(path, L"a");
  if (f) { fprintf(f, "%s\n", line); fclose(f); }
}

static void HR(const char* what, HRESULT hr) {
  Log("%-44s : 0x%08lX %s", what, (unsigned long)hr,
      SUCCEEDED(hr) ? "OK" : "FAIL");
}

// 生成测试图:中央红色圆盘,边缘 ~14px 平滑渐隐(预乘 alpha,BGRA 字节序)。
// 这是验证「8-bit alpha 是否被窗口尊重」的关键:平滑渐隐 = 支持;硬切 = 不支持。
static void FillCircle(unsigned char* px, int W, int H) {
  const double cx = W / 2.0, cy = H / 2.0;
  const double R = (W < H ? W : H) / 3.0;
  const double soft = 14.0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const double d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      double a = (R - d) / soft;
      a = a > 1.0 ? 1.0 : (a < 0.0 ? 0.0 : a);
      const unsigned char A = static_cast<unsigned char>(a * 255.0 + 0.5);
      px[(y * W + x) * 4 + 0] = 0;      // B
      px[(y * W + x) * 4 + 1] = 0;      // G
      px[(y * W + x) * 4 + 2] = A;      // R(预乘:r = a)
      px[(y * W + x) * 4 + 3] = A;      // A
    }
}

static void Pump(DWORD ms) {
  const DWORD start = GetTickCount();
  while (GetTickCount() - start < ms) {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
    Sleep(10);
  }
}

static HWND MakeWindow(DWORD exStyle) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AlphaProbeWnd";
    RegisterClassExW(&wc);
    registered = true;
  }
  return CreateWindowExW(exStyle, L"AlphaProbeWnd", L"alpha probe", WS_POPUP,
                         80, 80, kW, kH, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// ---------- 探针 A:CreateSwapChainForHwnd + FLIP + PREMULTIPLIED ----------

static void ProbeA_FlipPremult() {
  Log("\n=== A) CreateSwapChainForHwnd + FLIP_DISCARD + PREMULTIPLIED ===");
  HWND hwnd = MakeWindow(WS_EX_TOPMOST | WS_EX_NOACTIVATE);

  Ptr<ID3D11Device> dev;
  Ptr<ID3D11DeviceContext> ctx;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, dev.GetAddressOf(), nullptr, ctx.GetAddressOf());
  HR("D3D11CreateDevice", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<IDXGIDevice> dxgiDev;
  hr = dev->QueryInterface(IID_PPV_ARGS(dxgiDev.GetAddressOf()));
  HR("QueryInterface IDXGIDevice", hr);
  Ptr<IDXGIAdapter> adapter;
  if (SUCCEEDED(hr)) hr = dxgiDev->GetAdapter(adapter.GetAddressOf());
  HR("GetAdapter", hr);
  Ptr<IDXGIFactory2> factory;
  if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
  HR("GetParent IDXGIFactory2", hr);

  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = kW;
  sd.Height = kH;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.Stereo = FALSE;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.Scaling = DXGI_SCALING_STRETCH;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  sd.Flags = 0;

  Ptr<IDXGISwapChain1> sc;
  hr = factory->CreateSwapChainForHwnd(dev.Get(), hwnd, &sd, nullptr, nullptr, sc.GetAddressOf());
  HR("CreateSwapChainForHwnd(FLIP+PREMULT)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  // 渐变源纹理(默认池,BGRA,一次 Upload,之后 CopyResource 到后备缓冲)
  D3D11_TEXTURE2D_DESC td{};
  td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = 0;
  std::vector<unsigned char> px(kW * kH * 4);
  FillCircle(px.data(), kW, kH);
  D3D11_SUBRESOURCE_DATA srd{px.data(), static_cast<UINT>(kW * 4), 0};
  Ptr<ID3D11Texture2D> grad;
  hr = dev->CreateTexture2D(&td, &srd, grad.GetAddressOf());
  HR("CreateTexture2D (gradient)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<ID3D11Texture2D> back;
  hr = sc->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()));
  HR("GetBuffer(0)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  ctx->CopyResource(back.Get(), grad.Get());
  sc->Present(1, 0);
  HR("Present", S_OK);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  Log("    -> 看窗口:圆盘边缘应平滑渐隐;硬切/黑框 = 不支持");
  Pump(2500);
  DestroyWindow(hwnd);
}

// ---------- 探针 B:DirectComposition ----------

static void ProbeB_DComposition() {
  Log("\n=== B) DirectComposition + CreateSwapChainForComposition + PREMULTIPLIED ===");
  // DComp 目标窗口:NOREDIRECTION,不能是 LAYERED
  HWND hwnd = MakeWindow(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_NOACTIVATE);

  Ptr<ID3D11Device> dev;
  Ptr<ID3D11DeviceContext> ctx;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, dev.GetAddressOf(), nullptr, ctx.GetAddressOf());
  HR("D3D11CreateDevice", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<IDXGIDevice> dxgiDev;
  hr = dev->QueryInterface(IID_PPV_ARGS(dxgiDev.GetAddressOf()));
  HR("QueryInterface IDXGIDevice", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<IDCompositionDevice> dcomp;
  hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(dcomp.GetAddressOf()));
  HR("DCompositionCreateDevice", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  // CreateDevice(v1) 只返回 IDCompositionDevice,拿桌面设备要 CreateDevice2
  Ptr<IDCompositionDesktopDevice> desktop;
  hr = DCompositionCreateDevice2(dxgiDev.Get(), IID_PPV_ARGS(desktop.GetAddressOf()));
  HR("DCompositionCreateDevice2 (desktop)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<IDXGIAdapter> adapter;
  hr = dxgiDev->GetAdapter(adapter.GetAddressOf());
  HR("GetAdapter", hr);
  Ptr<IDXGIFactory2> factory;
  if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
  HR("GetParent IDXGIFactory2", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = kW; sd.Height = kH; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.Stereo = FALSE; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
  sd.Scaling = DXGI_SCALING_STRETCH; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; sd.Flags = 0;

  Ptr<IDXGISwapChain1> sc;
  hr = factory->CreateSwapChainForComposition(dev.Get(), &sd, nullptr, sc.GetAddressOf());
  HR("CreateSwapChainForComposition(PREMULT)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<IDCompositionTarget> target;
  hr = desktop->CreateTargetForHwnd(hwnd, TRUE, target.GetAddressOf());
  HR("CreateTargetForHwnd", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  // 关键:visual 必须从「同一个」桌面设备创建。之前用 v1 的 dcomp->CreateVisual
  // 造 visual、又用 v2 的 desktop 造 target,跨设备 SetRoot → E_INVALIDARG。
  Ptr<IDCompositionVisual2> visual;
  hr = desktop->CreateVisual(visual.GetAddressOf());
  HR("desktop::CreateVisual", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  hr = visual->SetContent(sc.Get());
  HR("Visual::SetContent(swapchain)", hr);
  hr = target->SetRoot(visual.Get());
  HR("Target::SetRoot", hr);
  hr = dcomp->Commit();
  HR("Commit", hr);

  D3D11_TEXTURE2D_DESC td{};
  td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = 0;
  std::vector<unsigned char> px(kW * kH * 4);
  FillCircle(px.data(), kW, kH);
  D3D11_SUBRESOURCE_DATA srd{px.data(), static_cast<UINT>(kW * 4), 0};
  Ptr<ID3D11Texture2D> grad;
  hr = dev->CreateTexture2D(&td, &srd, grad.GetAddressOf());
  HR("CreateTexture2D (gradient)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  Ptr<ID3D11Texture2D> back;
  hr = sc->GetBuffer(0, IID_PPV_ARGS(back.GetAddressOf()));
  HR("GetBuffer(0)", hr);
  if (FAILED(hr)) { DestroyWindow(hwnd); return; }

  ctx->CopyResource(back.Get(), grad.Get());
  sc->Present(1, 0);
  HR("Present", S_OK);
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  Log("    -> 看窗口:圆盘边缘应平滑渐隐");
  Pump(2500);
  DestroyWindow(hwnd);
}

// ---------- 探针 C:UpdateLayeredWindow ----------

static void ProbeC_ULW(DWORD dwell = 2500) {
  Log("\n=== C) UpdateLayeredWindow + AC_SRC_ALPHA ===");
  BOOL dwmOn = FALSE;
  DwmIsCompositionEnabled(&dwmOn);
  Log("DwmIsCompositionEnabled = %d", dwmOn);

  HWND hwnd = MakeWindow(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE);

  // 32bpp top-down(负高度)预乘 DIB
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = kW;
  bi.bmiHeader.biHeight = -kH;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  HBITMAP dib = CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HR("CreateDIBSection(32bpp top-down)", dib ? S_OK : HRESULT_FROM_WIN32(GetLastError()));
  if (!dib) { DeleteDC(memDc); ReleaseDC(nullptr, screenDc); DestroyWindow(hwnd); return; }

  FillCircle(static_cast<unsigned char*>(bits), kW, kH);
  HGDIOBJ old = SelectObject(memDc, dib);

  POINT ptSrc{0, 0};
  SIZE sz{kW, kH};
  POINT ptDst{0, 0};
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  BOOL ok = UpdateLayeredWindow(hwnd, screenDc, &ptDst, &sz, memDc, &ptSrc, 0, &bf, ULW_ALPHA);
  HR("UpdateLayeredWindow(ULW_ALPHA)", ok ? S_OK : HRESULT_FROM_WIN32(GetLastError()));

  SelectObject(memDc, old);
  DeleteObject(dib);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);

  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  Log("    -> 看窗口:圆盘边缘应平滑渐隐;全黑/全透明/不显示 = 失败");
  Pump(dwell);
  DestroyWindow(hwnd);
}

// ---------- 探针 D:色键基线(对照) ----------

static void ProbeD_ColorKey() {
  Log("\n=== D) LWA_COLORKEY 色键基线(当前主程序方案) ===");
  constexpr COLORREF kKey = RGB(255, 0, 255);
  HWND hwnd = MakeWindow(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
  if (!SetLayeredWindowAttributes(hwnd, kKey, 0, LWA_COLORKEY))
    HR("SetLayeredWindowAttributes", HRESULT_FROM_WIN32(GetLastError()));
  else
    HR("SetLayeredWindowAttributes", S_OK);

  // 复刻当前 renderer 的阈值化:alpha<8 → 品红;否则反预乘红色
  std::vector<unsigned char> px(kW * kH * 4);
  FillCircle(px.data(), kW, kH);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      unsigned char* p = &px[(y * kW + x) * 4];
      const unsigned char a = p[3];
      if (a < 8) { p[0] = 0xFF; p[1] = 0x00; p[2] = 0xFF; }  // 品红
      else       { p[0] = 0x00; p[1] = 0x00; p[2] = 0xFF; }  // 反预乘红(实心)
      p[3] = 0;
    }

  HDC dc = GetDC(hwnd);
  HDC mem = CreateCompatibleDC(dc);
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = kW; bi.bmiHeader.biHeight = -kH;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  memcpy(bits, px.data(), px.size());
  HGDIOBJ old = SelectObject(mem, dib);
  BitBlt(dc, 0, 0, kW, kH, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(dib);
  DeleteDC(mem);
  ReleaseDC(hwnd, dc);

  ShowWindow(hwnd, SW_SHOWNOACTIVATE);
  Log("    -> 看窗口:圆盘边缘硬切(有毛边)= 当前方案的固有缺陷");
  Pump(2500);
  DestroyWindow(hwnd);
}

// ---------- 入口 ----------

int main(int argc, char** argv) {
  wchar_t path[MAX_PATH];
  swprintf(path, MAX_PATH, L"%salpha_probe.log", ExeDir());
  FILE* f = _wfopen(path, L"w");  // 每轮清空重写
  if (f) fclose(f);

  // 命令行可单独跑某探针:alpha_probe.exe C [秒] 只跑 ULW 并停留指定秒数
  char which = argc > 1 ? argv[1][0] : 0;
  if (which) {
    double sec = argc > 2 ? atof(argv[2]) : 8.0;
    switch (which) {
      case 'A': ProbeA_FlipPremult(); break;
      case 'B': ProbeB_DComposition(); break;
      case 'C': ProbeC_ULW(static_cast<DWORD>(sec * 1000)); break;
      case 'D': ProbeD_ColorKey(); break;
    }
    Log("\n完成(单独跑 %c)。", which);
    return 0;
  }

  Log("alpha_probe 开始(结果同时写 alpha_probe.log)");
  ProbeA_FlipPremult();
  ProbeB_DComposition();
  ProbeC_ULW();
  ProbeD_ColorKey();
  Log("\n完成。综合结论见窗口观感 + 上面各步 HRESULT。");
  return 0;
}
