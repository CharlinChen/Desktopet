#include "renderer.h"

#include <cstring>

// 方案:色键透明(LWA_COLORKEY,品红抠除)。
//   本机不支持 DXGI 预乘 alpha 交换链(CreateSwapChainForHwnd 对
//   PREMULTIPLIED/STRAIGHT 返回 DXGI_ERROR_INVALID_CALL),也不支持
//   UpdateLayeredWindow(返回 ERROR_GEN_FAILURE)。色键是唯一可用路径。
// 流程:
//   D3D11 离屏渲染(预乘 alpha,逐像素)
//   → CopyResource 到 staging → Map 读回
//   → CPU 逐像素:alpha≈0 → 品红(色键);否则反预乘(rgb/a)
//   → SetDIBits 进 DIB → GDI BitBlt 到窗口 DC
// 注意窗口类没有 CS_OWNDC,GetDC 每次都是临时 DC——BitBlt 是同步的,
// 对桌宠 400x400 每帧开销可接受。

namespace {
constexpr int kAlphaThreshold = 8;  // alpha < 8 判为透明
}

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
  rtv_.Reset();
  offscreen_.Reset();
  staging_.Reset();

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

  // 目标 DIB:32 位 BGRA top-down,初始全品红 = 全透明
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width_;
  bi.bmiHeader.biHeight = -height_;  // top-down:行序与 D3D 一致
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  dib_.reset(CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dib_bits_, nullptr, 0));
  if (!dib_) throw RendererError{"CreateDIBSection failed"};
  // 填品红(BGRA: B=FF G=00 R=FF)
  unsigned char* p = static_cast<unsigned char*>(dib_bits_);
  for (int y = 0; y < height_; ++y) {
    unsigned char* row = p + static_cast<size_t>(y) * width_ * 4;
    for (int x = 0; x < width_; ++x) {
      row[x * 4 + 0] = 0xFF;  // B
      row[x * 4 + 1] = 0x00;  // G
      row[x * 4 + 2] = 0xFF;  // R
      row[x * 4 + 3] = 0x00;  // A(不用)
    }
  }
}

void Renderer::Shutdown() {
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

void Renderer::End() {
  // GPU → staging → 读回
  context_->CopyResource(staging_.Get(), offscreen_.Get());
  D3D11_MAPPED_SUBRESOURCE m{};
  if (SUCCEEDED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) {
    const unsigned char* src = static_cast<const unsigned char*>(m.pData);
    const size_t row_bytes = static_cast<size_t>(width_) * 4;
    unsigned char* dst = static_cast<unsigned char*>(dib_bits_);
    for (int y = 0; y < height_; ++y) {
      const unsigned char* s = src + static_cast<size_t>(y) * m.RowPitch;
      unsigned char* d = dst + static_cast<size_t>(y) * row_bytes;
      for (int x = 0; x < width_; ++x) {
        // 源:BGRA 预乘。alpha < 阈值 → 品红;否则反预乘。
        const unsigned char b = s[x * 4 + 0];
        const unsigned char g = s[x * 4 + 1];
        const unsigned char r = s[x * 4 + 2];
        const unsigned char a = s[x * 4 + 3];
        if (a < kAlphaThreshold) {
          d[x * 4 + 0] = 0xFF;  // B
          d[x * 4 + 1] = 0x00;  // G
          d[x * 4 + 2] = 0xFF;  // R → 品红 = 色键 = 透明
          d[x * 4 + 3] = 0x00;
        } else {
          d[x * 4 + 0] = static_cast<unsigned char>(b * 255 / a);  // 反预乘
          d[x * 4 + 1] = static_cast<unsigned char>(g * 255 / a);
          d[x * 4 + 2] = static_cast<unsigned char>(r * 255 / a);
          d[x * 4 + 3] = a;
        }
      }
    }
    context_->Unmap(staging_.Get(), 0);
  }

  // GDI 呈现到窗口 DC
  HDC dc = GetDC(hwnd_);
  if (dc) {
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, dib_.get());
    BitBlt(dc, 0, 0, width_, height_, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(hwnd_, dc);
  }
}

void Renderer::Resize(int width, int height) {
  width_ = width;
  height_ = height;
  CreateTargets();
}