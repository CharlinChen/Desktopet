#pragma once
// 用户设置:所有持久化项统一存 exe 旁 desktopet.cfg(Key=Value 纯文本行)。
// 之前 model_select.cfg / mouse_follow.cfg 是单值文件,每加一项就多一个文件;
// 现在收敛到一份 cfg,后面加设置只需新增 getter/setter,不再增文件。
#include <windows.h>

#include <string>

namespace Settings {

// 当前选中的模型目录名。空串 = 未选择过(用列表首项兜底)。
std::string ModelName();
void SetModelName(const std::string& name);

// 鼠标追随开关(眼睛看向光标)。默认关闭。
bool MouseFollow();
void SetMouseFollow(bool on);

}  // namespace Settings

// 托盘切换「鼠标追随」后投递到主窗口的消息(主窗口据此翻转内存态,避免每帧读盘)。
constexpr UINT kMouseFollowToggleMsg = WM_APP + 3;
