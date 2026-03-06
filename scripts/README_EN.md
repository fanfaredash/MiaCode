# Scripts

[中文](README.md) | [English](README_EN.md)

This directory contains build and packaging helpers.

## Windows

- `build-win.ps1`: installs Qt automatically, runs the CMake build, and then calls `package-win.ps1`
- `package-win.ps1`: runs `windeployqt` against an existing Windows build and writes release artifacts to `dist/`

Common examples:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot D:\Qt\6.8.3\msvc2022_64
```

Output:
- `dist/MiaCode-v<version>-win64`
- `dist/MiaCode-v<version>-win64.zip`

## macOS

- `build-macos.sh`: installs Qt automatically, builds the project, and then calls `package-mac.sh`
- `package-mac.sh`: runs `macdeployqt`, signs the packaged app, and writes release artifacts to `dist/`

Common examples:

```bash
bash scripts/build-macos.sh
```

```bash
QT_ROOT="$HOME/Qt/6.8.3/macos" bash scripts/package-mac.sh
```

Optional environment variables:
- `CMAKE_OSX_ARCHITECTURES=arm64` or `x86_64`
- `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`
- `MACOS_CODESIGN_IDENTITY="-"`: use ad-hoc signing, or replace it with a real signing identity

Output:
- `dist/MiaCode-v<version>-macos`
- `dist/MiaCode-v<version>-macos.zip`
