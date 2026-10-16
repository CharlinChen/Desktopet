#pragma once
// 单实例锁:命名互斥体,进程整个生命周期持有。
// 第二个实例启动时检测到互斥体已存在,静默退出。
#include <windows.h>

class Singleton {
 public:
  // 尝试获取单实例锁。返回 true = 本进程是唯一实例,可继续;
  // false = 已有实例在运行。创建失败时保守放行(不因锁问题阻断启动)。
  bool TryAcquire(const wchar_t* name) {
    mutex_ = CreateMutexW(nullptr, TRUE, name);
    if (!mutex_) return true;  // 创建失败:fail-open
    return GetLastError() != ERROR_ALREADY_EXISTS;
  }

  ~Singleton() {
    if (mutex_) CloseHandle(mutex_);
  }

 private:
  HANDLE mutex_ = nullptr;
};
