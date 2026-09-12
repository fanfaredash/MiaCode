# FFmpeg Binaries

This directory stores prebuilt `ffmpeg` executables used by MiaCode video export.

## Fixed baseline (0.2.2-dev.1)

- Windows
  - Binaries: `windows/win64/ffmpeg.exe` (x64) and `windows/winarm64/ffmpeg.exe` (arm64)
  - x64 source package: `https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-essentials_build.7z`
    (Gyan.dev `essentials_build`, 87 MB static GPL binary)
  - arm64 source package: BtbN FFmpeg-Builds n8.1 **GPL** static build pinned to the
    immutable tag `autobuild-2026-09-12-13-12`
    (`.../releases/download/autobuild-2026-09-12-13-12/ffmpeg-n8.1.2-52-g5a03dfa0f6-winarm64-gpl-8.1.zip`)
  - Both are static GPL builds carrying libx264, which the export encoder probe needs
    for software H.264 export.
  - Runtime version patterns and SHA256 values live in
    `scripts/ffmpeg/ensure-windows-ffmpeg.ps1`

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
`windows/<win64|winarm64>/dev/` (gitignored, never committed):

```
third_party/ffmpeg/windows/<win64|winarm64>/dev/
  include/   libav*/ headers
  lib/       av*.lib import libs
  bin/       av*.dll runtime
             x64   (trimmed, n7.1):  avcodec-61, avformat-61, avutil-59,
                                       swresample-5, swscale-8, avfilter-10
             arm64 (full, n8.1):     avcodec-62, avformat-62, avutil-60,
                                       swresample-6, swscale-9, avfilter-11
             # avdevice intentionally dropped — see Size trimming
```

- Provision:
  - x64: `scripts/ffmpeg/trim/build-trimmed-ffmpeg.ps1` builds the decode-only n7.1 SDK from
    source; `scripts/build/build-win.ps1` runs it by default (`-SkipTrim` turns it off, and then
    `MIACODE_WINDOWS_FFMPEG_DEV_URL` has to point at a compatible SDK — BtbN dropped the n7.1
    prebuilt assets, so there is no default download for x64).
  - arm64: `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1 -Arch arm64` downloads the pinned
    BtbN n8.1 LGPL **shared** build; the trim toolchain covers x64 only, so the arm64 package
    ships that full SDK.
  - Both honour `MIACODE_WINDOWS_FFMPEG_DEV_URL` as a URL override.
- CMake finds the SDK via the `MIACODE_FFMPEG_DEV_DIR` cache variable (defaults to the
  architecture's `windows/<win64|winarm64>/dev/`).
- Major versions must match the packaged runtime per architecture; the package contract in
  `scripts/build/windows-toolchain.psd1` pins both sets (avdevice dropped).
- License: LGPL v2.1+ (decode-only, **no** `--enable-gpl` / `--enable-nonfree`) — same obligations as
  the FFmpeg already shipped; no new exposure. `avfilter` is a net-new DLL (`avdevice` is dropped,
  see below).

On macOS the same QtAVPlayer source uses VideoToolbox/Metal hardware decode. Run
`bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh` to build the pinned FFmpeg 6.1.2 LGPL shared SDK
under `macos/dev/` (ignored, never committed). It contains only the six QtAVPlayer runtime dylibs
and has `@rpath` install names, arm64 architecture, and a macOS 13 deployment target. CMake and
the package script use this repo-local SDK by default; macOS packaging never falls back to
Homebrew and rejects build-machine dylib references.

### Size trimming

The default BtbN n8.1 LGPL *shared* build is full-featured and large (`avcodec-62` ≈63 MB,
`avfilter-11` ≈24 MB, `avformat-62` ≈20 MB). Two levers:

1. **Drop `avdevice` (done in-tree, ≈7 MB).** `avdevice` only provides capture-device I/O
   (cameras, screen-grab, `dshow`/`gdigrab`) — useless for file playback. The vendored QtAVPlayer
   is patched to compile out its single `avdevice_register_all()` call and not link the lib (CMake
   define `QT_AVPLAYER_NO_AVDEVICE`), so `avdevice-62.dll` is neither built-against nor shipped.

2. **Minimal decode-only `avcodec`/`avfilter` (the big lever, ~70 MB → ~15–20 MB).** Automated by
   the **`scripts/ffmpeg/trim/`** toolchain — see its [README](../../scripts/ffmpeg/trim/README.md).
   It builds a decode-only LGPL n8.1 *shared* FFmpeg from a reviewed allowlist
   (`scripts/ffmpeg/trim/trim-allowlist.psd1`), generates MSVC import libs, and installs into
   `third_party/ffmpeg/windows/win64/dev/` (backing up the full set to `dev.full.bak`):

   ```powershell
   scripts\ffmpeg\trim\survey-chart-codecs.ps1 -ChartRoots <dirs>   # calibrate allowlist vs real PVs
   scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1 -PrintPlanOnly      # review the configure plan
   scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1                     # build + install (~30–60 min)
   ```

   It keeps major versions pinned (avcodec-62 / avfilter-11 / …) to match CMakeLists.txt +
   `scripts/build/package-win.ps1`, keeps the mandatory QtAVPlayer filtergraph endpoints
   (`buffer`/`buffersink`/`abuffer`/`abuffersink`) + `scale`/`format`/`fps` + `protocol=file` +
   D3D11VA hwaccels, and validates the allowlist against `./configure --list-*` so a missing
   component is reported (not silently 误删'd). Alternatively, drop a pre-built trimmed
   `include/ lib/ bin/` into `third_party/ffmpeg/windows/win64/dev/` (the trim toolchain covers the x64 SDK), or point
   `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1` at it via `MIACODE_WINDOWS_FFMPEG_DEV_URL=<your-zip>`.
