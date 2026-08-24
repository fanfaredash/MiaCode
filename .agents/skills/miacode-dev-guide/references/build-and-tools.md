# Build & Tools

Build configurations, targets, the dev-tools/spec convention, scripts, packaging, assets, and
helper binaries. Build file: root `CMakeLists.txt` (one file, ~1200 lines). Presets:
`CMakePresets.json` (configure `vs2022-qt6` → `build/`; build presets `release`, `debug`).

## 1. Build configuration policy

- **Release is the only configuration that matters for routine work.** Build / test / verify in
  Release (`--config Release`). Do not create or maintain a separate Debug build just to get
  diagnostics — debug behavior is a **runtime `--debug` flag** (see `debug-and-logging.md`).
- Debug-specific CMake handling that exists today is standard MSVC/windeployqt boilerplate
  (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` at `CMakeLists.txt:36`; launcher runtime lib at `:565`;
  windeployqt `$<CONFIG:Debug>` at `:1179`/`:1190`). Leave it unless it gets in the way; it is
  not a second supported product config.
- Generator is multi-config (Visual Studio 17 2022), so CTest needs `-C Release`.
- **macOS compatibility follows `dev-macos`.** Keep Windows-only BASS export and D3D11 sources
  inside `if(WIN32)`; guard their owning members/includes/call sites with `Q_OS_WIN`. Realtime
  preview BASS is enabled on Windows, macOS, and Linux when `MIACODE_HAS_BASS_AUDIO` is defined;
  miniaudio is the compatibility backend for builds without that definition.
  Published preview snapshots use `std::shared_ptr<const PreviewFrameState>` plus the free
  `std::atomic_load_explicit` / `std::atomic_store_explicit` overloads — do not replace this with
  `std::atomic<std::shared_ptr<...>>`, which is rejected by the supported macOS libc++ toolchain.
  For memory-constrained local macOS builds, use an explicit low bound such as `--parallel 2`.

## 2. Targets

Default build (no options) produces only: **`MiaCode`** (the app), **`soundtouch`** (static lib
linked into MiaCode), **`miniz`** (static lib, vendored `third_party/miniz/` single-file ZIP
writer for "Export as ZIP" — no new DLL), and on Windows **`MiaCodeLauncher`** (the dist-root
launcher exe that forwards to `app/MiaCode.exe`).

> Note: the project enables **`LANGUAGES C CXX`** (not just CXX) so the vendored `miniz.c`
> compiles. With CXX-only, CMake silently demotes `.c` sources to `<None>` and the link fails
> with unresolved `mz_zip_*` symbols. The `miniz` target has AUTOMOC/UIC/RCC turned off (plain C).

Everything else is gated behind `option(MIACODE_BUILD_DEV_TOOLS … OFF)` (`CMakeLists.txt:48`,
block at `:586`). These are dev/diagnostic/spec binaries, off by default:

- Dumps/probes: `miacode_muri_dump`, `simai_native_dump`, `soundtouch_probe`,
  `latency_offset_batch` (batch offset-detection evaluator: walks a chart corpus, scores
  `detectOffset` vs each chart's `&first` folded mod one 8th-note; `DetectionTuning` weights
  exposed as CLI flags. Manual diagnostic — needs a corpus, so NOT a CTest case.)
- Specs (standalone `main()` style): `oplog_self_test`, `simai_parser_spec`,
  `simai_document_spec` (SimaiDocument designer model — standalone chart-less `&des_N` round-trip),
  `chart_batch_transform_spec`, `muri_spec`, `timeline_model_spec`, `plain_code_editor_spec`,
  `preview_asset_loader_spec`, `preview_firework_lifecycle_spec`,
  `preview_end_of_media_policy_spec` (background-video `EndOfMedia` classification:
  natural short PV vs stale mid-clip end, measured against the media's own duration),
  `preview_head_layer_spec`,
  `preview_realtime_object_hot_path_spec`, `preview_quick_sprite_batch_spec`,
  `preview_sfx_timeline_spec`, `preview_audio_settings_spec`,
  `preview_audio_command_queue_spec`, `preview_audio_worker_protocol_spec`,
  `preview_audio_worker_spec` (also compile-checks the production miniaudio backend and, on
  Windows/macOS, the production BASS backend), `preview_audio_non_gui_barrier_spec`,
  `preview_bass_device_lease_spec`, `preview_audio_playback_flow_policy_spec`,
  `preview_audio_device_change_policy_spec`, `preview_audio_health_spec`,
  `bass_preview_retained_state_spec`, `bass_preview_debug_log_routing_spec`,
  `bass_preview_sfx_scheduler_policy_spec`, `ui_hang_watchdog_policy_spec`,
  `log_pruning_policy_spec`, `quickshell_preview_surface_policy_spec`,
  `video_export_runtime_policy_spec`, `video_export_intro_mode_spec`,
  `video_export_audio_render_plan_spec`,
  `touch_pad_authoring_state_spec`,
  `chart_zip_packager_spec` (verifies the Export-as-ZIP packager against real zip read-back),
  `debug_flag_index_spec` (drift guard — every `MIACODE_*` flag read in `src/` must appear in
  `docs/ops/DEBUG_INDEX.md`, and every flag the doc names must still be read in code or be in the
  spec's retired allowlist; repo root injected via a `MIACODE_SOURCE_ROOT` compile define. Note:
  `MIACODE_SOURCE_ROOT` itself is a compile def, not an env flag, so the spec filters it via
  `kCompileDefinitions` — any new spec that consumes the source-root define is fine),
  `process_diagnostics_spec` (DebugLog PV-memory channel/session/path and current-process memory
  sample contract), `pv_memory_diagnostics_spec` (pure PV source/clear-epoch accounting), and
  `pv_memory_host_contract_spec` (source-level guard that host instrumentation stays debug-gated
  and reuses the single existing `toImage()` conversion),
  `ui_text_locale_spec` (i18n drift guard — see the localization note in
  `architecture-and-layout.md`; also uses the `MIACODE_SOURCE_ROOT` compile define),
  `ui_text_preferences_spec` (canonical preference normalization/migration and unknown-key
  preservation guard)

## 3. Spec / dev-tool convention (audit 2026-05-29 — being standardized)

Findings the convention is fixing:

- The `*_spec` targets are now registered with CTest (`enable_testing()` + `add_test()` per
  spec), so `ctest -C Release` runs the suite. Before this they were standalone mains nobody ran.
- Specs are declared via the helper **`miacode_add_dev_tool(NAME TEST SOURCES … LIBS … INCLUDES …)`**
  (CMakeLists.txt ~636) instead of a copy-pasted `add_executable` + `target_link_libraries` +
  `target_include_directories` triple. The `TEST` keyword registers the `add_test()` + the Qt-bin
  `ENVIRONMENT_MODIFICATION` PATH fix. Shared source groups (`_miacode_chart_core`,
  `_miacode_log_core`, …) are set once and reused.

Rules going forward:

- A new spec = one `miacode_add_dev_tool(NAME TEST ...)` call inside the `MIACODE_BUILD_DEV_TOOLS`
  block. It is auto-registered with CTest. Do not hand-write the three-call boilerplate. (Need a
  compile define, e.g. a source-root path? Append a `target_compile_definitions(NAME PRIVATE …)`
  after the call — see `debug_flag_index_spec`.)
- Reuse the shared source-group variables; only list sources unique to that spec.
- Build the suite with `cmake --build build --config Release` after configuring with
  `-DMIACODE_BUILD_DEV_TOOLS=ON`, then `ctest --test-dir build -C Release`.

## 4. Build & packaging scripts (`scripts/`, whitelisted in `.gitignore`)

- Windows build/package: `scripts/build/build-win.ps1`, `scripts/build/package-win.ps1` (defaults to `build/`, prechecks
  version freshness against `CMakeLists.txt` + generated `AppVersion.h`, auto-rebuilds MiaCode,
  runs windeployqt with `--qmldir src`, keeps the Qt Quick DLL set, copies repo-local BASS DLLs).
- macOS build/package: `scripts/build/build-macos.sh`, `scripts/build/package-mac.sh`.
- Linux build/package: `scripts/build/build-linux.sh`, `scripts/build/package-linux.sh`. The wrapper
  builds with 8 jobs by default in a fixed Ubuntu 22.04 / Qt 6.11.1 Docker or Podman image and
  clears `dist/`, then emits `MiaCode-v<version>-linux-x86_64/` plus a matching `tar.gz`.
  `MiaCode.AppImage` bundles Qt/QML, the C++ runtime, XCB Fcitx/IBus input plugins, assets,
  extensions, and the pinned export FFmpeg; release documentation and license notices sit beside
  the AppImage in the version directory. Incremental build, packaging, and download files stay
  under `build/`; glibc, the loader, X11, and graphics drivers remain system-provided.
- ffmpeg provisioning: `scripts/ffmpeg/ensure-windows-ffmpeg.ps1`, `scripts/ffmpeg/ensure-macos-ffmpeg.sh`,
  `scripts/ffmpeg/ensure-linux-ffmpeg.sh` (the standalone
  `ffmpeg` executable used by **export**).
- ffmpeg **dev SDK** provisioning (Windows): `scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1` downloads the BtbN
  n7.1 LGPL *shared* build (headers + import libs + runtime DLLs) into
  `third_party/ffmpeg/windows/dev/` for the **QtAVPlayer preview decode backend**. On macOS,
  `package-mac.sh` accepts `MIACODE_FFMPEG_DEV_DIR` or finds Homebrew `ffmpeg@6` (then `ffmpeg`).
  Both are CMake inputs, not runtime flags; the SDK root must contain `include/` and `lib/`.
  Linux developer builds use pkg-config from the host toolchain for FFmpeg, libva, libva-drm,
  and libdrm; `PKG_CONFIG_PATH` selects a non-system installation.
- Public debug launchers: `scripts/debug/Start_MiaCode_Debug.bat`, `scripts/debug/Start_MiaCode_SoftwareVideoDecode.bat`,
  and `scripts/debug/Start_MiaCode_QtPluginDiag.bat`. One-off A/B launchers stay as ignored maintainer-local
  tools unless they become part of a repeatable public support workflow.

## 5. Helper binaries (behind `MIACODE_BUILD_DEV_TOOLS`)

- `miacode_muri_dump`, `simai_native_dump`, `soundtouch_probe`, `latency_offset_batch` — CLI diagnostics.
- `package-win.ps1` currently packages only `simai_native_dump.exe` as a Windows dev tool.
- `soundtouch_probe` always builds the Miniaudio/SoundTouch path; only Windows adds the
  `BassPreviewAudioBackend` sources, BASS headers, import libraries, and runtime deployment.
- BASS runtime DLLs are copied post-build for `MiaCode` (and `soundtouch_probe`) from
  `third_party/bass/bin/win64/`.

## 6. Assets & runtime file conventions

- Asset root resolution: `src/common/AssetPaths.h` (`findAssetRoot`, `assetPath`).
- Asset areas: `assets/skin` (`skinSTD`/`skinDX` + user skins), `assets/SFX`,
  `assets/background` (+ `outlines/` custom judge-line PNGs), `assets/noteguide`,
  `assets/reference` (`slide_data.json`), `assets/fonts`.
- `slide_data.json` is the single source of slide-shape support (parser "unknown shape"
  errors, preview geometry, and Muri judge data all key into it; embedded via
  `resources/slide_data.qrc` as `:/data/slide_data.json` — touch the qrc after editing the
  json, AUTORCC misses the dep). Same-lane v slides (`1v1`..`8v8`, out to center and
  back) are an editor extension spliced from the MajdataPlay-dump donor entries by
  `scripts/assets/gen_same_lane_v_slides.py` (idempotent); opposite-lane `Xv(X+4)` stays
  unsupported on purpose — it is identical to the straight slide `X-(X+4)`. Return-leg
  track arrows mirror the inbound arrows IN PLACE (same positions, flipped rotation)
  instead of continuing the equidistant generation — staggered opposite arrows on the
  shared segment read as a broken track. Data conventions the script relies on: arrow
  arrays are stored in reverse travel order (a C area lists its outbound arrows first),
  rotation is uniform per straight leg.
- Qt resources: `resources/{app_icons,fonts,preview_runtime_qml,quick_shell_qml}.qrc`.
- Chart-directory conventions: `maidata.txt`, `track.mp3` (`track_bak.mp3`), background
  `bg.mp4`/`pv.mp4`/`bg.{jpg,png,jpeg}` (or `&video=` target; `<stem>_bak.mp4`), project sidecar
  `.miacode/` (`miacode_settings.json`, `waveform/`, `.autosave/<chart>/`, `logs/`).
- SFX kind→filename map: `src/common/PreviewSfxAssets.h` (kinds: answer, judge, judge_break,
  slide, break, ex, touch, touchhold, firework, clock). Do not rename casually — preview AND
  export depend on it.
- ffmpeg: pinned-binary notes in `third_party/ffmpeg/README.md`; never commit the binary
  (`.gitignore` blocks it). Export resolves ffmpeg from app-local/repo-local fallback.
  The Linux x86_64 provisioner validates the fixed archive and binary hashes, ELF architecture,
  resolved runtime linkage without system FFmpeg libraries, version, and the
  encoder/filter/protocol/format surface used by MiaCode before packaging it as
  `app/ffmpeg/ffmpeg`.
- **QtAVPlayer preview backend (desktop):** vendored MIT source in `third_party/QtAVPlayer/`
  (compiled straight into `MiaCode` with `QT_AVPLAYER_MULTIMEDIA` + `QT_BUILD_QTAVPLAYER_LIB`);
  needs the `Qt6::MultimediaQuickPrivate` component and the FFmpeg dev SDK (see §4). The CMake
  block lives just after `target_link_libraries(MiaCode … Qt6::Multimedia)` and covers Windows,
  macOS, and Linux. Windows uses D3D11VA; macOS compiles VideoToolbox/Metal; Linux obtains FFmpeg,
  libva, libva-drm, and libdrm from pkg-config and compiles the VA-API DRM/EGL hwdevice. `avdevice` is dropped (CMake `QT_AVPLAYER_NO_AVDEVICE` + a vendored-source patch in
  `qavdemuxer.cpp`/`QtAVPlayer.cmake`) since it's capture-device-only — saves ~7 MB + one DLL.
  **Packaging:** `package-win.ps1` stages 6 `av*.dll` into `app/` next to `MiaCode.exe`
  (`avcodec-61`/`avformat-61`/`avutil-59`/`swresample-5`/`swscale-8` overlap windeployqt's set;
  `avfilter-10` is net-new) and asserts them in `$requiredPackagePaths`. avcodec (≈63 MB) +
  avfilter (≈24 MB) ship as un-trimmed full-LGPL builds by default; the **`scripts/ffmpeg/trim/`**
  toolchain builds a decode-only LGPL n7.1 replacement (~70 MB → ~15–20 MB) from a reviewed
  allowlist (`trim-allowlist.psd1`) + `survey-chart-codecs.ps1` calibration, installs into
  `third_party/ffmpeg/windows/dev/` (backs up to `dev.full.bak`). Decode-only ⇒ stays LGPL (no
  `--enable-gpl`/x264 — that would make MiaCode GPL; export encoding stays in the separate
  `ffmpeg.exe`). `package-mac.sh` copies the complete non-system dylib closure from the selected
  SDK into `MiaCode.app/Contents/Frameworks`, rewrites it to `@rpath`, and fails if an external
  dylib reference remains. Needs MSYS2 + VS BuildTools (both present on the dev box).
- **Qt6::Svg (toolbar gear icon):** in `find_package(Qt6 … COMPONENTS … Svg)` +
  `target_link_libraries(MiaCode PRIVATE Qt6::Svg)`, so `makeSettingsGearIcon`
  (`MainWindowShared.cpp`) renders the Google Material "settings" gear via `QSvgRenderer`.
  windeployqt stages `Qt6Svg.dll` automatically (it's a direct link dependency — no extra
  packaging step). So far this is the only `QSvgRenderer` use; keep it that way unless a new
  feature genuinely needs SVG.

## 7. Do not commit build artifacts

`.gitignore` blocks `build/`, `.qt/`, `dist/`, `CMakeFiles/`, `*.obj`, `*.log`, `*.pdb`,
`tmp*/`, third-party binaries, and whitelists only specific `docs/` and `scripts/` files. Audit
added `experimental/` and `logs/` to the ignore set. Never `git add` a compiled binary, log, or
local experiment output. `experimental/` holds untracked local experiments (e.g. aubio) with no
source — not part of the build.

## Update this file when

- A target is added/removed/re-gated, or the spec/CTest convention changes.
- A build or packaging script is added, renamed, or changes responsibility.
- An asset directory, filename convention, or required packaged binary changes.
