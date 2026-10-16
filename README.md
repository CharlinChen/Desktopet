# Desktopet

极轻量 Windows 桌宠:沿屏幕边缘爬行的 Live2D 形象。C++ + Win32 分层窗口 + D3D11 + Live2D Cubism SDK for Native。目标内存占用 < 60MB。

## 当前状态

- [x] 透明置顶窗口(逐像素透明 + 鼠标穿透切换)
- [x] D3D11 渲染器(预乘 alpha 混合)
- [x] 行为状态机(沿边爬行/发呆/点击互动)
- [x] **Live2D 模型渲染**(Haru 已上屏并沿屏幕底边爬行,用户肉眼验证通过)
- [x] 占位模型(青色跳动方块,无 SDK 时验证链路)

## 构建

### 0. 一次性:准备工具链

需要 MSYS2(UCRT64 环境),安装:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-json-c
```

(要求 gcc 16 / cmake 4 / ninja / json-c。)

### 1. Live2D Cubism SDK 就位方式

`third_party/` 需要:

- `third_party/Framework/src/...`(Framework 源码,直接参与编译)
- `third_party/Core/include/Live2DCubismCore.h`
- `third_party/Core/dll/windows/x86_64/Live2DCubismCore.dll` + 由 gendef/dlltool 生成的 `libLive2DCubismCore.dll.a`(官方 .lib 是 MSVC 格式,MinGW 无法链接)

SDK 从 <https://www.live2d.com/en/download/cubism-sdk/download-native/> 获取(个人/小规模商用免费,见文末授权说明)。

### 2. 模型资源就位

本仓库不含模型资源。把 Live2D 官方示例模型 **Haru** 放入 `assets/Haru/`(含 `Haru.model3.json`、`Haru.moc3`、贴图、`motions/`、`expressions/` 等)。SDK 的 `Samples/Resources/Haru` 目录可直接整个复制过去。

### 3. 编译

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=release -DLIVE2D_REAL=ON
cmake --build build
```

(`build.bat` 亦可用,见下文。LIVE2D_REAL=OFF 时编译占位方块模型。)

产物:`build/bin/desktopet.exe`,旁边自动带上 `Live2DCubismCore.dll`、`FrameworkShaders/` 与 `assets/`——所有资源按 exe 所在目录解析,双击直接可跑。

#### build.bat(可选)

`build.bat` 是 Windows 一键构建脚本,需要知道 MSYS2 安装位置。默认从环境变量 `MSYS2_ROOT` 读取,未设置时用 `D:\msys64`;也可直接改脚本顶部。用法:

```
set MSYS2_ROOT=C:\path\to\msys64
build.bat
```

## 运行

直接运行 exe:透明置顶窗口趴在屏幕底边,Haru 沿边爬行/发呆。点击宠物触发 Happy(张嘴)。**关闭方式:右下角系统托盘图标右键 →「退出」**(Esc 为调试阶段的临时退出,仅在窗口有焦点时可用)。托盘菜单同时提供「开机自启」开关,再次点击即可取消(写入/删除 `HKCU\...\Run\Desktopet`)。

## 目录结构

```
src/
  main.cpp           主程序(工作区检测/回调装配/COM 初始化)
  window.h/.cpp      Win32 分层透明窗口(色键透明,见设计决策)
  renderer.h/.cpp    D3D11 离屏渲染器(预乘 → 读回 → 反预乘 → GDI BitBlt)
  model.h           模型抽象
  model_live2d.*    Live2D 模型(Cubism Framework 闭环:模型/贴图/运动/物理/呼吸/眨眼)
  model_stub.cpp    占位方块模型(LIVE2D_REAL=OFF)
  behavior.h/.cpp   行为状态机
  splash.h/.cpp     首次启动"正在编译着色器"提示窗口(独立不透明,编译完关闭)
  tray.h/.cpp       系统托盘图标 + 右键「退出」/「开机自启」菜单(正式关闭通道)
  autostart.h/.cpp  开机自启(HKCU\...\Run 注册表值,可取消)
  singleton.h       单实例锁(命名互斥体,重复启动静默退出)
third_party/         Live2D SDK(Framework 源码 + Core 头/dll + MinGW 补丁)
patches/             third_party 内 SDK 源码的补丁(着色器磁盘缓存,SDK 更新后需重新 apply)
assets/              模型资源(不随仓库分发,需从 SDK 自行放入,见「模型资源就位」)
CMakeLists.txt
build.bat
```

## 设计决策

- **透明窗口 = LWA_COLORKEY 色键**。开发环境实测:`DXGI_ALPHA_MODE_PREMULTIPLIED` 交换链返回 DXGI_ERROR_INVALID_CALL、UpdateLayeredWindow 返回 ERROR_GEN_FAILURE,只有色键可用。管线为:D3D11 离屏预乘渲染 → staging 读回 → CPU 逐像素(alpha<8 → 品红,否则反预乘)→ GDI BitBlt。自 Win8 起稳定,且兼容性最好。
- **预乘 alpha 混合**(`D3D11_BLEND_ONE` / `INV_SRC_ALPHA`),Live2D 贴图(WIC 32bppPRGBA)预乘,渲染器读回时反预乘给 GDI。
- **为什么不用 Electron/Tauri**:WebView2 进程基础开销 ~150MB,不满足轻量目标。
- **为什么不用 Rust**:Live2D 无官方 Rust 绑定,FFI 封装成本高。
- **MinGW × Live2D 补丁**(third_party 内):DirectXMath 头路径、XMMATRIX 结构体适配、常量缓冲 16 字节对齐(MSYS2 DirectXMath 的 XMFLOAT4X4 对齐为 4,D3D11 ByteWidth 必须 16 对齐)。
- **着色器磁盘缓存**:首次启动把 Framework 运行时编译的 DXBC 缓存到 exe 旁 `ShaderCache/`,之后跳过 ~500 次 `D3DCompile` 秒开。SDK 源码补丁在 `patches/`,SDK 更新后 `git apply patches/*.patch`。

## 风险点 / 待办

- **跨 DPI 缩放**:窗口坐标目前按逻辑像素处理,多显示器混合 DPI 时需处理 `WM_DPICHANGED`(后续)。
- **贴边行为**:目前固定贴屏幕底边,换边只动 x;顶/左/右贴边待完善。
- **托盘/自启/单例**:托盘「右键退出」、开机自启(可取消)、单实例锁均已实现。
- **内存目标 <60MB 验证**:未实测,后续用任务管理器核对。

## 授权说明

- **本项目代码**:MIT 许可(见 `LICENSE`)。
- **Live2D Cubism SDK**:非 MIT,个人/小规模商用免费,收入超阈值需商业许可。公开发布前请核对 <https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html>。
- **模型资源(如 Haru)**:属于 Live2D 官方示例资源,遵循其各自条款,本仓库不随源码再分发。
