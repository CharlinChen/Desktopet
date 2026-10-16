#pragma once
// MinGW 没有 Microsoft::WRL,提供一个最小 ComPtr 替代。
// 仅覆盖本项目用到的成员。
template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(T* p) : p_(p) {}
  ~ComPtr() { Reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  ComPtr& operator=(ComPtr&& o) noexcept {
    if (this != &o) {
      Reset();
      p_ = o.p_;
      o.p_ = nullptr;
    }
    return *this;
  }

  T* Get() const { return p_; }
  T* operator->() const { return p_; }
  operator bool() const { return p_ != nullptr; }
  T** GetAddressOf() {
    Reset();
    return &p_;
  }
  // 不释放已持对象,直接暴露内部指针地址
  // (传 T** 给 COM 函数时需要;若 COM 直接写 &p_ 而 p_ 非空会泄漏)
  T** InPlaceAddressOf() { return &p_; }
  void** VoidAddressOf() { return reinterpret_cast<void**>(&p_); }
  T** ReleaseAndGetAddressOf() {
    Reset();
    return &p_;
  }
  void Reset() {
    if (p_) p_->Release();
    p_ = nullptr;
  }

 private:
  T* p_ = nullptr;
};
