# maicode

`maicode` is a native Qt 6 simai editor and preview host.

The current codebase is self-contained for the main editing path:
- native C++ simai parsing for timeline metadata and validation
- native OpenGL-backed preview rendering
- native miniaudio-based preview SFX and BGM playback

The old Python bridge is no longer required for the main workflow. A legacy pygame preview session fallback still exists in code, but it is disabled by default and optional.

## Features

- text editor with optional QScintilla integration
- validation error list with jump-to-location
- native timeline view with:
  - waveform background
  - zoom levels
  - playback line and cursor line
  - note previews for tap / hold / slide / wifi / touch / touch-hold
- native OpenGL preview with staged GPU migration
- local preview audio settings and display settings
- native `simai_native_dump` utility for parser inspection

## Build

Requirements:

- CMake 3.21+
- Qt 6.8+ (`Core`, `Gui`, `Widgets`, `OpenGLWidgets`)
- Optional: `Qt6::Multimedia`
- Optional: QScintilla for Qt 6

This workspace is configured for:

- `D:/Qt/6.8.3/msvc2022_64`

via [CMakePresets.json](CMakePresets.json).

Configure and build:

```powershell
cmake --preset vs2022-qt6
cmake --build --preset release
```

Run:

```powershell
.\build\Release\maicode.exe
```

Parser dump:

```powershell
.\build\Release\simai_native_dump.exe D:\Desktop\maimuri\test\maidata.txt
```

## Windows Packaging

After a Release build:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1
```

This copies `maicode.exe` into `dist/windows-x64` and runs `windeployqt`.

## Environment Variables

Current environment variables:

- `MAICODE_TRACK_PATH`
  Optional default `track.mp3` path for preview.
- `MAICODE_PREVIEW_SFX_DIR`
  Optional override directory for preview SFX assets.
- `MAICODE_ENABLE_PYGAME_PREVIEW`
  Optional legacy preview-session fallback toggle.
- `MAICODE_PREVIEW_SESSION_SCRIPT`
  Optional override path for the legacy pygame preview session script.

Backward-compatible `MAIMURI_*` names are still accepted for now.

## Quick Check

1. Build and run `maicode.exe`.
2. Open `D:\Desktop\maimuri\test\maidata.txt`.
3. Confirm:
   - timeline renders notes and waveform
   - `Validation Errors` stays empty
   - `Preview From Start` opens the native Qt preview path
4. Run:

```powershell
.\build\Release\simai_native_dump.exe D:\Desktop\maimuri\test\maidata.txt
```

Expected current baseline:

- `ok: true`
- `note_count: 460`
- `error_count: 0`

## Repository Layout

- [src](src): application source
- [assets](assets): embedded assets and generated slide data
- [resources](resources): Qt resource collections
- [scripts](scripts): packaging helpers
- [third_party](third_party): vendored dependencies (including miniaudio)

