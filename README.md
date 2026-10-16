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

MSYS2 位于 `D:\ENV\msys64`(UCRT64:gcc 16 / cmake 4 / ninja / json-c)。

### 1. Live2D Cubism SDK 就位方式

`third_party/` 需要:

- `third_party/Framework/src/...`(Framework 源码,直接参与编译)
- `third_party/Core/include/Live2DCubismCore.h`
- `third_party/Core/dll/windows/x86_64/Live2DCubismCore.dll` + 由 gendef/dlltool 生成的 `libLive2DCubismCore.dll.a`(官方 .lib 是 MSVC 格式,MinGW 无法链接)

SDK 从 <https://www.live2d.com/en/download/cubism-sdk/download-native/> 获取(个人/小规模商用免费,见文末授权说明)。

### 2. 编译

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=release -DLIVE2D_REAL=ON
cmake --build build
```

(`build.bat` 亦可用:自动检测 third_party 就位并启用 `LIVE2D_REAL`。LIVE2D_REAL=OFF 时编译占位方块模型。)

产物:`build/bin/desktopet.exe`,旁边自动带上 `Live2DCubismCore.dll`、`FrameworkShaders/` 与 `assets/`——所有资源按 exe 所在目录解析,双击直接可跑。

## 运行

直接运行 exe:透明置顶窗口趴在屏幕底边,Haru 沿边爬行/发呆。点击宠物触发 Happy(张嘴),Esc 退出(调试阶段)。

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
third_party/         Live2D SDK(Framework 源码 + Core 头/dll + MinGW 补丁)
patches/             third_party 内 SDK 源码的补丁(着色器磁盘缓存,SDK 更新后需重新 apply)
assets/              Haru 模型资源(model3.json/moc3/贴图/motion/物理)
CMakeLists.txt
build.bat
```

## 设计决策

- **透明窗口 = LWA_COLORKEY 色键**。本机(双屏 + NVIDIA 4060 Laptop)实测:`DXGI_ALPHA_MODE_PREMULTIPLIED` 交换链返回 DXGI_ERROR_INVALID_CALL、UpdateLayeredWindow 返回 ERROR_GEN_FAILURE,只有色键可用。管线为:D3D11 离屏预乘渲染 → staging 读回 → CPU 逐像素(alpha<8 → 品红,否则反预乘)→ GDI BitBlt。自 Win8 起稳定,且兼容性最好。
- **预乘 alpha 混合**(`D3D11_BLEND_ONE` / `INV_SRC_ALPHA`),Live2D 贴图(WIC 32bppPRGBA)预乘,渲染器读回时反预乘给 GDI。
- **为什么不用 Electron/Tauri**:WebView2 进程基础开销 ~150MB,不满足轻量目标。
- **为什么不用 Rust**:Live2D 无官方 Rust 绑定,FFI 封装成本高。
- **MinGW × Live2D 补丁**(third_party 内):DirectXMath 头路径、XMMATRIX 结构体适配、常量缓冲 16 字节对齐(MSYS2 DirectXMath 的 XMFLOAT4X4 对齐为 4,D3D11 ByteWidth 必须 16 对齐)。
- **着色器磁盘缓存**:首次启动把 Framework 运行时编译的 DXBC 缓存到 exe 旁 `ShaderCache/`,之后跳过 ~500 次 `D3DCompile` 秒开。SDK 源码补丁在 `patches/`,SDK 更新后 `git apply patches/*.patch`。

## 风险点 / 待办

- **跨 DPI 缩放**:窗口坐标目前按逻辑像素处理,多显示器混合 DPI 时需处理 `WM_DPICHANGED`(后续)。
- **贴边行为**:目前固定贴屏幕底边,换边只动 x;顶/左/右贴边待完善。
- **托盘/自启/单例**:未实现,后续按需添加。
- **内存目标 <60MB 验证**:未实测,后续用任务管理器核对。

## 授权说明

Live2D Cubism SDK 非 MIT:个人/小规模商用免费,收入超阈值需商业许可。公开发布前请核对 <https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html>。
