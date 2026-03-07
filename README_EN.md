# MiaCode

[中文](README.md) | [English](README_EN.md)

`MiaCode` is a simai editor and preview tool built with Qt 6 + OpenGL.

## Features

- Text editor
- Validation error list with jump-to-location
- Native timeline view
  - Waveform background
  - Zoom levels
  - Playback line and cursor line
  - Tap, hold, slide, wifi, touch, and touch-hold preview
- Native OpenGL preview
- Local render settings for audio and video

## Build

Requirements:
- CMake 3.21+
- Qt 6.8+ with `Core`, `Gui`, `Widgets`, `OpenGLWidgets`, `Multimedia`

### Windows Build

The recommended entry point is the helper script that installs Qt, builds, and packages:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

If Qt is already installed locally, you can also build with [CMakePresets.json](CMakePresets.json):

```powershell
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

### macOS Build

The recommended entry point is the helper script that installs Qt, builds, and packages:

```bash
bash scripts/build-macos.sh
```

If Qt is already installed locally, you can also pass `QT_ROOT` explicitly to the packaging script:

```bash
QT_ROOT="$HOME/Qt/6.8.3/macos" bash scripts/package-mac.sh
```

Packaging details have been moved to [scripts/README_EN.md](scripts/README_EN.md).

## Repository Layout

- [src](src): source code
- [assets](assets): runtime assets and generated data
- [resources](resources): Qt resource files
- [scripts](scripts): packaging scripts
- [third_party](third_party): third-party dependencies

---

## Acknowledgements

Thanks to [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) for reference implementations of simai syntax parsing and rendering logic.

## Changelog

### 0.1.1

- UI polish.
- Added application icon.

### 0.1.0

- **Simai Editing**
  - Full support for simai parsing and editing workflow.
  - Multi-difficulty field management, including add, delete, and auto-switch.
  - Batch mirror and rotate operations.
  - Support for both difficulty pages and chart metadata settings.
- **Rendering and Preview**
  - Player-style preview controls with timeline and speed options.
  - Optimized video and audio preview pipeline to reduce stutter.
  - Stronger timeline-preview synchronization for drag and seek behavior.
- **Others**
  - Chinese and English UI support.
