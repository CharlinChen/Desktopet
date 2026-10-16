#include "autostart.h"

#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"Desktopet";

// 可执行文件全路径,两侧加引号(路径可能含空格)。失败返回空串。
std::wstring ExePathQuoted() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return L"";
  return L"\"" + std::wstring(buf, n) + L"\"";
}

}  // namespace

bool AutoStart::IsEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    return false;
  const LONG r = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(key);
  return r == ERROR_SUCCESS;  // 值存在即视为已启用(含类型异常,但罕见)
}

bool AutoStart::SetEnabled(bool on) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS)
    return false;

  LONG r = ERROR_SUCCESS;
  if (on) {
    const std::wstring path = ExePathQuoted();
    if (path.empty()) {
      RegCloseKey(key);
      return false;
    }
    r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(path.c_str()),
                       static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
  } else {
    r = RegDeleteValueW(key, kValueName);  // 不存在返回 ERROR_FILE_NOT_FOUND,按已取消处理
    if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
  }

  RegCloseKey(key);
  return r == ERROR_SUCCESS;
}
