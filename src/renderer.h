#pragma once
// D3D11 渲染器:离屏渲染(预乘 alpha)→ 读回 → UpdateLayeredWindow 逐像素合成。
// (2026-10-19 探针实测本机 ULW 可用,色键方案已作废,见 renderer.cpp 头注释)
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>

#include <memory>

#include "com_ptr.h"

// 渲染器初始化失败时抛出(消息为静态字符串)
struct RendererError {
  const char* msg;
};

class Renderer {
 public:
  // 初始化设备与离屏缓冲,失败抛 RendererError
  void Init(HWND hwnd, int width, int height);
  // 释放资源
  void Shutdown();

  // 清屏为全透明(alpha=0)。模型绘制必须在 Begin/End 之间发生
  void Begin();
  // 读回并 UpdateLayeredWindow 呈现
  void End();

  // 窗口尺寸变化时调用(重建离屏缓冲)
  void Resize(int width, int height);

  ID3D11Device* Device() const { return device_.Get(); }
  ID3D11DeviceContext* Context() const { return context_.Get(); }

 private:
  void CreateTargets();
  void PresentLayered();

  HWND hwnd_ = nullptr;
  int width_ = 0;
  int height_ = 0;

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11Texture2D> offscreen_;  // 渲染目标
  ComPtr<ID3D11Texture2D> staging_;    // 回读缓冲
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11BlendState> bs_premult_;  // 预乘 alpha:src + dst*(1-src.a)

  // 逐像素 alpha 呈现:预乘 DIB + 常驻内存 DC,End() 用 UpdateLayeredWindow 合成。
  std::unique_ptr<std::remove_pointer_t<HBITMAP>, decltype(&DeleteObject)> dib_{nullptr, &DeleteObject};
  void* dib_bits_ = nullptr;
  HDC mem_dc_ = nullptr;  // DIB 需 SelectObject 进 DC,常驻整个生命周期
};
