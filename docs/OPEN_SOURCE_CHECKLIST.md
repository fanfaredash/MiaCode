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
- [x] Record that `docs/` will be decided later.
- [x] Record that OpenMoji/Remotion prototype assets are not distributed.
- [x] Record that assets are distributed with the non-commercial repository/release.
- [ ] Review package contents against THIRD_PARTY_NOTICES.md after removing Remotion prototype files.

## Owner Confirmation Required

- [ ] Decide whether [LICENSE](../LICENSE) should be replaced or amended to match the non-commercial positioning.
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

- [ ] Run a current-tree secret scan.
- [ ] Run a full-history secret scan.
- [ ] Rotate any credential that ever appeared in Git history.
- [ ] Generate a largest-object report for Git history.
- [ ] Remove historical build outputs and binary blobs with `git-filter-repo` if public history must be clean.
- [ ] Re-clone the filtered repository and verify build/package scripts from the cleaned history.

## Branches And Tags

- [ ] Make `main` the public default branch.
- [x] Keep only `main` as the public branch.
- [ ] Delete local experiment branches.
- [ ] Delete remote experiment branches.
- [x] Old tags may remain public.

## Release Readiness

- [ ] Build from a clean clone.
- [ ] Package Windows release artifacts.
- [ ] Generate SHA256 checksums.
- [ ] Write release notes with known issues and license notes.
- [x] Keep the current first public version number.
- [x] Publish the first public release as a prerelease.
