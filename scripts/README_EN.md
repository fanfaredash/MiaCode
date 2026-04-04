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
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot>
```

Notes:
- `package-win.ps1` defaults to the `build/` directory.
- `build-win.ps1` now installs `qtmultimedia`, `qtdeclarative`, and `qtsvg` so the Qt Quick runtime and export session can be packaged from a clean machine.
- Before packaging, it checks whether `build/generated/AppVersion.h` and `build/<Config>/MiaCode.exe` match the current `CMakeLists.txt`; if the executable is missing, version-stale, or older than the generated version header, it automatically runs `cmake --build build --target MiaCode --config <Config> --parallel 8`.
- If `-QtRoot` is omitted, the script tries to find `windeployqt` from the current `PATH`.
- `-BuildJobs` defaults to `8`, and can be overridden per machine.
- If you want Qt to be installed automatically and then build/package in one step, use `build-win.ps1`.
- The packaged Windows artifact explicitly keeps the Qt Quick / QML runtime DLL set (`Qt6Quick`, `Qt6Qml`, `Qt6QmlMeta`, `Qt6QmlModels`, `Qt6QmlWorkerScript`) and rejects stale legacy DLLs such as `Qt6OpenGLWidgets.dll`.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot> -BuildJobs 8
```

Output:
- `dist/MiaCode-v<version>-win64`
- `dist/MiaCode-v<version>-win64.zip`

## macOS

- `build-macos.sh`: installs Qt automatically, builds the project, and then calls `package-mac.sh`
- `package-mac.sh`: runs `macdeployqt`, signs the packaged app, and writes release artifacts to `dist/`
- `build-macos.sh` now installs `qtmultimedia`, `qtdeclarative`, and `qtsvg` for the same Qt Quick packaging baseline as Windows.

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
- `dist/MiaCode-v<version>-macos-apple-silicon` or `dist/MiaCode-v<version>-macos-intel`
- `dist/MiaCode-v<version>-macos-apple-silicon.zip` or `dist/MiaCode-v<version>-macos-intel.zip`
