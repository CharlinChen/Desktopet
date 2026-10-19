#include "behavior.h"

#include <algorithm>
#include <cmath>

void Behavior::Update(float dt) {
  time_in_state += dt;
  happiness = std::max(0.f, happiness - dt * 0.8f);  // 兴奋约 1.25s 衰减完
  eye_blink += dt * 2.0f;                            // 约 3s 一次眨眼循环(模型层取相位)

  switch (state) {
    case State::Idle:
      is_moving = false;
      // 静止模式:不随机爬行,留在 Idle(点击 Happy 结束后的落点也是这里)。
      if (!wander) break;
      if (time_in_state > 2.5f) state = State::Walk, time_in_state = 0.f;
      break;

    case State::Walk: {
      is_moving = true;
      x += dir * speed * dt;
      // 到达边缘尽头:换边。边界由 main 根据显示器工作区注入(见 SetBounds)
      if (x < bound_min_ || x > bound_max_) {
        x = std::clamp(x, bound_min_, bound_max_);
        dir = -dir;
        facing_left = dir < 0.f;
        NextEdge();
      }
      if (time_in_state > 6.f) state = State::Idle, time_in_state = 0.f;
      break;
    }

    case State::Happy:
      is_moving = false;
      if (time_in_state > 0.8f) state = State::Idle, time_in_state = 0.f;
      break;
  }
}

void Behavior::OnClick() {
  happiness = 1.f;
  state = State::Happy;
  time_in_state = 0.f;
}

void Behavior::MaybeSwitch(float rng01) {
  if (!wander) return;  // 静止模式:不随机开始爬
  // Idle 下随机开始爬(每帧以低概率触发,约平均 2s 一次)
  if (state == State::Idle && rng01 < 0.02f) {
    state = State::Walk;
    time_in_state = 0.f;
    dir = (rng01 * 100.f < 50.f) ? -1.f : 1.f;  // 复用 rng 决定初始方向
    facing_left = dir < 0.f;
  }
}

// main 注入工作区边界后调用(在 Update 之前)
void Behavior::SetBounds(float min_x, float max_x) {
  bound_min_ = min_x;
  bound_max_ = max_x;
  x = std::clamp(x, bound_min_, bound_max_);
}

// 走到尽头换到哪条边:底→左→顶→右 轮换(后续可做成随机)
void Behavior::NextEdge() {
  switch (edge) {
    case Edge::Bottom: edge = Edge::Left; break;
    case Edge::Left: edge = Edge::Top; break;
    case Edge::Top: edge = Edge::Right; break;
    case Edge::Right: edge = Edge::Bottom; break;
  }
}
