# CLAUDE.md — Desktopet 桌宠

Windows 极轻量桌宠:C++ + Win32 分层窗口 + D3D11 + Live2D Cubism Native SDK。目标内存 < 60MB。

## 构建环境

- **工具链**:MSYS2 UCRT64(`D:\ENV\msys64`),gcc 16 / cmake 4 / ninja / json-c
- **构建**:`build.bat`(自动检测 SDK 切换 stub/real);或 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=release -DLIVE2D_REAL=ON && cmake --build build`
- **产物**:`build/bin/desktopet.exe`(资源 dll/FrameworkShaders/assets 由 CMake 自动复制到 exe 旁)
- **运行**:直接双击 exe;Esc 退出(调试);点击宠物 = 互动
- **当前状态**:Live2D 已上屏并行走(Haru),用户验证通过

## 关键实现约束(改代码前必读)

1. **透明窗口**:`WS_EX_LAYERED` + `SetLayeredWindowAttributes(LWA_COLORKEY)`。逐像素透明靠 backbuffer 中 alpha=0 的区域,所以 **渲染器 Begin() 必须清空为 (0,0,0,0)**,模型只在有内容处写 alpha。破坏这一点窗口会变成黑色矩形。
2. **blend 状态**:预乘 alpha 混合(`BLEND_ONE` / `INV_SRC_ALPHA`)。Live2D 贴图(WIC 32bppPRGBA)是预乘的,渲染器读回时做反预乘再给 GDI BitBlt。
3. **鼠标穿透**:`WS_EX_TRANSPARENT` 动态增删(`Window::SetClickThrough`)。NOACTIVATE 窗口常态不抢焦点。
4. **窗口尺寸 = 模型画布**:当前固定 400×400 逻辑像素,模型/窗口移动都在逻辑坐标;未处理 DPI。
5. **行为状态机在 `behavior.h`**:与渲染解耦。窗口 y 目前固定贴底(`main.cpp` Frame 回调),换边行走只动 x——顶/左/右贴边待完善。
6. **占位模型 `model_stub.cpp`**:无 SDK 时编译,画跳动方块。`HAVE_LIVE2D_SDK` 宏控制工厂选择。
7. **ComPtr**:MinGW 无 `wrl/client.h`,用项目内 `src/com_ptr.h` 替代。
8. **exe 目录解析**:所有资源路径(model3.json/贴图/FrameworkShaders)必须经 `model_live2d.cpp` 的 `ResolvePath()` 解析到 exe 所在目录,不要依赖工作目录(CWD 会因启动方式而异)。

## Live2D 运行期已踩过的坑(改 model_live2d.cpp 前必读)

1. **构造顺序**:`SetDevice` 必须在 `new Impl()` 前调 `EnsureCubismStarted()`——`CubismUserModel` 基类构造用 `CSM_NEW`(Framework placement new 走分配器),StartUp 前分配器为 NULL。
2. **Option 必须 static**:`CubismFramework::StartUp` 只保存 Option 指针,局部变量会悬空。
3. **贴图 SRV 必须持有**:`BindTexture` 只存裸指针,Framework 不 AddRef——Impl 用 `textures_` vector 持有 ComPtr,否则驱动读已释放内存崩溃(表象是显卡驱动崩溃)。
4. **不要自加 scissor**:Framework 自己不设 scissor rect,手动开 ScissorEnable + rect=NULL 违反 D3D11 规范,部分驱动直接崩溃。
5. **MVP 矩阵**:模型 `SetupFromLayout` 后已在 ±1 单位空间,投影只需按窗口宽高比 Scale + `MultiplyByMatrix(modelMatrix)`(与官方 `LAppModel::Draw` 一致)。不要再用 2/canvas 缩放——模型会缩成几个像素。
6. **贴图格式**:WIC 32bppPRGBA 是 8bit/通道,用 `R8G8B8A8_UNORM` 上传(不是 R32G32B32A32_FLOAT)。
7. **COM**:WinMain 开头已 `CoInitializeEx`(WIC 需要),不要移除。

## MinGW × Live2D Framework 补丁(third_party 内,已应用)

1. `CubismNativeInclude_D3D11.hpp`:MSYS2 DirectXMath 头是 `DirectXMath/DirectXMath.h`。
2. `CubismRenderer_D3D11.cpp::ConvertToD3DX`:XMVECTOR 结构体适配(`XMStoreFloat4`)。
3. `CubismRenderer_D3D11.cpp::UpdateConstantBuffer`:常量缓冲 ByteWidth/RowPitch 16 字节对齐——MSYS2 DirectXMath 的 XMFLOAT4X4 对齐为 4(MSVC=16),直接用 sizeof 会错位导致驱动崩溃。

## SDK 结构(third_party/)

- `Framework/`:Framework 源码(src + CMakeLists,`FRAMEWORK_SOURCE=D3D11`)
- `Core/include/`:Core 头文件
- `Core/dll/windows/x86_64/`:官方 dll + gendef/dlltool 生成的 `libLive2DCubismCore.dll.a`(官方 .lib 是 MSVC 格式,MinGW 不能链)
- Framework 源文件全带 UTF-8 BOM,gcc 只有警告,不用处理

## 设计决策记录

- **为什么不用 Electron/Tauri**:WebView2 进程基础开销 ~150MB,不满足轻量目标。
- **为什么不用 Rust**:Live2D 无官方 Rust 绑定,FFI 封装成本高。
- **C++ 原生方案**:无运行时,内存预期 40–60MB,地板 20–30MB。
- **窗口技术选型**:分层窗口(LWA_COLORKEY)而非 DWM blur-behind——本机实测 ULW 与预乘交换链均不可用(见记忆 transparency-constraint),色键是唯一可行路径且自 Win8 起稳定。
- **为什么不用 MSVC**:MinGW 与官方 MSVC 格式的 Core .lib 不兼容,但通过 dll 动态加载解决;Framework 源码编译只需 3 处补丁,整体可行。
