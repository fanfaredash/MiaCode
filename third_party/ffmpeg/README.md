# FFmpeg Binaries

This directory stores prebuilt `ffmpeg` executables used by MiaCode video export.

## Fixed baseline (0.2.2-dev.1)

- Windows
  - Binary: `windows/ffmpeg.exe`
  - Source package:
    `https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-7.1.1-essentials_build.7z`
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
