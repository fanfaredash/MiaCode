# FFmpeg Binaries

This directory stores prebuilt `ffmpeg` executables used by MiaCode video export.

## Fixed baseline (0.2.2-dev.1)

- Windows
  - Binary: `windows/ffmpeg.exe`
  - Source package:
    `https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-essentials_build.7z`
  - Upstream build family: Gyan.dev `essentials_build`
  - Runtime version pattern: `ffmpeg version 7.1.1-essentials_build-www.gyan.dev`
  - SHA256 (`ffmpeg.exe`): `B90225987BDD042CCA09A1EFB5E34E9848F2D1DBF5FBCD388753A44145522997`

- macOS
  - Binary: `macos/ffmpeg`
  - Source package: `https://evermeet.cx/ffmpeg/ffmpeg-7.1.zip`
  - Runtime version pattern: `ffmpeg version 7.1`
  - SHA256 (`ffmpeg`): `430D60FBF419DAB28DAEE9B679E7929A31EE9BAE53F6E42E8AE26B725584290F`

- Linux
  - Binary: `linux/ffmpeg`
  - SHA256 (`ffmpeg`): `D91FE748D77422A783BBFA1811E33E12C7BDC1667AB746AB0C765F00467F1AC4`

## Notes

- Release packages now include only `ffmpeg` (not `ffprobe`) to reduce package size.
- Export runtime resolves binaries from app-local `ffmpeg/` and `third_party/ffmpeg/<platform>/` paths.
- When upgrading ffmpeg, update this file and the corresponding ensure scripts together.

## FFmpeg dev SDK (QtAVPlayer preview decode backend)

Separate from the standalone `ffmpeg.exe` above (used by **export**), the **preview** background-video
decode backend links FFmpeg directly via the vendored QtAVPlayer (`third_party/QtAVPlayer/`). That
needs the FFmpeg **shared dev SDK** — headers + import libs + runtime DLLs — provisioned into
`windows/dev/` (gitignored, never committed):

```
third_party/ffmpeg/windows/dev/
  include/   libav*/ headers
  lib/       av*.lib import libs
  bin/       av*.dll runtime (avcodec-61, avformat-61, avutil-59, swresample-5,
             swscale-8, avfilter-10)   # avdevice intentionally dropped — see Size trimming
```

- Provision: `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1` (downloads the BtbN n7.1 LGPL **shared** build;
  override URL via `MIACODE_WINDOWS_FFMPEG_DEV_URL`). The recommended Windows entry point
  `scripts/build/build-win.ps1` runs this automatically on a clean clone.
- CMake finds it via the `MIACODE_FFMPEG_DEV_DIR` cache variable (defaults to `windows/dev/`).
- Major versions must match the packaged runtime: avcodec-61 / avformat-61 / avutil-59 /
  swresample-5 / swscale-8 / avfilter-10 (avdevice dropped).
- License: LGPL v2.1+ (decode-only, **no** `--enable-gpl` / `--enable-nonfree`) — same obligations as
  the FFmpeg already shipped; no new exposure. `avfilter` is a net-new DLL (`avdevice` is dropped,
  see below).

On macOS the same QtAVPlayer source uses VideoToolbox/Metal hardware decode. CMake accepts
`MIACODE_FFMPEG_DEV_DIR=<SDK root>` (the optional repo-local default is `macos/dev/`); the
standard package script otherwise resolves Homebrew `ffmpeg@6`, then `ffmpeg`. The macOS package
copies the SDK's complete non-system dylib closure into `MiaCode.app/Contents/Frameworks` and
rewrites absolute install names to `@rpath`, so the resulting app must not retain `/opt/homebrew`
or another build-machine dylib reference.

### Size trimming

The default BtbN n7.1 LGPL *shared* build is full-featured and large (`avcodec-61` ≈63 MB,
`avfilter-10` ≈24 MB, `avformat-61` ≈20 MB). Two levers:

1. **Drop `avdevice` (done in-tree, ≈7 MB).** `avdevice` only provides capture-device I/O
   (cameras, screen-grab, `dshow`/`gdigrab`) — useless for file playback. The vendored QtAVPlayer
   is patched to compile out its single `avdevice_register_all()` call and not link the lib (CMake
   define `QT_AVPLAYER_NO_AVDEVICE`), so `avdevice-61.dll` is neither built-against nor shipped.

2. **Minimal decode-only `avcodec`/`avfilter` (the big lever, ~70 MB → ~15–20 MB).** Automated by
   the **`scripts/ffmpeg/trim/`** toolchain — see its [README](../../scripts/ffmpeg/trim/README.md).
   It builds a decode-only LGPL n7.1 *shared* FFmpeg from a reviewed allowlist
   (`scripts/ffmpeg/trim/trim-allowlist.psd1`), generates MSVC import libs, and installs into
   `third_party/ffmpeg/windows/dev/` (backing up the full set to `dev.full.bak`):

   ```powershell
   scripts\ffmpeg\trim\survey-chart-codecs.ps1 -ChartRoots <dirs>   # calibrate allowlist vs real PVs
   scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1 -PrintPlanOnly      # review the configure plan
   scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1                     # build + install (~30–60 min)
   ```

   It keeps major versions pinned (avcodec-61 / avfilter-10 / …) to match CMakeLists.txt +
   `scripts/build/package-win.ps1`, keeps the mandatory QtAVPlayer filtergraph endpoints
   (`buffer`/`buffersink`/`abuffer`/`abuffersink`) + `scale`/`format`/`fps` + `protocol=file` +
   D3D11VA hwaccels, and validates the allowlist against `./configure --list-*` so a missing
   component is reported (not silently 误删'd). Alternatively, drop a pre-built trimmed
   `include/ lib/ bin/` into `third_party/ffmpeg/windows/dev/`, or point
   `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1` at it via `MIACODE_WINDOWS_FFMPEG_DEV_URL=<your-zip>`.
