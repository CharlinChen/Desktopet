#pragma once
// 开机自启:读写 HKCU\...\Run 注册表值。取消 = 删除该值(可逆)。
namespace AutoStart {
// 当前是否已注册开机自启(值存在即视为启用)。
bool IsEnabled();
// 设置开机自启开关;返回设置后的实际状态,失败返回 false。
bool SetEnabled(bool on);
}  // namespace AutoStart
