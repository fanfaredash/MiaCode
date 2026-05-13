# Assets And Tools

Use this file for asset lookup rules, chart-directory conventions, scripts, helper executables, and packaged external tools.

## 1. Asset Root And Repo Asset Areas

- Shared asset root resolution:
  - File: `src/common/AssetPaths.h`
  - Functions: `findAssetRoot`, `assetPath`
- Main repo asset areas:
  - `assets/skin`
  - Built-in skins live under `assets/skin/skinSTD` and `assets/skin/skinDX`; user-imported skins are additional valid child folders under `assets/skin`
  - `assets/SFX`
  - `assets/background`
  - Custom judge-line PNGs live under `assets/background/outlines`; the render settings import action only opens this folder
  - `assets/noteguide`
  - `assets/generated`
  - `assets/fonts`
- Qt resources:
  - `resources/app_icons.qrc`
  - `resources/fonts.qrc`
  - `resources/preview_runtime_qml.qrc`
  - `resources/icons/*`

## 2. Runtime File Conventions Near A Chart

Current chart-directory conventions:

- chart text file: `maidata.txt`
- music track: `track.mp3`
- toolbox media backup: `track_bak.mp3`
- background media candidates:
  - `bg.mp4`
  - `pv.mp4`
  - chart `&video=` may point at another existing `.mp4`, such as `mv.mp4`
  - toolbox media backup: `<video-stem>_bak.mp4`
  - `bg.jpg`
  - `bg.png`
  - `bg.jpeg`
- project metadata sidecar root:
  - `.miacode/`
- project render-state sidecar:
  - `.miacode/miacode_settings.json`
- waveform cache container root:
  - `.miacode/waveform/`
  - stores hashed per-track waveform cache blobs reused by timeline and latency detection
  - cache validity is tied to normalized track path plus file size and last-modified timestamp
- autosave container root:
  - `.miacode/.autosave/<chart file>/`
  - contains `<chart file>.bak`, `history/*.bak`, and `autosave.json`
  - current history filenames use `YYYY-MM-DD-HH-MM-SS.bak`
  - if multiple history writes happen inside the same second, the later write replaces the earlier file of that same name

If these conventions change, update both code and this file.

The toolbox blank-media submenu operates on the current chart directory only. Its audio action prepends silence to `track.mp3` and leaves the original input as `track_bak.mp3`; its video action prepends black frames to the resolved chart background video and leaves the original input as `<video-stem>_bak.mp4`.

## 3. Asset Consumers

- Skin textures:
  - Consumers: `PreviewRuntime`, `PreviewQuickExportSession`, `VideoExportQuickRenderBackend`
  - Entry: `MainWindow::resolvePreviewSkinDir`, `PreviewRuntime::setSkinDirectory`, `PreviewSceneAssetRepository::setSkinDirectory`, `PreviewSceneAssetLoader::load`
  - Skin selection enumerates child directories of `assets/skin`; a directory is shown only when core files such as `tap.png`, `hold.png`, and `star.png` exist
  - Quick scene textures are uploaded through `PreviewTextureRepository` per Quick item/window, with cacheable reuse keyed by `QImage::cacheKey()`, per-frame transient cleanup, and debug/profile counters for cache hits, cache creates, sprite count, and sprite-batch count
  - `PreviewAnimatedSpriteHelpers` now only caches CPU overlay composites by source-image keys plus tint parameters; Quick runtime `BreakAnimate` / `HoldShine` effects no longer rebuild per-frame `QImage`s and instead run through `PreviewQuickSpriteNodes.cpp` plus `src/preview/quick_scene/shaders/PreviewSpriteMaterial.{vert,frag}`
  - Quick sprite rendering now expands sprites into layer-local contiguous batch geometry keyed by `(texture, effect)` without reordering; shared base images and `sourceRect` slicing are the intended path for atlas-like reuse this round
  - Native chart-review judge overlays load from:
    - `JudgeTextSkins/judge_text_normal.png`
    - `JudgeTextSkins/judge_text_break.png`
    - root-level `<selected skin>/just_str_l.png`
    - root-level `<selected skin>/just_str_r.png`
    - root-level `<selected skin>/just_curv_l.png`
    - root-level `<selected skin>/just_curv_r.png`
    - root-level `<selected skin>/just_wifi_u.png`
    - root-level `<selected skin>/just_wifi_d.png`
  - MaimuriDX-style bad-judge slide overlays load only from `<selected skin>/SlideOKSkins/*.png`
  - Canonical filenames:
    - `just_str_l_fast_gd.png`
    - `just_str_r_fast_gd.png`
    - `just_curv_l_fast_gd.png`
    - `just_curv_r_fast_gd.png`
    - `just_wifi_u_fast_gd.png`
    - `just_wifi_d_fast_gd.png`
  - Do not add fallback from these overlays to root-level `<selected skin>/just_*.png` or any `perfect`-style judge assets
- SFX clips:
  - Consumer: `QtPreviewSfxRuntime`, `VideoExportAudioRenderPlan`, export audio backends
  - Entry: `miacode::preview_sfx::resolveSfxDirectory`
- Windows BASS runtime assets:
  - Repo-local files:
    - `third_party/bass/include/bass.h`
    - `third_party/bass/include/bassmix.h`
    - `third_party/bass/lib/win64/bass.lib`
    - `third_party/bass/lib/win64/bassmix.lib`
    - `third_party/bass/bin/win64/bass.dll`
    - `third_party/bass/bin/win64/bassmix.dll`
    - `third_party/bass/bin/win64/bass_fx.dll`
    - `third_party/bass/bin/win64/bass_aac.dll`
    - `third_party/bass/bin/win64/bassopus.dll`
  - Current build contract:
    - `CMakeLists.txt` links `bass.lib` and `bassmix.lib` on Windows for `MiaCode` and `soundtouch_probe`
    - post-build copy now deploys the repo-local `bass*.dll` files into the executable directory so both preview audio and Windows export audio never depend on MajdataPlay's install path or any external machine-global location
- Background outlines and auxiliary background art:
  - Consumers: preview and export overlay composition
  - Current active variant files:
    - `background/outline_point.png`
    - `background/outline_line.png`
    - `background/outline_area.png`
    - `background/outline_area_labeled.png`
  - Optional custom judge-line PNGs are selected by file name from `background/outlines/*.png`; if the selected file is missing, preview/export fall back to the saved built-in `PreviewOutlineVariant`
  - Source helper art for rebuilding the labeled-area variant currently lives at `background/region_labels_overlay_transparent_v3.png`, and `scripts/build_outline_area_labeled.py` regenerates the final labeled outline by compositing that overlay over `outline_area.png`
  - The active outline assets are currently `1080x1080` canvases with built-in transparent border; preview/export map them across the full playfield square, and the selected variant is a shared render setting rather than an asset-size inference
- Generated slide data:
  - Stored under `assets/generated`
  - Current merged asset file: `assets/generated/slide_data.json`
  - Treat as runtime input data, not ordinary decorative assets

## 4. SFX Naming Convention

Current canonical mappings are defined in `src/common/PreviewSfxAssets.h`.

Important kinds include:

- `answer`
- `judge`
- `judge_break`
- `slide`
- `break`
- `ex`
- `touch`
- `touchhold`
- `firework`
- `clock`

Do not rename sound files casually; both preview-time and export-time behavior depend on these conventions.

## 5. Build And Packaging Scripts

- Default local build expectation:
  - run routine compile, test, and verification work in `--Release` / `--config Release`
  - do not create or maintain a separate `Debug` build just for ordinary compile/test runs
  - use debug-only launch paths or diagnostics only when the task explicitly requires debug-specific investigation
- Windows build/package:
  - `scripts/build-win.ps1`
  - `scripts/package-win.ps1`
  - `scripts/build-local-dev2.ps1`
  - `scripts/package-win.ps1` defaults to `build/`, prechecks version freshness against `CMakeLists.txt` and `build/generated/AppVersion.h`, treats version/header drift as a normal refresh instead of a warning, and auto-runs `cmake --build <BuildDir> --target MiaCode --config <Config> --parallel 8` when the packaged executable or generated version metadata needs refreshing
  - `scripts/build-win.ps1` and `scripts/package-win.ps1` resolve relative `BuildDir`, `DistDir`, `QtRoot`, and Qt output paths from the repo root instead of the caller's current working directory; this prevents `windeployqt` output from spilling into the desktop when launched from outside the repo
  - `scripts/build-win.ps1` now installs `qtmultimedia`, `qtdeclarative`, and `qtsvg` so the Qt Quick runtime can be reproduced from a clean machine
  - `scripts/build-local-dev2.ps1` is the local one-click wrapper used by the desktop shortcut; it reuses repo-local `build/` and `.qt/`, then delegates packaging to `scripts/package-win.ps1`
  - both the CMake post-build deploy step and `scripts/package-win.ps1` now pass `--qmldir src/preview/runtime/qml` to `windeployqt` so the Qt Quick runtime imports are deployed
  - both the CMake post-build deploy step and `scripts/package-win.ps1` explicitly keep the Qt Quick runtime DLL set (`Qt6Quick`, `Qt6Qml`, `Qt6QmlMeta`, `Qt6QmlModels`, `Qt6QmlWorkerScript`) and remove stale `Qt6OpenGLWidgets.dll`
  - the CMake post-build deploy step now also copies the repo-local BASS runtime DLL set (`bass`, `bassmix`, `bass_fx`, `bass_aac`, `bassopus`) into the executable directory
- Windows release packages now also include:
    - root-level `Start_MiaCode_Debug.bat`
    - root-level `Start_MiaCode_Legacy_QML.bat`, which now opts into chart worker rendering and the HWND/DComp timeline route for A/B testing against the default embedded QSG-only startup path
    - root-level `Start_MiaCode_QuickShell_Debug.bat`
    - root-level `logs/` helper folder only for explicit debug-launch scripts; normal project-bound runtime logs default to `.miacode/logs/`
    - `docs/DEBUG_INDEX.md`
    - `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
  - optional Windows dev-tool packaging currently includes only `simai_native_dump.exe`; `soundtouch_probe.exe` is no longer copied by `scripts/package-win.ps1`
- macOS build/package:
  - `scripts/build-macos.sh`
  - `scripts/package-mac.sh`
  - `scripts/build-macos.sh` now installs `qtmultimedia`, `qtdeclarative`, and `qtsvg`
- ffmpeg provisioning:
  - `scripts/ensure-windows-ffmpeg.ps1`
  - `scripts/ensure-macos-ffmpeg.sh`
- Script docs:
  - `scripts/README.md`
  - `scripts/README_EN.md`

## 6. Analysis And Debug Scripts

Current repo-local helper scripts include:

- `scripts/Start_MiaCode_Debug.bat`
- `scripts/Start_MiaCode_Debug_CompareDump.bat`
- `scripts/analyze_ffmpeg_chain_variants.py`
- `scripts/analyze_video_duplicate_frames.py`
- `scripts/compare_log_vs_video_trajectory.py`
- `scripts/export_and_analyze_duplicates.py`
- `scripts/calc_hold_crop_ratio.py`
- `tools/intro_remotion/qml/build-exporter.ps1`
- `tools/intro_remotion/qml/render-qml-video.ps1`
- `tools/intro_remotion/qml/render-remotion-video.ps1`

These are developer aids, not runtime dependencies. If a debugging workflow starts depending on one of them regularly, keep this list and its purpose notes up to date.

Repo-wide debug flags and diagnostic env vars are indexed separately in `references/debug-flags.md`.

## 7. Dev Helper Binaries

Defined in `CMakeLists.txt` behind `MIACODE_BUILD_DEV_TOOLS`:

- `miacode_muri_dump`
- `simai_native_dump`
- `soundtouch_probe`
- `simai_parser_spec`
- `chart_batch_transform_spec`

When these helpers change scope, update both this file and any packaging assumptions.

## 8. ffmpeg Packaging Contract

- Pinned binary notes live in `third_party/ffmpeg/README.md`.
- Runtime export resolves `ffmpeg` from app-local and repo-local fallback paths.
- Packaging scripts copy the pinned `ffmpeg` binary into release artifacts.

If ffmpeg is upgraded, review together:

- `third_party/ffmpeg/README.md`
- `scripts/ensure-windows-ffmpeg.ps1`
- `scripts/ensure-macos-ffmpeg.sh`
- packaging scripts
- any export documentation that mentions version assumptions

## 9. Update This File When

- a new asset directory is added
- a filename convention changes
- a new required external binary is packaged
- a helper script becomes part of normal maintenance workflow
- asset lookup order changes in code
