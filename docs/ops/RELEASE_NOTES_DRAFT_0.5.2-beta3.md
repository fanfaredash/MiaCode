# MiaCode 0.5.2-beta3 Release Notes Draft

> Draft for the first public prerelease. Mark the GitHub release as **Prerelease**.

## Summary

MiaCode is a Qt 6 / CMake editor, previewer, and export tool for simai chart authoring. This prerelease focuses on publishing the current Windows workflow with public build scripts, packaged assets, debug-launch support, and clarified non-commercial distribution notes.

## Artifact

Windows:

- `MiaCode-v0.5.2-beta3-win64.zip`
- SHA256: `50E6564E96866AF2B0D8C29EACCED1E9FC2684E8AC21F48104FD0EAD33B4BC57`

## Build And Packaging Verification

Verified on 2026-06-24 from a clean clone:

- `scripts/build/build-win.ps1`
- The build script provisioned Qt 6.8.3, Windows export FFmpeg, and the Windows QtAVPlayer FFmpeg dev SDK from the documented sources.
- Package content audit passed for required app, wrapper, Qt/QML, FFmpeg, BASS, assets, license, third-party notice, and readme files.
- Packaged FFmpeg is located at `app/ffmpeg/ffmpeg.exe`; the package root `MiaCode.exe` is the launcher wrapper and the real app is `app/MiaCode.exe`.
- Smoke-ran the packaged wrapper with `--export-video`; it reached the app CLI and exited with the expected missing `--chart` argument error.
- Deprecated debug launchers and public docs are not included in the Windows package.

## License And Distribution Notes

- Project-owned MiaCode source code is MIT-licensed; see `LICENSE`.
- Repository contents, bundled assets, packaged binaries, and release archives are positioned for non-commercial distribution; see `LICENSE_SCOPE.md`.
- Third-party libraries, fonts, SFX, skins, FFmpeg, BASS, Qt, and reference projects keep their own terms; see `THIRD_PARTY_NOTICES.md`.
- BASS runtime files are included only for the intended non-commercial Windows build/release.
- Intro transition behavior references `gfdfdxc/maimai-transition`; no source or assets from that project are directly copied.

## Known Release-Gating Notes

- Do not publish the existing full Git history as-is. The history scan found old build artifacts, removed Remotion/OpenMoji prototype assets, local absolute paths, and investigation notes in previous commits.
- Before making the repository public, use a filtered history or publish from a fresh clean repository and re-run the current-tree/history scans.
- macOS packaging scripts remain in the repository, but this draft only verifies the Windows package.

## Suggested GitHub Release Text

This is the first public prerelease of MiaCode.

Please read `LICENSE_SCOPE.md` and `THIRD_PARTY_NOTICES.md` before redistributing. The release is intended for non-commercial use, and some bundled dependencies/assets have their own terms.

Download the Windows zip and verify:

```powershell
Get-FileHash .\MiaCode-v0.5.2-beta3-win64.zip -Algorithm SHA256
```

Expected SHA256:

```text
50E6564E96866AF2B0D8C29EACCED1E9FC2684E8AC21F48104FD0EAD33B4BC57
```
