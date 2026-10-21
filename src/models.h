#pragma once
// 模型目录枚举与托盘切换。托盘菜单与 main 共用同一份有序列表(下标 = 菜单项)。
#include <windows.h>

#include <string>
#include <vector>

// 模型切换消息:托盘菜单选中后投递到主窗口(WPARAM = 模型在 ListModels() 中的下标)。
constexpr UINT kModelSwitchMsg = WM_APP + 2;

// 枚举 exe 旁 assets/ 下所有含 *.model3.json 的模型目录名(升序)。
std::vector<std::string> ListModels();
// 枚举 exe 旁 assets/ 下旧版 Cubism 2.x 格式(*.model.json 且无 *.model3.json)的模型目录名(升序)。
// 这类模型本程序无法加载,仅列在托盘菜单里;点击时提示用户用官方转换器转 3.0。
std::vector<std::string> ListLegacyModels();
// 模型目录绝对路径(exe 旁 assets/<name>,无尾斜杠)。
std::string ModelDir(const std::string& name);
