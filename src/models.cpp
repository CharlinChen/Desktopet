#include "models.h"

#include <algorithm>

namespace {

// exe 所在目录(以 '\\' 结尾)。与 model_live2d.cpp 里的 ExeDir 语义一致,
// 这里单独一份用于资源枚举(模型目录、assets 列表)。
std::string ExeDir() {
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring w(buf, n);
  const size_t pos = w.find_last_of(L"\\/");
  const std::wstring dir = (pos == std::wstring::npos) ? std::wstring() : w.substr(0, pos + 1);
  const int m = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()),
                                    nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(m), '\0');
  WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), static_cast<int>(dir.size()), &s[0], m, nullptr,
                      nullptr);
  return s;
}

std::wstring ToWide(const std::string& s) {
  if (s.empty()) return L"";
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

std::string ToUtf8(const std::wstring& w) {
  if (w.empty()) return "";
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

}  // namespace

std::vector<std::string> ListModels() {
  std::vector<std::string> out;
  const std::wstring assets = ToWide(ExeDir() + "assets");
  WIN32_FIND_DATAW fd{};
  HANDLE h = FindFirstFileW((assets + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (fd.cFileName[0] == L'.') continue;  // 跳过 . 与 ..
    WIN32_FIND_DATAW jfd{};
    HANDLE jh =
        FindFirstFileW((assets + L"\\" + fd.cFileName + L"\\*.model3.json").c_str(), &jfd);
    if (jh != INVALID_HANDLE_VALUE) {
      FindClose(jh);
      out.push_back(ToUtf8(fd.cFileName));
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end());
  return out;
}

std::string ModelDir(const std::string& name) { return ExeDir() + "assets\\" + name; }
