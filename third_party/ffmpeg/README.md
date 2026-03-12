# FFmpeg Binaries

This directory stores prebuilt `ffmpeg` executables used by MiaCode video export.

## Files

- `windows/ffmpeg.exe`
  - Source: https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-7.1.zip
  - Current observed SHA256 on 2026-03-12: `56A6CAF7B94C88E0741AD57536033BC0A1D74A0DAB072C840DD41879C7E51082`
  - Runtime check: `ffmpeg.exe -version` starts with `ffmpeg version n7.1.`

- `linux/ffmpeg`
  - Source: https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-linux64-lgpl-7.1.tar.xz
  - Size: 109,580,488 bytes
  - SHA256: `D91FE748D77422A783BBFA1811E33E12C7BDC1667AB746AB0C765F00467F1AC4`

- `macos/ffmpeg`
  - Source: https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip
  - Size: 80,083,328 bytes
  - SHA256: `430D60FBF419DAB28DAEE9B679E7929A31EE9BAE53F6E42E8AE26B725584290F`

## Notes

- Downloaded on 2026-03-11.
- Export runtime resolves these binaries from app-local `ffmpeg/` and `third_party/ffmpeg/<platform>/` paths.
- Keep binary replacement explicit and update hashes in this file after upgrades.
- The Windows `latest` asset is mutable. CI validates runtime/version by default instead of pinning a fixed hash.
