#include "settings.h"

#include <fstream>
#include <map>
#include <string>

namespace {

// exe 所在目录(以 '\\' 结尾)。与 models.cpp / model_live2d.cpp 里的 ExeDir 语义一致,
// 单独一份用于设置文件路径解析。
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

constexpr char kCfgName[] = "desktopet.cfg";

// 键名约定:小写单词下划线。value 允许含空格,但不含 '='(模型目录名满足)。
struct State {
  bool loaded = false;
  std::map<std::string, std::string> kv;
};

State& S() {
  static State s;
  return s;
}

// 去行尾 CR/LF 与两端空白。
std::string Trim(const std::string& s) {
  std::string out = s;
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ' ||
                          out.back() == '\t'))
    out.pop_back();
  size_t i = 0;
  while (i < out.size() && (out[i] == ' ' || out[i] == '\t')) ++i;
  return out.substr(i);
}

// 首次访问时加载 cfg 到内存;之后所有读写走内存,避免每帧读盘。
void LoadOnce() {
  State& st = S();
  if (st.loaded) return;
  st.loaded = true;

  // Key=Value 行;空行/注释('#' 开头)/无 '=' 行跳过。
  std::ifstream f(ExeDir() + kCfgName);
  std::string line;
  while (std::getline(f, line)) {
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#') continue;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    st.kv[Trim(t.substr(0, eq))] = Trim(t.substr(eq + 1));
  }
}

// 全量写盘(按 key 排序)。未知键保留在内存映射里,不会因扩展格式而丢失。
void SaveNow() {
  const State& st = S();
  std::ofstream f(ExeDir() + kCfgName, std::ios::trunc);
  if (!f) return;
  for (const auto& kv : st.kv) f << kv.first << "=" << kv.second << "\n";
}

}  // namespace

std::string Settings::ModelName() {
  LoadOnce();
  const auto it = S().kv.find("model");
  return it == S().kv.end() ? std::string() : it->second;
}

void Settings::SetModelName(const std::string& name) {
  LoadOnce();
  S().kv["model"] = name;
  SaveNow();
}

bool Settings::MouseFollow() {
  LoadOnce();
  const auto it = S().kv.find("mouse_follow");
  return it != S().kv.end() && it->second == "1";
}

void Settings::SetMouseFollow(bool on) {
  LoadOnce();
  S().kv["mouse_follow"] = on ? "1" : "0";
  SaveNow();
}
