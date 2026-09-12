# Open Source Checklist

Use this checklist before making the repository public. It intentionally separates items Codex can prepare from items that require project-owner confirmation.

## Low-Risk Cleanup

- [x] Merge `test` into `main`.
- [x] Remove obsolete GitHub Actions workflows.
- [x] Rewrite README / README_EN as concise project entry points.
- [x] Move historical release notes to CHANGELOG.md.
- [x] Add THIRD_PARTY_NOTICES.md inventory.
- [x] Record repository/release non-commercial positioning.
- [x] Record that `.codex/` and `.claude/` remain public.
- [x] Organize public `docs/` into topic subdirectories and keep private notes untracked.
- [x] Record that OpenMoji/Remotion prototype assets are not distributed.
- [x] Record that assets are distributed with the non-commercial repository/release.
- [x] Add [LICENSE_SCOPE.md](../../LICENSE_SCOPE.md) for scheme A: project-owned source code under MIT, bundled assets/releases under non-commercial distribution notes.
- [x] Add [DOCS_PUBLICATION_AUDIT.md](../audit/DOCS_PUBLICATION_AUDIT.md) for tracked-docs publication review.
- [x] Review package contents against THIRD_PARTY_NOTICES.md after removing Remotion prototype files.
- [x] Ensure BASS runtime DLLs are explicitly tracked for the intended non-commercial Windows build.
- [x] Audit third-party build inputs from a clean-clone perspective: required vendored headers/libs/assets are tracked, and intentionally untracked FFmpeg binaries/SDKs have provisioning scripts and documented URLs.
- [x] Verify pinned FFmpeg download URLs are reachable: Windows export package, Windows QtAVPlayer dev SDK, macOS package, and FFmpeg 8.1.2 source mirrors.
- [x] Fix Windows export FFmpeg URL after the old Gyan.dev package path returned 404.
- [x] Update Windows/macOS build scripts to install the Qt ShaderTools module required by CMake.
- [x] Update Windows build script to provision both standalone export FFmpeg and the QtAVPlayer FFmpeg dev SDK.

## Owner Confirmation Required

- [x] Use scheme A: keep [LICENSE](../../LICENSE) as MIT for project-owned source code and document repository/release non-commercial scope separately.
- [x] Confirm BASS headers, import libs, and runtime DLLs stay in the non-commercial repository/release.
- [x] Confirm `assets/skin` is distributed.
- [x] Confirm `assets/SFX` is distributed.
- [x] Confirm intro visual assets/templates are distributed and reference gfdfdxc/maimai-transition.
- [x] Record that `assets/fonts/consola.ttf` is distributed with the non-commercial repository/release package.
- [x] Record that Xiaolai Mono is distributed and M PLUS was removed with the Remotion prototype.
- [x] Confirm OpenMoji / Remotion-related assets do not ship and are not kept in the repository.
- [x] Confirm Minepig/MaiMuriDX, gfdfdxc/maimai-transition, and MajdataPlay were behavior references only, with no copied source/assets.
- [x] Confirm TJAPlayer3 was not used; current preview video backend uses QtAVPlayer.

## History And Sensitive Data

- [x] Run a current-tree secret scan.
- [x] Run a full-history secret scan.
- [ ] Rotate any credential that ever appeared in Git history.
- [x] Generate a largest-object report for Git history.
- [x] Remove historical build outputs and binary blobs with `git-filter-repo` if public history must be clean.
- [x] Re-clone the filtered repository and verify build/package scripts from the cleaned history.
- [x] Force-push the filtered `main` and `test` histories after owner approval.

Current history scan result:

- Current tracked tree has no secret/local-path hits from the open-source scan patterns.
- Filtered history has no hits from the open-source secret/local-path scan patterns, excluding intentionally vendored third-party code.
- Filtered history removed historical build artifacts under `build-mingw-ascii/` / `build-mingw/`, probe files under `_audio_probe/`, generated slide data under `assets/generated/`, Remotion prototype files under `tools/intro_remotion/`, old FFmpeg backup files under `third_party/ffmpeg/windows/win64/dev.full.bak/`, old M PLUS font copies, and superseded private investigation docs/scripts.
- Largest remaining Git objects are current distribution assets and third-party/source files that are intentionally retained.
- Filtered `main` and `test` were force-pushed to GitHub on 2026-06-24; both point to `e150dfcd94c20877a0f212164040e3979e05f1c0`.
- A pre-filter bundle backup exists outside the repository; do not publish it.

## Branches And Tags

- [x] Make `main` the public default branch.
- [x] Keep `main` and `test` as the public branches.
- [x] Delete local experiment branches.
- [x] Delete remote experiment branches.
- [x] Old tags may remain public.

## Release Readiness

- [x] Build from a clean clone.
- [x] Package Windows release artifacts.
- [x] Generate SHA256 checksums.
- [x] Write release notes with known issues and license notes.
- [x] Keep the current first public version number.
- [x] Publish the first public release as a prerelease.

Current Windows smoke package:

- `dist/MiaCode-v0.5.2-beta3-win64.zip`
- SHA256: `50E6564E96866AF2B0D8C29EACCED1E9FC2684E8AC21F48104FD0EAD33B4BC57`
- Release notes draft: [RELEASE_NOTES_DRAFT_0.5.2-beta3.md](RELEASE_NOTES_DRAFT_0.5.2-beta3.md)

Clean-clone verification:

- Checked on 2026-06-24 from `D:/Desktop/maimuri/MiaCode-clean-build-check`.
- `scripts/build/build-win.ps1` completed successfully from a fresh local clone of `test`.
- The build script provisioned Qt 6.8.3, Windows export FFmpeg, and the Windows QtAVPlayer FFmpeg dev SDK from the documented sources.
- Verified package layout includes the wrapper `MiaCode.exe`, real app `app/MiaCode.exe`, FFmpeg at `app/ffmpeg/ffmpeg.exe`, BASS runtime DLLs, Qt/QML runtime files, `assets/`, `licenses/`, `LICENSE`, `LICENSE_SCOPE.md`, `THIRD_PARTY_NOTICES.md`, `README.md`, and `README_EN.md`.
- Smoke-ran the packaged wrapper with `--export-video`; it reached the app CLI and exited with the expected missing `--chart` argument error.

## Third-Party Download Verification

- Windows export FFmpeg:
  - x64 (Gyan.dev 7.1.1 essentials, 87 MB static GPL): `https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-essentials_build.7z`
    - Expected `ffmpeg.exe` SHA256: `B90225987BDD042CCA09A1EFB5E34E9848F2D1DBF5FBCD388753A44145522997`.
  - arm64 (BtbN n8.1 **GPL** static, pinned tag `autobuild-2026-09-12-13-12`):
    `https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-12-13-12/ffmpeg-n8.1.2-52-g5a03dfa0f6-winarm64-gpl-8.1.zip`
    - Expected `ffmpeg.exe` SHA256: `22E1BB241B8747ED5EA5ECE8DE64AFCC8720F4550ED35ED24657D00C2BADBA5E`.
  - Checked: both archives reachable; the arm64 `ffmpeg.exe` hash reproduced from the extracted binary.
- Windows QtAVPlayer FFmpeg dev SDK:
  - x64: built from FFmpeg source by `scripts/ffmpeg/trim/build-trimmed-ffmpeg.ps1` (run by
    `build-win.ps1` unless `-SkipTrim` is passed). It uses the FFmpeg 8.1.2 source tag and emits
    the same avcodec-62 ABI used by arm64.
  - arm64 (BtbN n8.1 LGPL **shared**, pinned immutable tag):
    `https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-12-13-12/ffmpeg-n8.1.2-52-g5a03dfa0f6-winarm64-lgpl-shared-8.1.zip`
    - Expected archive SHA256: `DFB3F394B316F91CC2C399BBAA38A280F81E523E150A20F7DDD477843AA70608`.
  - Checked: both archives reachable. The n8.1 win64/winarm64 payloads are byte-identical to the
    rolling `latest` tag of the same day, while the archive hashes differ (the embedded top-level
    directory name differs, so the hash is tag-bound).
- macOS export FFmpeg: `https://ffmpeg.martin-riedl.de/download/macos/arm64/1783011502_8.1.2/ffmpeg.zip`
  - Expected SHA256: `EAF91238E104DD0E262BC6510E25061855CC99A6955A721B0AC99660D58C473D`.
- FFmpeg preview source mirrors for tag `n8.1.2`:
  - `https://gitee.com/mirrors/ffmpeg.git`
  - `https://github.com/FFmpeg/FFmpeg.git`
  - `https://git.ffmpeg.org/ffmpeg.git`
  - GitHub returned `refs/tags/n8.1.2` at `1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c`.
  - The official source archive SHA256 is `464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c`.
