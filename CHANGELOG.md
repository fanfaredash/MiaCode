# Changelog

This file collects historical release notes. The project is still preparing for public open-source releases, so older version names may not map one-to-one to future GitHub releases.

## 0.5.2-beta3

First public prerelease candidate, prepared from the current Windows-focused workflow.

### Highlights

- Added the Qt Quick preview and offscreen export pipeline used by the current realtime preview, export-page preview, and video-export flow.
- Added intro-template support, including the maimai-style transition, track-start jingle, negative-time intro preview region, and optional `clock_count` count-in.
- Added mine-note (`m`) parsing/rendering and negative high-speed (`<HS*-N>`) reverse-flow support.
- Added QtAVPlayer-based preview decoding with hardware/software decode preference and live switching.
- Added Net public-chart batch downloader and batch transform actions surfaced in the editor context menu.
- Added cover export, partial export, batch export, ZIP packaging helpers, Muri diagnostics, latency tools, and crash-recovery/session-marker improvements.

### Build And Packaging

- Prepared the repository for a non-commercial public prerelease with cleaned docs, release checklists, third-party notices, license-scope notes, and branch/history cleanup records.
- Removed obsolete GitHub Actions and moved release packaging to local scripts.
- Reorganized public scripts under `scripts/build`, `scripts/ffmpeg`, `scripts/debug`, and `scripts/dev`.
- Updated Windows build scripts to provision Qt modules, the pinned FFmpeg export binary, and the QtAVPlayer FFmpeg dev SDK from a clean clone.
- Updated packaging to include Qt Quick/QML runtime files, BASS runtime DLLs, FFmpeg, assets, license files, notices, and README files.
- Verified `MiaCode-v0.5.2-beta3-win64.zip` with SHA256 `B86CD85E65E95CAB155734A90EAA4ED20BD26D822410D38A108853F02CF0030A`.

### Fixes And Polish

- Fixed empty numeric metadata serialization, including the bare `&first=` case.
- Fixed welcome/metadata page relayout, export-page layout settling, fullscreen export-page handling, and IME preference text.
- Fixed export and preview synchronization around intro playback, BGM seeking, `clock_count`, and selected export ranges.
- Fixed timeline and transform edge cases around BPM changes, subdivision grids, difficulty badges, and preview resource cleanup.
- Split several large main-window, preview, export, logging, timeline, audio, and chart files into smaller translation units.

## 3.0

- Improved export stability.
- Added note scroll-speed control.
- Added a dark UI theme.
- Added break touch support.

## 0.2.1

- Fixed ffmpeg compositing cadence to eliminate periodic duplicate-frame stutter during export.
- Fixed timeline follow-preview behavior so the editor cursor is only controlled during playback.

## 0.2.0

### Simai Text Editor

- Added Ctrl+F find and replace.
- Added adjustable font size and line spacing.
- Added simai syntax highlighting.

### BPM And Offset Detection

- Added automatic BPM and delay detection.
- Reduced the need for chart creators to measure BPM and delay by hand.

### Chart Video Export

- Added full chart video export.
- Added partial clip export.
- Made it easier to share short chart previews.

### Syntax Validation

- Added chart syntax-error detection.
- Added warnings for potential compatibility issues.
- Helped avoid parsing problems on non-Maj platforms.

### Feature Completion

- Added judgment effect animations for multiple note types.
- Added firework rendering effects.

## 0.1.1

- UI polish.
- Added application icon.

## 0.1.0

### Simai Editing

- Added simai parsing and editing workflow support.
- Added multi-difficulty field management, including add, delete, and auto-switch.
- Added batch mirror and rotate operations.
- Added difficulty-page and chart-metadata settings.

### Rendering And Preview

- Upgraded preview controls to a player-style workflow with timeline and speed options.
- Added editor, timeline, and preview playback synchronization.

### Other

- Added Chinese and English UI support.
