#pragma once
// 桌宠行为状态机:
//   Idle(发呆) → Walk(沿边爬) → 随机进入 Idle → 被点击 → Happy(抖一下) → 继续
// 纯逻辑,不依赖窗口/渲染,方便单独测试。
struct Behavior {
  enum class State { Idle, Walk, Happy };

  // 边缘类型:沿哪条边爬
  enum class Edge { Bottom, Top, Left, Right };

  State state = State::Idle;
  Edge edge = Edge::Bottom;
  float dir = 1.f;          // 爬行方向(±1)
  float speed = 30.f;       // 爬行速度 px/s
  float time_in_state = 0.f;

  // 外部可见的动画参数(模型 Update 用)
  bool facing_left = false;    // 面朝左
  bool is_moving = false;      // 正在爬行
  float eye_blink = 0.f;       // 0~1,眨眼相位
  float happiness = 0.f;       // 0~1,点击后的兴奋值,随时间衰减
  bool follow_mouse = false;   // 鼠标追随开关(托盘「设置」项,main 每帧同步)
  float look_x = 0.f;          // 归一化 [-1,1]:光标相对宠物脸中心的水平偏移(右正)
  float look_y = 0.f;          // 归一化 [-1,1]:垂直偏移(上正)

  float x = 0.f, y = 0.f;      // 当前位置(窗口左上角,屏幕坐标)

  // main 注入显示器工作区边界(在 Update 之前调用)
  void SetBounds(float min_x, float max_x);
  // 走到尽头换到哪条边:底→左→顶→右 轮换
  void NextEdge();
  // 清零状态时钟(首帧大 DT 防护)
  void ResetClock() { time_in_state = 0.f; }

  // 每帧推进;返回新位置。dt 秒。
  void Update(float dt);

  // 鼠标点击时调用
  void OnClick();

  // 随机切换状态(发呆 ↔ 爬行)。rng01: [0,1) 随机数
  void MaybeSwitch(float rng01);

 private:
  float bound_min_ = 0.f, bound_max_ = 1920.f;
};
