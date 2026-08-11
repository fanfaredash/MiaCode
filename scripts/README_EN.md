# Scripts

[中文](README.md) | [English](README_EN.md)

This directory keeps only public, repeatable build, release, asset, and diagnostic scripts. One-off analysis scripts and retired A/B launchers are maintainer-local tools and are not tracked publicly.

## Layout

| Directory | Contents |
|---|---|
| `build/` | Windows/macOS build and packaging entry points |
| `debug/` | Currently retained Windows debug/diagnostic launchers |
| `ffmpeg/` | FFmpeg runtime/dev-SDK provisioning plus the decode-only trim toolchain |
| `assets/` | Asset generation and font-subsetting helpers |

## Build And Package

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

`package-win.ps1` defaults to `build/` and checks whether the generated version header and `MiaCode.exe` need refreshing. If the executable is missing, version-stale, or older than the generated version header, it rebuilds `MiaCode` automatically.

macOS:

```bash
bash scripts/build/build-macos.sh
```

```bash
QT_ROOT="$HOME/Qt/6.10.2/macos" CMAKE_OSX_ARCHITECTURES=arm64 bash scripts/build/package-mac.sh
```

Artifacts are written to `dist/`. When a single `arm64` or `x86_64`
architecture is requested, packaging removes the other architecture slice from
Qt frameworks/plugins after `macdeployqt` and verifies that every bundled
Mach-O contains only the target architecture before re-signing. Set
`MIACODE_THIN_MACOS_APP=OFF` to produce a comparison package that keeps Qt's
universal binaries.

The macOS QtAVPlayer preview decoder also needs an FFmpeg development SDK.
`package-mac.sh` uses `MIACODE_FFMPEG_DEV_DIR` when set, otherwise discovers
Homebrew `ffmpeg@6` (falling back to `ffmpeg`). It copies the non-system FFmpeg
dylib closure into `MiaCode.app/Contents/Frameworks` and rewrites it to the
bundle's `@rpath`, so a release package does not depend on the build machine's
Homebrew paths.

## Other Scripts

- `debug/Start_MiaCode_Debug.bat`: the only Windows debug launcher shipped in release packages.
- `debug/Start_MiaCode_SoftwareVideoDecode.bat`, `debug/Start_MiaCode_QtPluginDiag.bat`: public support diagnostics; not shipped in the Windows release package.
- `ffmpeg/ensure-windows-ffmpeg.ps1`, `ffmpeg/ensure-macos-ffmpeg.sh`: provision the standalone export `ffmpeg`.
- `ffmpeg/ensure-windows-ffmpeg-dev.ps1`: provisions the Windows QtAVPlayer preview-decode dev SDK.
- `ffmpeg/trim/`: builds a trimmed Windows decode-only FFmpeg dev SDK.
- `assets/subset_hud_font.py`: generates the HUD font subset; see `assets/README_font_subset.md`.
- `assets/gen_same_lane_v_slides.py`: generates same-lane V-slide reference data.
- `assets/match_outline_canvas_ratio.py`: expands transparent outline PNG canvases by the fixed 980:1080 ratio without scaling the visible pixels.
- `gen_skin_mine_sprites.py`: generates `_mine.png` sprites from normal skin sprites using the MajMine luminance grayscale transform.
- `assets/build_skin_tool_exes.ps1`: packages the outline and mine sprite helpers as standalone Windows executables under `dist/skin-tools-win64`.
