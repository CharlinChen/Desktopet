#pragma once
// UTF-8 路径 → Windows 宽字符文件 API 的统一入口。
//
// 背景:exe 目录含中文时,ExeDir()/ResolvePath() 产出 UTF-8 字节串,但
// std::ifstream/ofstream 与 fopen 走 ANSI 代码页(中文系统是 GBK)解释路径,
// 导致 model3.json/moc3/贴图/cfg/日志全部打开失败。表现就是「双击咚一声
// 就退出、无任何日志」(连日志 fopen 都写不进中文目录)。
//
// 统一改用 _wfopen(宽字符)才能正确打开中文路径。WIC 贴图加载用的
// CreateDecoderFromFilename 已经是宽字符路径,所以唯独窄字符文件 API 需要替换。
#include <cstdio>
#include <string>
#include <wchar.h>
#include <windows.h>

namespace utf8 {

inline std::wstring ToWide(const std::string& s) {
  if (s.empty()) return L"";
  const int n =
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  if (n > 0)
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

// 以宽字符打开 UTF-8 路径的文件。mode 用 ANSI 风格 "rb"/"a"/"wt" 等,
// 内部转成宽字符交给 _wfopen。
inline FILE* Fopen(const std::string& utf8_path, const char* mode) {
  std::wstring wm;
  for (const char* p = mode; *p; ++p)
    wm.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
  return _wfopen(ToWide(utf8_path).c_str(), wm.c_str());
}

}  // namespace utf8
