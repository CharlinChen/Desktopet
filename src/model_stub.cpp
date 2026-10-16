#include "model.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>

#include "com_ptr.h"

// 占位模型:画一个 64x64 的预乘 alpha 方块,以 2Hz 频率上下跳动。
// 目的:在没有 Live2D SDK 时验证 透明分层窗口 → D3D11 渲染 → 鼠标交互 整条链路。
// 真实 SDK 就位后本文件被 model_live2d.cpp 取代。
class StubModel final : public Model {
 public:
  void SetDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) override {
    dev_ = dev;
    ctx_ = ctx;
    CreatePipeline();
  }
  void Load(const std::string&) override {}
  void Update(float dt) override { t_ += dt; }
  void Draw() override;

 private:
  void CreatePipeline();

  ID3D11Device* dev_ = nullptr;
  ID3D11DeviceContext* ctx_ = nullptr;
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11InputLayout> il_;
  ComPtr<ID3D11Buffer> vb_;
  ComPtr<ID3D11Buffer> cb_;
  float t_ = 0.f;
};

namespace {
const char* kVS =
    "struct VSIn { float2 pos : POSITION; };\n"
    "cbuffer CB : register(b0) { float2 viewport; float2 pad; };\n"
    "float4 main(VSIn i) : SV_POSITION {\n"
    "  float2 ndc = float2(i.pos.x / viewport.x, 1.0 - i.pos.y / viewport.y) * 2.0 - 1.0;\n"
    "  return float4(ndc, 0.5, 1.0);\n"
    "}\n";
const char* kPS =
    "struct PSIn { float4 pos : SV_POSITION; };\n"
    "cbuffer CB : register(b0) { float2 viewport; float2 pad; float4 color; };\n"
    "float4 main(PSIn i) : SV_TARGET { return color; }\n";
}  // namespace

void StubModel::CreatePipeline() {
  if (!dev_) return;
  ComPtr<ID3DBlob> vs_blob, ps_blob, err;

  D3DCompile(kVS, strlen(kVS), "vs", nullptr, nullptr, "main", "vs_4_0", 0, 0, vs_blob.GetAddressOf(), err.GetAddressOf());
  dev_->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vs_.GetAddressOf());

  D3DCompile(kPS, strlen(kPS), "ps", nullptr, nullptr, "main", "ps_4_0", 0, 0, ps_blob.GetAddressOf(), err.GetAddressOf());
  dev_->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, ps_.GetAddressOf());

  // 输入布局必须用顶点着色器的签名,不能用像素着色器的 blob
  D3D11_INPUT_ELEMENT_DESC ie[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  dev_->CreateInputLayout(ie, 1, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), il_.GetAddressOf());

  D3D11_BUFFER_DESC bd{};
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.ByteWidth = 4 * 2 * sizeof(float);  // 4 个顶点 × (x, y)
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  dev_->CreateBuffer(&bd, nullptr, vb_.GetAddressOf());

  D3D11_BUFFER_DESC cd{};
  cd.Usage = D3D11_USAGE_DEFAULT;
  cd.ByteWidth = 3 * 16;  // viewport(pad) + color,各 16 字节对齐
  cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  dev_->CreateBuffer(&cd, nullptr, cb_.GetAddressOf());
}

void StubModel::Draw() {
  if (!ctx_) return;
  const float S = 64.f;
  const float bounce = 20.f * (0.5f + 0.5f * sinf(t_ * 3.14159f * 2.f));  // 2Hz
  const float cx = meta_.canvas_w * 0.5f, cy = meta_.canvas_h * 0.5f + bounce;
  const float x0 = cx - S / 2, y0 = cy - S / 2;

  const float verts[8] = {x0, y0, x0 + S, y0, x0, y0 + S, x0 + S, y0 + S};
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(ctx_->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    memcpy(m.pData, verts, sizeof verts);
    ctx_->Unmap(vb_.Get(), 0);
  }

  const float cb_data[12] = {
      static_cast<float>(meta_.canvas_w), static_cast<float>(meta_.canvas_h), 0.f, 0.f,
      0.17f, 0.75f, 0.85f, 0.9f, 0.f, 0.f, 0.f, 0.f};  // 直通 alpha,由 ULW 合成
  ctx_->UpdateSubresource(cb_.Get(), 0, nullptr, cb_data, 0, 0);

  ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  ctx_->IASetInputLayout(il_.Get());
  UINT stride = 2 * sizeof(float), off = 0;
  // 注意:绑定用 .Get(),不能用 .GetAddressOf()(后者会先 Release)
  ID3D11Buffer* vb = vb_.Get();
  ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &off);
  ctx_->VSSetShader(vs_.Get(), nullptr, 0);
  ctx_->PSSetShader(ps_.Get(), nullptr, 0);
  ID3D11Buffer* cb_bind = cb_.Get();
  ctx_->VSSetConstantBuffers(0, 1, &cb_bind);
  ctx_->PSSetConstantBuffers(0, 1, &cb_bind);
  ctx_->Draw(4, 0);
}

std::unique_ptr<Model> CreateModel() { return std::make_unique<StubModel>(); }
