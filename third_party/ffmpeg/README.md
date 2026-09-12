# FFmpeg distribution

MiaCode keeps preview decoding and video export in separate license domains.

## Preview SDK

QtAVPlayer links the FFmpeg 8.1 ABI dynamically on Windows and macOS. Linux
links the host FFmpeg through pkg-config.

| Target | SDK | Runtime |
|---|---|---|
| Windows x64 | source-built FFmpeg 8.1.2 decode profile | six trimmed `av*.dll` files |
| Windows arm64 | pinned BtbN FFmpeg 8.1 LGPL shared build | six `av*.dll` files |
| macOS arm64 | source-built FFmpeg 8.1.2 LGPL profile | six arm64 `dylib` files |

The preview SDK enables LGPL components, file playback and the platform hardware
decoder. `avdevice` and `postproc` stay outside the package. Windows x64 uses
`scripts/ffmpeg/trim/trim-allowlist.psd1` to keep
the runtime near the existing 15–20 MB target. The macOS build uses
`--disable-autodetect` and produces an arm64-only SDK.

Provisioning commands:

```powershell
scripts\ffmpeg\trim\build-trimmed-ffmpeg.ps1
scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1 -Arch arm64
```

```bash
bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh
```

## Export executable

Export runs a separate static `ffmpeg` process. The packaged executable keeps
`libx264` as the software H.264 fallback and therefore follows GPL terms. Its
runtime does not link into MiaCode or the LGPL preview SDK.

| Target | Source package |
|---|---|
| Windows x64 | Gyan.dev FFmpeg 7.1.1 essentials |
| Windows arm64 | pinned BtbN FFmpeg 8.1 GPL build |
| macOS arm64 | pinned FFmpeg 8.1.2 arm64 build |
| Linux x64 | pinned BtbN FFmpeg 7.1 GPL build |

The `ensure-<platform>-ffmpeg` scripts own download URLs and hashes. Packages
carry `ffmpeg` without `ffprobe`.

## CI cache boundary

GitHub Actions caches `third_party/ffmpeg/<platform>` using the FFmpeg scripts
as the cache key. An FFmpeg configuration change creates a new media cache;
application commits reuse the prepared SDK and export executable.
