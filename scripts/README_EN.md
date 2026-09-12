# Scripts

[中文](README.md) | [English](README_EN.md)

This directory keeps only public, repeatable build, release, asset, and diagnostic scripts. One-off analysis scripts and retired A/B launchers are maintainer-local tools and are not tracked publicly.

## Layout

| Directory | Contents |
|---|---|
| `build/` | Windows/macOS build, packaging and package verification entry points |
| `debug/` | Windows/macOS debug and diagnostic launchers |
| `ffmpeg/` | FFmpeg runtime/dev-SDK provisioning plus the decode-only trim toolchain |
| `assets/` | Asset generation and font-subsetting helpers |

## Build And Package

Windows:

```powershell
# MSVC x64: the generator is resolved through vswhere (Visual Studio + Windows SDK required)
# x64 builds the decode-only preview SDK with ffmpeg/trim by default (~150 MB -> ~20 MB, needs
# MSYS2); pass -SkipTrim to skip that build.
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc -BuildDir build-msvc

# MSVC arm64: native arm64 host, ARM64 generator platform and Qt's arm64 package
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain msvc-arm64 -BuildDir build-msvc-arm64

# MinGW + Ninja: Qt's mingw_64 build
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1 -Toolchain mingw -BuildDir build

# Package an existing build (-Arch has to match the build)
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot> -BuildDir <BuildDir> -Arch x64

# Verify a package
powershell -ExecutionPolicy Bypass -File .\scripts\build\verify-win-package.ps1 -DistDir .\dist\MiaCode_<version>_win_x64 -Arch x64
```

`build-win.ps1` runs, in order: Python dependencies (py7zr only), the export `ffmpeg.exe`, the
preview FFmpeg dev SDK (a trim build on x64 unless `-SkipTrim` is set), Qt resolution, CMake configure and
build, then `package-win.ps1`. Both
FFmpeg chains and the packaging chain pick their directories from the architecture the selected
toolchain targets: `third_party\ffmpeg\windows\<win64|winarm64>\` and
`third_party\bass\bin\<win64|winarm64>\`, producing `MiaCode_<version>_win_x64` or
`MiaCode_<version>_win_arm64` (a channel segment from `MIACODE_PACKAGE_CHANNEL` lands right after the version). The arm64 package ships no `bass_aac.dll` (upstream publishes that
add-on for x86/x64 only); the audio backend already tolerates a missing add-on.

Qt resolution order: `-QtRoot` → `C:\Qt\<version>\<arch dir>` → `.qt\<version>\<arch dir>` →
`QT_ROOT_DIR`/`Qt6_DIR` → otherwise `build/provision-qt.ps1` downloads it from the Qt repository.
That script handles both upstream repository layouts (the flat pre-6.11 folders and the
per-architecture folders 6.11 uses), verifies every archive against the published `.sha1`, and
needs no aqtinstall.

`package-win.ps1` rebuilds `MiaCode` and `MiaCodeLauncher` when needed, places the selected
toolchain's C++ runtime and the trimmed FFmpeg runtimes into `app/`, asserts the package contents
contract, and writes a 7z archive. With a single-config generator it also checks that
`CMAKE_BUILD_TYPE` matches `-Config`.

`verify-win-package.ps1` checks the contents contract, that the packaged FFmpeg DLLs match the dev
SDK byte for byte, that every non-system import resolves inside the package, a smoke launch, and the
archive; it exits 1 on any failure.

The preview FFmpeg SDK can come from `ffmpeg/trim/build-trimmed-ffmpeg.ps1` instead of the full SDK
that `ffmpeg/ensure-windows-ffmpeg-dev.ps1` downloads.

Qt version and modules, all three toolchain definitions (target architecture, Qt host repository
tree, MSVC redistributable subdirectory), the package contents lists and the archive format live in
`build/windows-toolchain.psd1`. Give each toolchain its own build directory — generators and target
architectures cannot share one.

macOS:

Local packaging, reusing the Qt install, FFmpeg dev SDK, and export `ffmpeg`
already present on the machine:

```bash
bash scripts/build/build-macos-local.sh
```

CI packaging, installing Qt 6.10.2 and both FFmpeg pieces on a clean runner
before assembling the package:

```bash
bash scripts/build/build-macos-ci.sh
```

Both entries hand the Release build and package assembly to `package-mac.sh`;
call it directly for manual control, for example
`QT_ROOT="$HOME/Qt/6.10.2/macos" bash scripts/build/package-mac.sh`. The CI
entry reads `QT_VERSION`, `QT_MODULES`, and `MIACODE_PYTHON_VENV_DIR`; the local
entry reads `QT_ROOT` (probing `.qt/` then `$HOME/Qt/` for `QT_VERSION`),
`BUILD_DIR`, and `MIACODE_FFMPEG_DEV_DIR`. Setting `MIACODE_PACKAGE_CHANNEL`
inserts a channel segment into the package name.

Artifacts are written to `dist/`. When a single `arm64` or `x86_64`
architecture is requested, packaging removes the other architecture slice from
Qt frameworks/plugins after `macdeployqt` and verifies that every bundled
Mach-O contains only the target architecture before re-signing. Set
`MIACODE_THIN_MACOS_APP=OFF` to produce a comparison package that keeps Qt's
universal binaries.

The macOS QtAVPlayer preview decoder also needs an FFmpeg development SDK at the
repo-local `third_party/ffmpeg/macos/dev/`, created by
`bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh` (an existing SDK that passes
validation is reused). `package-mac.sh` uses that SDK (or an explicit compatible
`MIACODE_FFMPEG_DEV_DIR`) and stages only its six required dylibs; it does not
discover or copy Homebrew.

## Other Scripts

- `debug/Start_MiaCode_Debug.bat`: the only Windows debug launcher shipped in release packages.
- `debug/Start_MiaCode_Debug.command`: the macOS debug launcher at the release package root; double-click it to launch `MiaCode.app` with `--debug` and write logs to the package-root `logs/` directory.
- `debug/Start_MiaCode_SoftwareVideoDecode.bat`, `debug/Start_MiaCode_QtPluginDiag.bat`: public support diagnostics; not shipped in the Windows release package.
- `ffmpeg/ensure-windows-ffmpeg.ps1`, `ffmpeg/ensure-macos-ffmpeg.sh`, `ffmpeg/ensure-linux-ffmpeg.sh`: provision the standalone export `ffmpeg`.
- `ffmpeg/ensure-macos-ffmpeg-dev.sh`: builds the pinned macOS FFmpeg 6 SDK for QtAVPlayer preview decode.
- `ffmpeg/ensure-windows-ffmpeg-dev.ps1`: provisions the Windows QtAVPlayer preview-decode dev SDK.
- `ffmpeg/trim/`: builds a trimmed Windows decode-only FFmpeg dev SDK.
- `assets/subset_hud_font.py`: generates the HUD font subset; see `assets/README_font_subset.md`.
- `assets/gen_same_lane_v_slides.py`: generates same-lane V-slide reference data.
- `assets/match_outline_canvas_ratio.py`: expands transparent outline PNG canvases by the fixed 980:1080 ratio without scaling the visible pixels.
- `gen_skin_mine_sprites.py`: generates `_mine.png` sprites from normal skin sprites using the MajMine luminance grayscale transform.
- `assets/build_skin_tool_exes.ps1`: packages the outline and mine sprite helpers as standalone Windows executables under `dist/skin-tools-win64`.
