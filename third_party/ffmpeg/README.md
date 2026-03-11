# FFmpeg Binaries

This directory stores prebuilt `ffmpeg` executables used by MiaCode video export.

## Files

- `windows/ffmpeg.exe`
  - Source: https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-7.1.zip
  - Size: 107,622,912 bytes
  - SHA256: `DA80A9F19D6D3D58321F4C6C1A7590CE3B98BD7EF59107FEC6556482E188AB9E`
  - Runtime check: `ffmpeg version n7.1.3-43-g5a1f107b4c-20260310`

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
