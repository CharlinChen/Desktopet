cmake_minimum_required(VERSION 3.16)
project(desktopet CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出到 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- 目标:先建可执行文件 ----
# WIN32 子系统:双击运行不弹 cmd 黑窗(否则 Explorer 会给控制台进程分配一个控制台)
add_executable(desktopet WIN32
  src/main.cpp
  src/window.h
  src/window.cpp
  src/renderer.h
  src/renderer.cpp
  src/model.h
  src/models.h
  src/models.cpp
  src/behavior.h
  src/behavior.cpp
  src/splash.h
  src/splash.cpp
  src/tray.h
  src/tray.cpp
  src/autostart.h
  src/autostart.cpp
  src/settings.h
  src/settings.cpp
  src/singleton.h
)

# ---- Live2D SDK 配置(通过 -D 传入)----
# LIVE2D_SDK_DIR: CubismSdkForNative-* 目录(Framework + Samples)
# LIVE2D_CORE_DIR: CubismSdkForNative-Core 目录(Core/include + Core/lib)
set(LIVE2D_SDK_DIR "" CACHE PATH "Cubism SDK for Native 根目录")
set(LIVE2D_CORE_DIR "" CACHE PATH "Cubism Core 目录")
set(LIVE2D_REAL OFF CACHE BOOL "使用真实 Live2D 渲染(需要 SDK)")

# LIVE2D_SDK_DIR / LIVE2D_CORE_DIR 保留为可选覆盖(默认使用仓库内 third_party/)

if(LIVE2D_REAL)
  add_compile_definitions(HAVE_LIVE2D_SDK)
  add_subdirectory(third_party)
  target_sources(desktopet PRIVATE src/model_live2d.cpp src/model_live2d.h)
  target_link_libraries(desktopet PRIVATE Framework cubism_core windowscodecs)
  # Core 是动态链接:dll 复制到 exe 旁边
  add_custom_command(TARGET desktopet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Core/dll/windows/x86_64/Live2DCubismCore.dll
      $<TARGET_FILE_DIR:desktopet>/Live2DCubismCore.dll)
  # Framework 的 D3D11 着色器在运行时按路径读取(exe 旁边 FrameworkShaders/)
  add_custom_command(TARGET desktopet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Framework/src/Rendering/D3D11/Shaders
      $<TARGET_FILE_DIR:desktopet>/FrameworkShaders)
  # 模型资源也复制到 exe 旁边(路径按 exe 目录解析,与工作目录无关)
  add_custom_command(TARGET desktopet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/assets
      $<TARGET_FILE_DIR:desktopet>/assets)
else()
  message(STATUS "LIVE2D 使用占位渲染(STUB)。下载 SDK 后 build.bat 会自动切换为真实渲染。")
  target_sources(desktopet PRIVATE src/model_stub.cpp)
endif()

# ---- json-c (MSYS2: pacman -S mingw-w64-ucrt-x86_64-json-c) ----
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(JSONC QUIET json-c)
endif()
if(JSONC_FOUND)
  target_link_libraries(desktopet PRIVATE ${JSONC_LIBRARIES})
  target_include_directories(desktopet PRIVATE ${JSONC_INCLUDE_DIRS})
else()
  find_path(JSONC_INCLUDE json.h PATH_SUFFIXES json-c)
  find_library(JSONC_LIB NAMES json-c libjson-c)
  if(JSONC_INCLUDE AND JSONC_LIB)
    target_include_directories(desktopet PRIVATE ${JSONC_INCLUDE})
    target_link_libraries(desktopet PRIVATE ${JSONC_LIB})
  else()
    message(FATAL_ERROR "未找到 json-c。请在 MSYS2 中执行: pacman -S mingw-w64-ucrt-x86_64-json-c")
  endif()
endif()

# ---- D3D11 ----
target_link_libraries(desktopet PRIVATE d3d11 d3dcompiler)

# 宽字符入口
target_compile_definitions(desktopet PRIVATE UNICODE _UNICODE)

if(MSVC)
  target_compile_options(desktopet PRIVATE /W3)
else()
  target_compile_options(desktopet PRIVATE -Wall -Wextra -O2)
  # 静态链接 MinGW 运行库:否则双击 exe 会报缺 libgcc_s_seh-1.dll / libstdc++-6.dll。
  # 全静态(-static,含 UCRT):g++ 驱动会在所有参数末尾自动追加 -lwinpthread,
  # 局部 -Bstatic 挡不住,只有整体 -static 才把它一并静态进来。
  target_link_options(desktopet PRIVATE -static)
endif()
