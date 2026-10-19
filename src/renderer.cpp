#include "renderer.h"

#include <cstring>

// 方案:逐像素 alpha 透明(UpdateLayeredWindow / ULW_ALPHA)。
//   2026-10-19 探针实测:本机 ULW 可用(预乘 32bpp top-down DIB + AC_SRC_ALPHA
//   显示带平滑渐变边缘的圆盘),推翻早前「ULW 返回 ERROR_GEN_FAILURE」的旧结论。
//   色键(LWA_COLORKEY)方案作废——它只有 1bit 透明,模型边缘半透明像素被硬切成毛刺。
// 流程:
//   D3D11 离屏渲染(预乘 alpha,逐像素)
//   → CopyResource 到 staging → Map 读回
//   → 预乘 BGRA 原样拷进 32bpp top-down DIB(不反预乘、不阈值化)
//   → UpdateLayeredWindow(ULW_ALPHA) 直接合成到桌面
// 素材透明背景天然透明、边缘逐像素平滑,不再依赖色键颜色。

void Renderer::Init(HWND hwnd, int width, int height) {
  hwnd_ = hwnd;
  width_ = width;
  height_ = height;

  D3D_FEATURE_LEVEL fl;
  // DESKTOPET_WARP=1 强制软件光栅:排除显卡驱动的排查手段
  char warp_env[8] = {0};
  D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_HARDWARE;
  if (GetEnvironmentVariableA("DESKTOPET_WARP", warp_env, sizeof warp_env) && warp_env[0] == '1')
    driver_type = D3D_DRIVER_TYPE_WARP;
  HRESULT hr = D3D11CreateDevice(
      nullptr, driver_type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
      nullptr, 0, D3D11_SDK_VERSION, device_.GetAddressOf(), &fl, context_.GetAddressOf());
  if (FAILED(hr)) {
    // 独显失败退 WARP(软件光栅)——桌宠对性能要求低,保证能跑
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, device_.GetAddressOf(), &fl, context_.GetAddressOf());
    if (FAILED(hr)) throw RendererError{"D3D11CreateDevice failed (hardware and WARP)"};
  }

  // 预乘 alpha blend:src + dst*(1-src.a)(与 Live2D 预乘贴图一致)
  D3D11_BLEND_DESC bd{};
  bd.RenderTarget[0].BlendEnable = TRUE;
  bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
  bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  device_->CreateBlendState(&bd, bs_premult_.GetAddressOf());

  CreateTargets();
}

void Renderer::CreateTargets() {
  // 释放旧 DIB/内存 DC(Resize 重进时)
  if (mem_dc_) {
    if (dib_) SelectObject(mem_dc_, GetStockObject(HOLLOW_BRUSH));
    DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
  }
  rtv_.Reset();
  offscreen_.Reset();
  staging_.Reset();
  dib_.reset();

  // 渲染目标纹理:预乘 alpha 写入
  D3D11_TEXTURE2D_DESC td{};
  td.Width = static_cast<UINT>(width_);
  td.Height = static_cast<UINT>(height_);
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device_->CreateTexture2D(&td, nullptr, offscreen_.GetAddressOf())))
    throw RendererError{"CreateTexture2D (offscreen) failed"};
  device_->CreateRenderTargetView(offscreen_.Get(), nullptr, rtv_.GetAddressOf());

  // 回读缓冲(CPU 可读)
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(device_->CreateTexture2D(&td, nullptr, staging_.GetAddressOf())))
    throw RendererError{"CreateTexture2D (staging) failed"};

  // 目标 DIB:32 位 BGRA top-down,预乘 alpha,交给 UpdateLayeredWindow 合成。
  // 初始清零 = 全透明,避免 Show 之前窗口闪黑。
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width_;
  bi.bmiHeader.biHeight = -height_;  // top-down:行序与 D3D 一致
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  dib_.reset(CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dib_bits_, nullptr, 0));
  if (!dib_) throw RendererError{"CreateDIBSection failed"};
  std::memset(dib_bits_, 0, static_cast<size_t>(width_) * height_ * 4);

  // 内存 DC:ULW 要求 DIB 被 SelectObject 进一个 DC 再传给它,此 DC 需常驻。
  mem_dc_ = CreateCompatibleDC(nullptr);
  if (!mem_dc_) throw RendererError{"CreateCompatibleDC failed"};
  SelectObject(mem_dc_, dib_.get());

  // 立即以全透明内容做一次 ULW:让窗口在 ShowWindow 之前就处于已定义的分层状态。
  PresentLayered();
}

void Renderer::Shutdown() {
  if (mem_dc_) {
    if (dib_) SelectObject(mem_dc_, GetStockObject(HOLLOW_BRUSH));
    DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
  }
  rtv_.Reset();
  offscreen_.Reset();
  staging_.Reset();
  dib_.reset();
  bs_premult_.Reset();
  context_.Reset();
  device_.Reset();
}

void Renderer::Begin() {
  const float clear[4] = {0.f, 0.f, 0.f, 0.f};  // 全透明
  context_->ClearRenderTargetView(rtv_.Get(), clear);
  // 注意:绑定用 .Get(),不能用 .GetAddressOf()(后者会先 Release)
  ID3D11RenderTargetView* rtv = rtv_.Get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  context_->OMSetBlendState(bs_premult_.Get(), nullptr, 0xFFFFFFFF);
  D3D11_VIEWPORT vp{0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_), 0.f, 1.f};
  context_->RSSetViewports(1, &vp);
}

void Renderer::PresentLayered() {
  // 用 mem_dc_ 里的预乘 DIB 直接合成到桌面。显式给 ptDst/sz,不依赖 NULL 默认,
  // 否则窗口首次分层可能被按 0×0 处理导致「全透明」。
  POINT ptSrc{0, 0};
  SIZE sz{width_, height_};
  POINT ptDst{0, 0};
  if (hwnd_) {
    RECT r{};
    GetWindowRect(hwnd_, &r);
    ptDst.x = r.left;
    ptDst.y = r.top;
  }
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  HDC screen = GetDC(nullptr);
  UpdateLayeredWindow(hwnd_, screen, &ptDst, &sz, mem_dc_, &ptSrc, 0, &bf, ULW_ALPHA);
  if (screen) ReleaseDC(nullptr, screen);
}

void Renderer::End() {
  // GPU → staging → 读回
  context_->CopyResource(staging_.Get(), offscreen_.Get());
  D3D11_MAPPED_SUBRESOURCE m{};
  if (SUCCEEDED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) {
    const unsigned char* src = static_cast<const unsigned char*>(m.pData);
    const size_t row_bytes = static_cast<size_t>(width_) * 4;
    unsigned char* dst = static_cast<unsigned char*>(dib_bits_);
    // 预乘 BGRA 原样拷贝(不反预乘、不阈值化):alpha 交给 ULW 做逐像素合成。
    for (int y = 0; y < height_; ++y) {
      std::memcpy(dst + static_cast<size_t>(y) * row_bytes,
                  src + static_cast<size_t>(y) * m.RowPitch, row_bytes);
    }
    context_->Unmap(staging_.Get(), 0);
  }

  PresentLayered();
}

void Renderer::Resize(int width, int height) {
  width_ = width;
  height_ = height;
  CreateTargets();
}
