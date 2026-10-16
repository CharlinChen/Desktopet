@echo off
rem 一键构建脚本:自动检测 Live2D SDK 是否就位
rem 用法:build.bat [debug|release]
rem MSYS2 位置从环境变量 MSYS2_ROOT 读取,未设置则用默认 D:\msys64。
rem   set MSYS2_ROOT=C:\path\to\msys64 && build.bat
setlocal

if "%MSYS2_ROOT%"=="" set MSYS2_ROOT=D:\msys64
set BUILD_DIR=build

set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=release

rem 工具链路径(相对 MSYS2 根)
set PATH=%MSYS2_ROOT%\ucrt64\bin;%PATH%

rem 检测 Live2D SDK
set LIVE2D_ARGS=
if exist third_party\Core\include\Live2DCubismCore.h (
  if exist third_party\Framework (
    echo [build] Live2D SDK detected, enabling real rendering
    set LIVE2D_ARGS=-DLIVE2D_REAL=ON
  )
)

cmake -S . -B %BUILD_DIR% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% %LIVE2D_ARGS%
if errorlevel 1 exit /b 1

cmake --build %BUILD_DIR%
if errorlevel 1 exit /b 1

echo.
echo [build] OK - binary at %BUILD_DIR%\bin\desktopet.exe
endlocal
