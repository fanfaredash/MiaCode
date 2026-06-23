# Release Checklist

This checklist documents the current local-release flow. GitHub Actions were removed because the old workflows no longer matched the active dependency and packaging setup.

## Version

- [ ] Update `MIACODE_VERSION_MAJOR`, `MIACODE_VERSION_MINOR`, `MIACODE_VERSION_PATCH`, and `MIACODE_VERSION_PRERELEASE` in [CMakeLists.txt](../CMakeLists.txt).
- [ ] Confirm the About dialog and package filename use the same version.
- [ ] Add release notes to [CHANGELOG.md](../CHANGELOG.md).

## Clean Build

- [ ] Start from a clean clone or a clean working tree.
- [ ] Confirm required Qt modules are available: `Core`, `Gui`, `Widgets`, `OpenGL`, `Qml`, `Quick`, `Multimedia`, `Svg`.
- [ ] Confirm FFmpeg runtime/dev SDK provisioning scripts still point at the intended versions.

## Windows Package

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

- [ ] Verify `dist/MiaCode-v<version>-win64`.
- [ ] Verify `dist/MiaCode-v<version>-win64.zip`.
- [ ] Launch the packaged app from `dist/`, not from the build tree.
- [ ] Test opening a sample chart, preview playback, video background decode, SFX playback, and export.

## Checksums

Generate checksums for uploaded artifacts:

```powershell
Get-FileHash .\dist\MiaCode-v*-win64.zip -Algorithm SHA256
```

```bash
shasum -a 256 dist/*.zip
```

## License And Notices

- [ ] Review [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
- [ ] Confirm package contents match the non-commercial distribution notes.
- [ ] Confirm BASS files are present only in the intended non-commercial release package.
- [ ] Confirm FFmpeg build flags and license notes match the packaged binaries.

## Publish

- [ ] Create a signed tag if signing is part of the release process.
- [ ] Upload release zip files and checksums.
- [ ] Include platform support, known issues, non-commercial positioning, and license notes in release notes.
- [ ] Mark the release as prerelease.
