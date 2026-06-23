<p align="center">
  <img src="resources/icons/app.png" alt="MiaCode avatar" width="128">
</p>

# MiaCode

[中文](README.md) | [English](README_EN.md)

MiaCode is a desktop editor, previewer, and export tool for simai chart authoring. It is built with Qt 6 and CMake, and covers the core workflow from text editing and validation to timeline preview, realtime playback, audio/video alignment helpers, and chart-video export.

## Features

- simai text editing, syntax highlighting, find/replace, and multi-difficulty field management
- Chart parsing, validation diagnostics, issue list, and jump-to-location navigation
- Native timeline view with waveform, zoom, playhead, cursor line, and note previews
- Qt Quick realtime preview and offscreen export pipeline
- Preview support for tap, hold, slide, wifi, touch, touch-hold, mine, break touch, and related objects
- BPM / offset / playback-latency helper tools
- Muri detection and chart diagnostics
- Full-video export, clip export, batch export, and chart ZIP packaging helpers
- Local resources, skins, SFX, background images/videos, and intro-template support

## In-House Components

MiaCode contains substantial project-owned implementation rather than only wiring external tools together:

- simai document model, parsing, validation, and batch transforms
- Timeline data sources, rendering, and editor synchronization
- Preview scene state, Qt Quick rendering layers, and export snapshots
- Preview audio, SFX timelines, latency detection, and export audio planning
- Muri analysis, chart diagnostics, and developer helper tools
- Windows build, dependency-provisioning, and packaging scripts

Project-owned MiaCode source code is MIT-licensed; the repository as a whole, bundled assets, and release packages are intended for non-commercial use. See [LICENSE_SCOPE.md](LICENSE_SCOPE.md) for the license boundary and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for third-party libraries, assets, and reference projects.

## Build

### Requirements

- CMake 3.21+
- C++20 compiler
- Qt 6.8+ with `Core`, `Gui`, `Widgets`, `Network`, `OpenGL`, `Qml`, `Quick`, `QuickControls2`, `ShaderTools`, `Multimedia`, `Svg`
- Windows: Visual Studio 2022 / MSVC; the FFmpeg export binary and QtAVPlayer FFmpeg dev SDK are provisioned by scripts

See [scripts/README_EN.md](scripts/README_EN.md) for packaging details.

### Windows

The recommended entry point installs Qt, prepares dependencies, builds, and packages:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

If Qt is already installed locally, you can also build with the CMake preset:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

The preset path expects Qt to be discoverable by CMake or `CMAKE_PREFIX_PATH`. On Windows, it also expects `third_party/ffmpeg/windows/ffmpeg.exe` and `third_party/ffmpeg/windows/dev/` to exist; the two FFmpeg scripts above download the pinned contents and validate the required files.

To package an existing build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

## Repository Layout

- [src](src): application source, core models, preview, audio, export, and tools
- [assets](assets): runtime resources, skins, SFX, background media, and generated data
- [resources](resources): Qt resource collections and application icons
- [scripts](scripts): build, dependency-provisioning, packaging, and maintenance scripts
- [third_party](third_party): vendored or referenced third-party dependencies
- [docs](docs): architecture, diagnostics, export, timeline, and open-source preparation docs
- [samples](samples): sample charts and acceptance materials

## Releases

Release packages are currently produced locally by maintainers using the scripts in this repository; obsolete GitHub Actions have been removed. See [docs/ops/RELEASE_CHECKLIST.md](docs/ops/RELEASE_CHECKLIST.md) for release steps and [docs/ops/OPEN_SOURCE_CHECKLIST.md](docs/ops/OPEN_SOURCE_CHECKLIST.md) for remaining open-source readiness checks.

## License And Acknowledgements

Project-owned MiaCode source code is MIT-licensed; see [LICENSE](LICENSE). The repository as a whole, bundled assets, packaged binaries, and release packages are intended for non-commercial use; see [LICENSE_SCOPE.md](LICENSE_SCOPE.md). Third-party libraries, fonts, SFX, images, FFmpeg, BASS, Qt, and reference implementations may have their own licenses or redistribution limits; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Thanks to [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) and related projects for simai parsing, preview, and engineering references. The intro transition references [gfdfdxc/maimai-transition](https://github.com/gfdfdxc/maimai-transition).

## Changelog

Historical release notes have moved to [CHANGELOG.md](CHANGELOG.md).

## Community

QQ group: 1095435375

<p align="center">
  <img src="resources/community/qq-group.png" alt="MiaCode QQ group QR code" width="360">
</p>
