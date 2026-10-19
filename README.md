# Desktopet

An ultra-lightweight Windows desktop pet: a Live2D character that crawls along your screen edges. Target memory footprint < 60MB.

> **中文版见 [README.zh.md](README.zh.md)** · [中文文档](README.zh.md)

## Running

Double-click `desktopet.exe` — the pet appears at the bottom edge of the screen and starts moving.

### Where model files go

Models live under an `assets/` folder next to the executable, **one sub-folder per model**:

```
desktopet.exe
assets/
  Haru/
    Haru.model3.json     ← model config (filename varies by model)
    Haru.moc3
    Haru.2048/           ← textures
    motions/             ← animations
    expressions/         ← expressions
    physics.json, etc.
  other-models/
    ...
```

- On startup the program enumerates every sub-folder of `assets/`; any folder containing a `*.model3.json` is treated as a model.
- Copy an official Live2D sample model (e.g. Haru) — its whole directory — into `assets/Haru/` and it just works.
- Model resources are not distributed with this repository (see Licensing).

### Interactions & settings

| Action | What it does |
|---|---|
| Click the pet | Interact (triggers a Happy expression) |
| Drag the pet | Move it anywhere (stays where you drop it) |
| Scroll wheel (over the pet) | Resize (0.5× ~ 2×) |
| Right-click tray icon | Open menu (quit / switch model / settings / autostart) |

**Three toggles under the tray menu's "设置" (Settings)** (right-click tray icon → 设置):

| Setting | What it does |
|---|---|
| 鼠标追随 (Follow mouse) | The pet's eyes and head track the cursor |
| 随机移动 (Random movement) | Pet crawls along screen edges at random; off = stays put |
| 随机动画 (Random animation) | Random idle animations; off = keeps a still pose |

**To quit**: right-click the tray icon (bottom-right) → 「退出」(Exit). (`Esc` is a debug-only shortcut and only works when the window has focus.)

> Note: the pet window is top-most and click-through, so it won't block clicks on other apps — the mouse only interacts with it when the cursor is directly over the pet.

## Development

Everything below is only needed to build from source; skip it if you're just running the exe.

### Toolchain

MSYS2 (UCRT64 environment), one-time setup:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc \
          mingw-w64-ucrt-x86_64-cmake \
          mingw-w64-ucrt-x86_64-ninja \
          mingw-w64-ucrt-x86_64-json-c
```

### Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=release -DLIVE2D_REAL=ON
cmake --build build
```

- `LIVE2D_REAL=OFF` builds a placeholder bouncing-cube model (no Live2D SDK needed, for verifying the pipeline).
- Output: `build/bin/desktopet.exe`; CMake automatically copies the runtime requirements (`Live2DCubismCore.dll`, `FrameworkShaders/`, `assets/`) next to the exe.
- A `build.bat` one-click script also exists (set the `MSYS2_ROOT` env var or edit the path at the top of the script).

### Live2D SDK

Real rendering requires the Cubism SDK for Native. `third_party/` must contain:

- `third_party/Framework/src/...` (Framework sources)
- `third_party/Core/include/` and `third_party/Core/dll/windows/x86_64/` (Core headers, dll and import library)

Download the SDK from <https://www.live2d.com/en/download/cubism-sdk/download-native/>.

### Directory layout

```
src/            Program sources
third_party/    Live2D SDK (Framework sources + Core, with MinGW patches)
patches/        SDK source patches (re-apply after upgrading the SDK)
assets/         Model resources (not distributed with the repo)
probe/          Standalone transparent-window probe (verifies per-pixel alpha)
CMakeLists.txt
build.bat
```

## Licensing

- **Project code**: MIT (see `LICENSE`).
- **Live2D Cubism SDK**: not MIT — free for individuals and small-scale commercial use, with a licensing threshold on revenue. See <https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html> before public release.
- **Model resources (e.g. Haru)**: official Live2D sample assets under their own terms; not redistributed with this repository.
