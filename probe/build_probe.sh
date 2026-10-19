#!/usr/bin/env bash
# 编译独立透明探针(不进主 CMake,单独一个 exe)。
# 依赖:d3d11 / dxgi / dcomp / dwmapi(MSYS2 UCRT64 自带)。
set -e
UCRT="/d/ENV/msys64/ucrt64"
export PATH="$UCRT/bin:/d/ENV/msys64/usr/bin:$PATH"
cd "$(dirname "$0")"

mkdir -p ../build/probe

"$UCRT/bin/g++" -std=c++17 -O2 -Wall -Wextra \
  -I"$UCRT/include" \
  alpha_probe.cpp \
  -L"$UCRT/lib" \
  -ld3d11 -ldxgi -ldcomp -ldwmapi -lgdi32 -lole32 -luuid -static \
  -mwindows \
  -o ../build/probe/alpha_probe.exe

echo "OK -> build/probe/alpha_probe.exe"
