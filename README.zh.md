# Desktopet

极轻量 Windows 桌宠:沿屏幕边缘爬行的 Live2D 形象。目标内存占用 < 60MB。

> **English version: [README.md](README.md)** · [英文版](README.md)

## 运行

直接双击 `desktopet.exe` 即可,宠物会出现在屏幕底边,开始走动。

### 模型文件放哪里

模型放在 exe 同目录的 `assets/` 下,**每个模型一个子文件夹**:

```
desktopet.exe
assets/
  Haru/
    Haru.model3.json     ← 模型配置文件(文件名因模型而异)
    Haru.moc3
    Haru.2048/           ← 贴图
    motions/             ← 动作
    expressions/         ← 表情
    physics.json 等
  其他模型/
    ...
```

- 程序启动时自动枚举 `assets/` 下的所有子文件夹,每个含 `*.model3.json` 的文件夹视为一个模型。
- 把 Live2D 官方示例模型(如 Haru)整个目录复制到 `assets/Haru/` 即可,无需改任何配置。
- 本仓库不随源码分发模型资源(见文末授权说明)。

### 操作与设置

| 操作 | 说明 |
|---|---|
| 单击宠物 | 互动(触发 Happy 表情) |
| 拖动宠物 | 把宠物移到任意位置(松手后停在原地) |
| 滚轮(悬停宠物上) | 缩放大小(0.5× ~ 2×) |
| 托盘图标右键 | 打开菜单(退出/切换模型/设置/开机自启) |

**托盘菜单「设置」里的三个开关**(右键托盘图标 → 设置):

| 设置项 | 功能 |
|---|---|
| 鼠标追随 | 开启后宠物的眼球和头部会看向光标位置 |
| 随机移动 | 开启后宠物沿屏幕边缘随机爬行;关闭则停在原地 |
| 随机动画 | 开启后空闲时随机播放动作;关闭则保持静止姿势 |

**关闭方式**:右下角系统托盘图标右键 →「退出」。(Esc 是调试阶段的临时退出,仅在窗口有焦点时可用。)

> 说明:宠物窗口是置顶、点击穿透的透明窗口,不会挡住其它程序的正常点击;只有光标落在宠物本体上时才会响应鼠标。

## 开发

以下为从源码构建所需的信息;仅使用编译好的 exe 可忽略本章节。

### 工具链

MSYS2(UCRT64 环境),一次性安装:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-json-c
```

### 编译

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=release -DLIVE2D_REAL=ON
cmake --build build
```

- `LIVE2D_REAL=OFF` 时编译占位方块模型(无需 Live2D SDK,用于验证链路)。
- 产物:`build/bin/desktopet.exe`,CMake 会自动把运行所需的 `Live2DCubismCore.dll`、`FrameworkShaders/`、`assets/` 复制到 exe 旁,双击可直接跑。
- 另有 `build.bat` 一键构建脚本(需设置 `MSYS2_ROOT` 环境变量或改脚本顶部路径)。

### Live2D SDK 就位

真实渲染需要 Cubism SDK for Native,`third_party/` 需含:

- `third_party/Framework/src/...`(Framework 源码)
- `third_party/Core/include/` 与 `third_party/Core/dll/windows/x86_64/`(Core 头文件与 dll + 导入库)

SDK 从 <https://www.live2d.com/en/download/cubism-sdk/download-native/> 获取。

### 目录结构

```
src/            程序源码
third_party/    Live2D SDK(Framework 源码 + Core,含 MinGW 补丁)
patches/        SDK 源码补丁(着色器磁盘缓存等,SDK 更新后需重新 apply)
assets/         模型资源(不随仓库分发)
probe/          独立透明窗口探针(验证逐像素 alpha 方案)
CMakeLists.txt
build.bat
```

## 授权说明

- **本项目代码**:MIT 许可(见 `LICENSE`)。
- **Live2D Cubism SDK**:非 MIT,个人/小规模商用免费,收入超阈值需商业许可。公开发布前请核对 <https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html>。
- **模型资源(如 Haru)**:属于 Live2D 官方示例资源,遵循其各自条款,本仓库不随源码再分发。
