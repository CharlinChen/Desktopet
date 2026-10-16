#pragma once
// 模型抽象。两个实现:
//   - StubModel(无 SDK 时的占位:跳动方块,验证 窗口透明 → 渲染 → 行为 链路)
//   - Live2DModel(真实 SDK,见 model_live2d.h)
#include <d3d11.h>
#include <memory>
#include <string>

struct ModelMeta {
  int canvas_w = 400;   // 模型画布宽(像素)
  int canvas_h = 400;   // 模型画布高
  float anchor_x = 0.5f;  // 模型锚点在画布中的归一化位置(用于窗口贴地/贴边)
  float anchor_y = 1.0f;  // 默认脚底在画布底部
};

// 模型加载失败(供 main 捕获并弹错误框)
struct ModelLoadError {
  const char* msg;
};

class Model {
 public:
  virtual ~Model() = default;
  // 注入渲染设备(主窗口创建的 D3D 设备)
  virtual void SetDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) = 0;
  // 从目录加载(目录下应有 model3.json / 对应资源)。失败抛 ModelLoadError{msg}
  virtual void Load(const std::string& dir) = 0;
  // 更新参数(表达式、朝向、眨眼等),dt 为秒
  virtual void Update(float dt) = 0;
  // 渲染到当前 RTV(需在 Renderer::Begin/End 之间)
  virtual void Draw() = 0;

  const ModelMeta& Meta() const { return meta_; }

 protected:
  ModelMeta meta_;
};

// 工厂:根据编译期 HAVE_LIVE2D_SDK 选择实现
std::unique_ptr<Model> CreateModel();
