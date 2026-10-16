#pragma once
// Live2D 模型实现(真实 Cubism SDK)。接口见 model.h。
// 依赖 third_party/{Framework,Core}(CubismSdkForNative-5-r.5)。
// 说明:
//   - 窗口 400x400,交换链预乘 alpha;SDK 用预乘着色器 + WIC 加载时预乘贴图,与窗口方案一致
//   - 本类只在元信息暴露一个"贴边缩放"参数给窗口层;模型完整绘制流程(相机/运动/物理/呼吸/眨眼)全部内部闭环
#include "model.h"

struct Behavior;

// 加载/绘制失败的错误输出(工程目录下 live2d_error.log)
void LogLive2dError(const char* what, const char* msg);

// 着色器字节码缓存是否尚未就绪(需要首次编译)。供 main 决定是否显示启动提示。
bool Live2DShaderCacheNeedsCompile();

class Live2DModel final : public Model {
 public:
  ~Live2DModel() override;

  void SetDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx) override;
  void Load(const std::string& dir) override;
  void Update(float dt) override;
  void Draw() override;

  // 由 main 注入行为状态机(参数驱动:朝向/移动/兴奋)
  void SetBehavior(Behavior* behavior) { b_ = behavior; }

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  Behavior* b_ = nullptr;
};
