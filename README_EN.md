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

Build with [CMakePresets.json](CMakePresets.json):

```powershell
cmake --preset vs2022-qt6
cmake --build --preset release
```

Run:

```powershell
.\build\Release\MiaCode.exe
```

## Repository Layout

- [src](src): source code
- [assets](assets): runtime assets and generated data
- [resources](resources): Qt resource files
- [scripts](scripts): packaging scripts
- [third_party](third_party): third-party dependencies

## Windows Packaging

If `windeployqt` is not found in `PATH`, pass `-QtRoot` explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot>
```

If `Qt\bin` is already in `PATH`, you can run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1
```

Output:
- `dist/MiaCode-v<version>-portable-win64`
- `dist/MiaCode-v<version>-portable-win64.zip`

---

## Acknowledgements

Thanks to [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) for reference implementations of simai syntax parsing and rendering logic.

## Changelog

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
