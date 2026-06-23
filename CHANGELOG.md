# Changelog

This file collects historical release notes reconstructed from the repository commit history. Some early and internal prerelease version names were used for local validation builds, so adjacent beta/dev bumps are grouped when they belong to the same user-facing release line.

## 0.5.2-beta3

First public prerelease candidate, focused on repository publication and Windows package reproducibility.

### Public Release Preparation

- Prepared the repository for a non-commercial public prerelease with clarified license scope, third-party notices, release checklists, and publication audits.
- Reorganized public documentation and scripts for the open-source tree.
- Removed obsolete GitHub Actions and moved release packaging to local scripts.
- Recorded history-cleaning, branch-cleaning, and reproducible dependency setup notes.
- Added README avatar/community QR assets and refreshed README acknowledgement wording.

### Build And Packaging

- Updated Windows build documentation and scripts around pinned FFmpeg provisioning and QtAVPlayer FFmpeg dev SDK setup.
- Fixed the pinned Windows FFmpeg download URL.
- Kept release packaging centered on local Windows prerelease artifacts.
- Verified `MiaCode-v0.5.2-beta3-win64.zip` with SHA256 `B86CD85E65E95CAB155734A90EAA4ED20BD26D822410D38A108853F02CF0030A`.

### Fixes

- Fixed empty numeric metadata serialization, including the bare `&first=` case.
- Fixed welcome/metadata page relayout and wording around the Chinese IME setting.
- Removed stray skinSTD SlideOK fast-gd sprites.
- Adjusted preview transport layout for the prerelease branch.

## 0.5.2-beta

Preview, export-page, chart-format, and refactor-heavy beta line leading into the public prerelease.

### Highlights

- Added hardware/software video-decode preference with live switching for the QtAVPlayer preview backend.
- Added hardware-decode stability work, NV12 diagnostics, and post-seek green-frame ordering fixes.
- Added mine-note (`m`) support and negative high-speed (`<HS*-N>`) reverse-flow behavior.
- Added Net public-chart batch downloader.
- Added the Xiaolai Mono CJK HUD font subset and the related subsetting toolchain.
- Added intro-switch UI, welcome-page IME setting, and preferences schema v4.

### Export And Intro

- Added negative-time scrub-able intro preview on the export page.
- Added playable export-page preview wiring, animated intro picture/sound, and editor-like preview controls.
- Added `clock_count` count-in controls, preview audio wiring, and export gating.
- Added range-track export controls, range-duration captions, and export-sidebar busy spinner feedback.
- Added export hub/page layout redesign with embedded video panel and fixed-frame surface behavior.
- Fixed BGM/transport synchronization around intro seeks and export-page audition resume.
- Fixed stale intro negative-time regions and clock-count duplication.

### Chart, Timeline, And Tools

- Added batch transform actions inline in the editor context menu.
- Added x3 fallback for raise-subdivision half-step transforms.
- Allowed leading whitespace before `&key=` metadata and right-trimmed metadata values.
- Fixed BPM-default discovery for latency detection and moved chart parameters into the latency workflow.
- Added same-lane `v` slide support (`1v1` through `8v8`).
- Improved crash-recovery session markers for multiple concurrent windows.

### Refactors And Polish

- Split large main-window, preview, export, logging, timeline, audio, chart, and app files into smaller translation units.
- Split `slide_data` into its own Qt resource and trimmed release package contents.
- Added skinDX mine-note sprites and relocated slide reference data under `assets/reference`.
- Improved logging record format, rotation behavior, diagnostics split, and debug flag caching.
- Fixed preview firework warm-up, clear-elements behavior, find bar, MP3 picker, fullscreen speed, and export-page layout settling.

## 0.5.1-beta

Cover-export, intro, welcome, timeline, parser, and recovery beta line.

- Added first-run setup dialog for preview side/theme selection.
- Added WYSIWYG cover composer, chart-frame layer editing, layout save/import, and cover-export transport polish.
- Added chart-frame inner-ring background and in-process frame picker support.
- Added pause-display hold-key rebinding.
- Added `clock_count` metadata and detection UI groundwork.
- Added export effect preview and fixed export preview slider scrubbing.
- Added parser warnings for missing commas before directives and stricter headed slide-branch validation.
- Added 3.0x timeline zoom preset and tiered grid-line heights.
- Fixed range exports that start at chart 0, preview debug HUD behavior, resource gauges, preferences sizing, and recovery prompts.
- Centralized native window theming and aligned crash recovery with backup restore flow.

## 0.5.0-beta

Intro, export dialog, preview video, HUD, and packaging beta line.

- Added maimai-style track-start intro overlay, export banner data, animated card reveal, backdrop texture, and CLI intro tooling.
- Composited the track-start intro into chart export with opening SFX.
- Added Resource Han Rounded fonts for intro/card rendering and shipped their OFL license in packaging.
- Added difficulty banner still-image export and backend-agnostic offscreen cover rendering.
- Added QtAVPlayer/FFmpeg preview video backend and decode-only FFmpeg trimming work.
- Reworked the video-export dialog into Output/Video/Gameplay/Font/Range style tabs and later merged HUD controls into Visuals.
- Added export quality toggle groundwork and intro background fade.
- Added preview HUD center display and per-type note totals.
- Split slide-shape rings below judge text and added scale-pop/break-flash judge effects.
- Fixed parser handling for dangling each separators and per-segment slide timing.
- Fixed intro visual seams, card-frame crops, jacket-frame pixel snapping, export readback crashes, and alpha flattening after intro fade-in.

## 0.4.0-beta series

Large internal beta series that moved MiaCode from the older preview/export stack toward the modern Quick Shell, BASS audio, DComp/HWND experiments, packaged Windows runtime, export-page tooling, and the first maimai-style intro pipeline.

### beta1-beta7: Quick Shell, Preview, And Early Packaging

- Prepared the first `0.4.0-beta` branch after the `0.3.11` Quick Shell stabilization work.
- Anchored Windows packaging paths to the repository root so scripts could be launched from outside the repo.
- Added quick-shell preview host diagnostics, higher-precision preview tick scheduling, and fullscreen transport-overlay darkening.
- Added global and per-channel preview mute controls.
- Improved paused-preview follow behavior, preview seek behavior for mislabeled audio files, and slide-motion angle interpolation.
- Added selection-based chart formatting and improved timeline segment anchors around inline terminal markers.
- Added cached column waveform rendering and early negative-time preview/Muri anchor handling.
- Made preview follow an app preference and preserved the package root folder in zip output.
- Removed the high-compression export preset and refined preview FPS diagnostics/logging.

### beta9-beta19: Export Runtime, BASS Audio, And Retained Preview State

- Refined export runtime policy, worker pipeline, filter handling, static background preprocessing, and preview/export ratio/HUD typography.
- Unified preview playback timing authority and extracted shared preview SFX playback scheduling.
- Added BASS preview backend, BASS export audio pipeline, shared timing settings, and Windows package BASS runtime deployment.
- Added retained BASS preview transport and retained preview playback state across timeline/export flows.
- Added tap/touch flow speed split, break-slide tail filtering, independent break-slide audio bucket behavior, and hold-tail judge SFX.
- Added 1.5x realtime preview speed option and playback-rate toast overlay.
- Added Quick Shell close/startup visibility fixes, stop/pause handling fixes, and preview-stage media recovery.
- Added chart formatting 384-snap preference and preserved merged slide structures through the release tail.
- Added Muri and timeline fixes for sub-tick wifi touches, head-star handling, collision targets, tooltip hit testing, synthetic slide heads, and static panel dedupe.
- Began present-driven frame pacing experiments, async logging, MMCSS registration, QSG timing capture, and DComp smoke tests.

### beta20-beta29: DComp/HWND Preview, Renderer Refactor, And Operation Logs

- Merged the large v2 refactor branch and made the DComp preview path the default experiment for the beta20 line.
- Restructured preview/video code toward a compositor/source model, with chart-side and HUD-side preview sources.
- Added D3D11/DXGI/DComp render-thread work, snapshot queues, fixed-nominal playhead timing, sprite/HUD/background pipelines, and device-removal/visibility recovery experiments.
- Added timeline DComp/HWND experiments, GPU pipelines for timeline geometry, label caching, sprite asset caching, and geometry/lifecycle diagnostics.
- Added crash-time autosave, recovery prompts, and package dependency audits.
- Removed dead libmpv package content and unified prerelease version variables.
- Added video-export audio bitrate dropdown and several export layout refinements.
- Fixed timeline grid visibility by promoting bar/note lines into dedicated z slots and palette entries.
- Decoupled grammar diagnostics from lenient parsing and added strict-only checks for misplaced modifiers and slide-chain syntax.
- Added autosave storage under `.miacode`, compact x264 retuning, worker-thread readback conversion, zero-copy frame handoff groundwork, and CPU-built mip-chain experiments.
- Added preview audio anchoring and DComp/audio synchronization fixes.
- Added scene-alignment refinements for pad distances, slide-OK alpha, judge-text alpha, touch border stacking, and judge-effect toggles.
- Added independent timeline follow controls, cursor-follow highlight behavior, and timeline/quick tick/header fixes.
- Added operation breadcrumb logging across document I/O, parser/transform, audio, render submission, preview QML bootstrap, IPC, worker processes, and export controller paths.
- Added cross-process crash-log discovery, hard-crash shadow buffers, and process-isolated preview worker experiments with in-process fallback.

### beta31-beta41: User Workflow, Packaging Wrapper, And Editor Tools

- Defaulted the timeline to the HWND path during the beta31 line while keeping the QML path as a fallback.
- Fixed preview-worker teardown, paused-worker window tracking, break-slide audio volume, worker Muri slide erasure, and partial-export overlay preload timing.
- Added the first QML intro video prototype and `&clock_count` count-in SFX for full-range exports.
- Added preview canvas/timeline frame-rate preferences and rendered playfield/HUD countdown during export lead-in.
- Added selected-subdivision adjustment actions, custom skin/outline imports, startup-folder drag/drop opening, and view-state preservation across difficulties.
- Reworked latency detection so offset detection reports a result popup instead of auto-writing immediately.
- Consolidated latency detector transport/SFX/zoom controls and moved follow/view-lock options into a gear-menu workflow.
- Added editor overwrite mode, block cursor behavior, inline Follow Code toggle, backup restore menu, and renamed Preview menu audio/video entries.
- Added BGM peak-normalization scans for live preview and export backends.
- Hid runtime DLLs under `app/` behind a wrapper executable and embedded the app icon in the wrapper.
- Added toolbox audio/video media utilities, editable shortcuts, bookmark management, track metadata import from `track.mp3` ID3 tags, and preview speed/subdivision fixes.
- Fixed `<HS*N>` high-speed multiplier parsing/rendering and chained slide rotation with matched bracket groups.
- Restored text drag-and-drop inside the editor and hardened preview/scrub behavior around rate restore and paused view-lock interactions.

### beta42-beta50: Startup Diagnostics And Preview Audio Overhaul

- Investigated Windows 10 22H2 silent startup crashes and added startup crash diagnostics.
- Added QtPluginDiag launcher support for platform-plugin startup races.
- Bundled the VC++ runtime app-locally and added DLL-version probing.
- Added bilingual launcher preflight diagnostics and simplified-Chinese MessageBox copy for missing DLL cases.
- Reworked BASS error reporting and public-entry operation scopes for preview audio diagnostics.
- Rebuilt preview audio synchronization around sample-flag pause/resume, tempo write-once behavior, master-always-on routing, and playback-rate changes via pause-modify-resume.
- Removed stale scheduling surfaces after the audio overhaul and restored key validation/status log lines.
- Added beta50 follow-up fixes for touch-hold pause, stop/entry seeking, BGM cursor anchoring, and SFX event cursor re-anchoring.
- Simplified chart-normalization snap behavior after testing table/cache approaches.

### beta51-beta59: Metadata, Lead-In Export, Latency Page, And Editor Polish

- Added bare touch-hold parsing as zero-duration holds and made zero-duration touch-holds survive renderer layer culling.
- Added partial-range export lead-in behavior as a 1.5s frozen chart/HUD/audio preload.
- Added a project preference for “all difficulties share same designer,” with dark-mode-safe dialogs, checkmark glyphs, seeding behavior, and default-off load behavior.
- Added chart-normalization improvements for slide-segment break branches and trailing `{N}` emission.
- Rebuilt preview media players on rate change and added sync beacon trails for diagnosis.
- Added project toggle for Muri issue prompts, saved preview-skin restore on startup, and square-fit background scaling with video watchdog hardening.
- Added exception/register dumps for SEH/terminate/signal failures and fixed export PBO tearing with `glFenceSync`.
- Routed full-range export lead-in through negative chart time and reworked static background target rectangles/square-fit filter chains.
- Added timeline cursor marker triangle, top-left chart-info HUD, native multi-folder batch picker, and numbered chart lists.
- Refactored BPM/latency tooling into a left-sidebar page with sandbox audition and better theme refresh.
- Added render-settings tabs, issue-icon jumps, editor auto-pairs, bottom-tabs height persistence, bracket highlight propagation, and IME-committed bracket pairing.
- Fixed preview/timeline hold duration mismatch, main-preview reuse for BPM-page audition, touch/firework alignment, slide-track break flash timing, and simultaneous-touch Muri gating.
- Added bracket-completion dropdown, Audio/Video Processing button for latency, latest-wins touch-hold SFX ownership, undo/redo hold behavior, export preview PV/BG visibility, and export ZIP packaging.

### beta62-beta63: Intro, QtAVPlayer Preview, And Export Dialog Redesign

- Added maimai-style track-start intro overlay assets and export banner data.
- Composited the track-start intro into chart export with opening SFX.
- Added QtAVPlayer/FFmpeg preview video backend and decode-only FFmpeg trim tooling.
- Reworked the export dialog into tabbed Output/Visuals/HUD and then Output/Video/Gameplay/Font/Range layouts.
- Added export quality toggle groundwork, PBO readback options, and intro background fade.
- Fixed parser handling for dangling each separators and equivalent total slide timing.
- Fixed preview `pv.mp4` handle release for media-tool operations.
- Added cancelable/size-gated FFmpeg media-tool operations and latency playhead parity.
- Added two-tier bottom-tab scaling, per-difficulty offset/designer dialogs, and harder Windows FFmpeg dev download handling.
- Aligned track-start pre-roll timing, fixed transition visuals, and added CLI intro iteration support.
- Added staggered per-part intro card reveal, animated backdrop texture, difficulty banner still export, preview-seconds export cap for intro iteration, and center-display/per-type note totals.

## 0.3.11

Quick Shell stabilization release.

- Released Quick Shell as the active preview shell path.
- Refined quick-shell startup, layout, fullscreen behavior, shortcut forwarding, and header-summary behavior.
- Added focus behavior for paused preview on gameplay lanes.
- Refined autosave anchors and behavior.
- Normalized hold and slide durations to the 384-grid model.
- Added touch exclusion for multi-touch analysis.
- Deduplicated same-head slide preview heads and fixed stale/misattributed Muri overlap anchors.
- Improved syntax issue display and preview parser marker matching.

## 0.3.10-dev series

Hybrid Quick frontend and platform-host migration work.

- Added quick-shell beta host mode and hybrid debug packaging.
- Split preview background media by host route.
- Moved GUI runtime toward a platform-backend abstraction.
- Refined timeline anchors, line-number scaling, UI cadence, forced labeled outline strategy, and generated labeled outline assets.
- Improved Quick preview rendering/startup restore, background profiling, resize throttling, and object-stat throttling.
- Added autosave/UI-change groundwork and DX skin variant support.

## 0.3.9

Qt Quick realtime preview and export migration release.

- Migrated the preview runtime host to Qt Quick scene graph.
- Migrated guide, touch, touch-hold, head, and early preview batches to Qt Quick.
- Moved preview runtime and export to Qt Quick.
- Fixed Qt Quick export overlay compositing.
- Packaged the Qt Quick export path for release.
- Restored firework fade semantics in the Quick preview.
- Added prepared note windows for Quick preview layers.
- Fixed offscreen readback unpremultiplication and timeline each rendering.

## 0.3.8-dev series

Preview runtime, Muri, timing, and export-preset development line.

- Added slide delay and head material semantics with documentation/tests.
- Hardened GL media fallback and preview runtime diagnostics.
- Improved preview play/pause behavior after timeline and slider focus.
- Refined long-slide Muri semantics, protected slide-collision severity, and full chained-slide labels in Muri output.
- Improved preview seek controls, export dialog preferences, and timeline rendering.
- Unified timing metadata and added chart formatting tools.
- Added export presets and preview comparison diagnostics.

## 0.3.7-dev series

Timeline, transform, Muri, and preview performance development line.

- Added parse-level chart transform regression specs.
- Split quick and slow timeline refresh paths.
- Fixed parser/comment handling for strict checks and editor highlighting.
- Preserved timing prefixes before braces and unmatched selection edges in transforms.
- Corrected slide each coloring, duplicate hold endcaps, and each-guide grouping.
- Added live offset edit/restore behavior in latency tooling.
- Preferred background media over embedded track art.
- Deferred preview resume and analysis UI work to reduce stalls.
- Refactored preview/audio SFX synchronization and prepared SFX state.

## 0.3.5-dev series

Export, audio, validation, and UI development line.

- Added batch export worker flow and improved batch export persistence.
- Added default preview audio-level persistence.
- Added hold and break shine note effects.
- Added alt-wheel timeline zoom and fixed zoom tiers.
- Added split diagnostics for fullwidth syntax mistakes and invalid notes.
- Improved export worker and ffmpeg failure diagnostics.
- Added Wi-Fi render option persistence and threaded that option through Muri analysis.
- Added side-panel swap experiments and themed preview panel reuse.

## 0.3.3

Validation, export, and preview release line.

- Added automatic validation on open/edit and restored auto-check behavior.
- Added Muri issues into the header summary and refined validation list priority.
- Added isolated video export process, refined progress UI, 120 FPS export option, performance profile, and faster cancel.
- Added undo support after deleting a difficulty.
- Added editable flow speed with quarter-step snapping.
- Added rotating slide head stars.
- Added interruptible firework effects and pause-on-timeline interaction.
- Added persistent export settings and HUD scaling with export resolution.
- Fixed preview refreshes after batch transforms, Unicode BGM paths, touch-hold each visuals, parser hold variants, and paused BGM pre-seek.

## 0.3.1

Preview/export polish and workflow stabilization line.

- Restored media sync and cursor-follow behavior in preview.
- Refined preview workflow, settings, layout, sizing, bottom-tab behavior, and taskbar icon behavior.
- Unified preview and export SFX asset lookup.
- Backported export rendering/audio support and enabled PBO readback by default.
- Added isolated video-export design work and improved export defaults/output naming.
- Added syntax validation summary polish and offset-sign alignment.
- Fixed black screens after export dialogs, false dirty states, preview audio bursts during save/edit, and selected preview FPS retention.

## 0.3.0

Formerly recorded as `3.0` in this changelog; the historical version bump is `0.3.0`.

- Improved export stability and hardened FFmpeg validation.
- Added CLI export entry points and export tracking updates.
- Added runtime encoder probing and safer software threading.
- Dropped `ffprobe` from release artifacts and pinned FFmpeg binaries with archive/version validation.
- Added widescreen export layouts, live preview aspect controls, and export brightness controls.
- Defaulted to H.264 export with PBO fallback.
- Fixed multiline paste spacing and slide-touch each grouping semantics.
- Added timeline header click-to-jump behavior synchronized with the editor.
- Added last-opened-file restore with a user preference.
- Added break touch parsing/preview support, touch border overlap rendering, dark theme support, and editor/menu polish.

## 0.2.2-dev series

Development line between `0.2.1` and `0.3.0`.

- Refined preview/export rendering and centralized shared constants.
- Added export dialog preview-state synchronization and preview layout/settings workflow polish.
- Preserved preview position when reinitializing the BGM track.
- Hardened export defaults and break-touch stats.
- Removed legacy bridge docs and unused export temp-dir helper.

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
