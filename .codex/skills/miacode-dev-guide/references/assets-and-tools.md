# Assets And Tools

Use this file for asset lookup rules, chart-directory conventions, scripts, helper executables, and packaged external tools.

## 1. Asset Root And Repo Asset Areas

- Shared asset root resolution:
  - File: `src/common/AssetPaths.h`
  - Functions: `findAssetRoot`, `assetPath`
- Main repo asset areas:
  - `assets/skin`
  - Built-in skins live under `assets/skin/skinSD` and `assets/skin/skinDX`; user-imported skins are additional valid child folders under `assets/skin`
  - `assets/SFX`
  - `assets/background`
  - Custom judge-line PNGs live under `assets/background/outlines`; the render settings import action only opens this folder
  - `assets/noteguide`
  - `assets/reference`
  - `assets/fonts`
- Qt resources:
  - `resources/app_icons.qrc`
  - `resources/fonts.qrc`
  - `resources/slide_data.qrc`
  - `resources/preview_judge_effects.qrc`
  - `resources/preview_runtime_qml.qrc`
  - `resources/icons/*`
  - `resources/community/*` for README/community-facing repository images

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
  - stores hashed per-track waveform cache blobs used by widget and Quick timeline waveform rendering
  - cache validity is tied to normalized track path plus file size and last-modified timestamp
  - Windows cache generation decodes through repo-local BASS so MP3 delay/padding matches the Windows preview BGM playback backend; non-Windows falls back to miniaudio
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
  - Touch break assets use the external-skin naming convention first:
    - `touch_break_border_2.png`
    - `touch_break_border_3.png`
    - `touch_break_point.png`
    - `touchhold_break_0.png`
    - `touchhold_break_1.png`
    - `touchhold_break_2.png`
    - `touchhold_break_3.png`
    - `touchhold_break_border.png`
  - The older MiaCode names remain a compatibility fallback for user skins:
    - `touch_border_2_break.png`
    - `touch_border_3_break.png`
    - `touch_point_break.png`
    - `touchhold_0_break.png`
    - `touchhold_1_break.png`
    - `touchhold_2_break.png`
    - `touchhold_3_break.png`
    - `touchhold_border_break.png`
  - `touchhold_border_miss.png` / `touchhold_off.png` are not runtime assets and should not be shipped.
  - Judge-effect textures are built into the program through `resources/preview_judge_effects.qrc`; they are not loaded from the selected skin directory and should not be shipped under `assets/skin/*`
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
    - post-build copy deploys the repo-local `bass*.dll` files into the executable directory
    - `src/common/WaveformCache.cpp` also uses BASS on Windows to keep timeline waveform cache timing aligned with preview BGM playback
- Background outlines and auxiliary background art:
  - Consumers: preview and export overlay composition
  - Current active variant files:
    - `background/outline_point.png`
    - `background/outline_line.png`
    - `background/outline_area.png`
    - `background/outline_area_labeled.png`
  - Optional custom judge-line PNGs are selected by file name from `background/outlines/*.png`; if the selected file is missing, preview/export fall back to the saved built-in `PreviewOutlineVariant`
  - Source helper art for rebuilding the labeled-area variant currently lives at `background/region_labels_overlay_transparent_v3.png`; rebuilding the final labeled outline is a maintainer-local asset task unless the helper becomes part of a repeatable public workflow
  - The active outline assets are currently `1080x1080` canvases with built-in transparent border; preview/export map them across the full playfield square, and the selected variant is a shared render setting rather than an asset-size inference
- Generated slide data:
  - Stored under `assets/reference`
  - Current merged asset file: `assets/reference/slide_data.json`
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
  - `scripts/build/build-win.ps1`
  - `scripts/build/package-win.ps1`
  - `scripts/build/package-win.ps1` defaults to `build/`, prechecks version freshness against `CMakeLists.txt` and `build/generated/AppVersion.h`, treats version/header drift as a normal refresh instead of a warning, and auto-runs `cmake --build <BuildDir> --target MiaCode` / `MiaCodeLauncher --config <Config> --parallel 8` when the packaged executables or generated version metadata need refreshing
  - `scripts/build/build-win.ps1` and `scripts/build/package-win.ps1` resolve relative `BuildDir`, `DistDir`, `QtRoot`, and Qt output paths from the repo root instead of the caller's current working directory; this prevents `windeployqt` output from spilling into the desktop when launched from outside the repo
  - `scripts/build/build-win.ps1` installs the add-on Qt modules `qtmultimedia` and `qtshadertools`; `qtdeclarative`/Qt Quick and `qtsvg` are provided by the base Qt desktop package for Qt 6.8.3
  - `scripts/build/build-win.ps1` provisions both FFmpeg inputs needed by a clean Windows clone: standalone export `third_party/ffmpeg/windows/ffmpeg.exe` and the QtAVPlayer preview-decode dev SDK under `third_party/ffmpeg/windows/dev/`
  - both the CMake post-build deploy step and `scripts/build/package-win.ps1` now pass `--qmldir src/preview/runtime/qml` to `windeployqt` so the Qt Quick runtime imports are deployed
  - both the CMake post-build deploy step and `scripts/build/package-win.ps1` explicitly keep the Qt Quick runtime DLL set (`Qt6Quick`, `Qt6Qml`, `Qt6QmlMeta`, `Qt6QmlModels`, `Qt6QmlWorkerScript`) and remove stale `Qt6OpenGLWidgets.dll`
  - the CMake post-build deploy step now also copies the repo-local BASS runtime DLL set (`bass`, `bassmix`, `bass_fx`, `bass_aac`, `bassopus`) into the executable directory
- Windows release packages now also include:
    - root-level `Start_MiaCode_Debug.bat`
    - root-level `logs/` helper folder only for explicit debug-launch scripts; normal project-bound runtime logs default to `.miacode/logs/`
    - root-level `LICENSE`, `LICENSE_SCOPE.md`, `THIRD_PARTY_NOTICES.md`, and `licenses/`; repository README files are developer-facing docs and are not shipped
  - optional Windows dev-tool packaging currently includes only `simai_native_dump.exe`; `soundtouch_probe.exe` is no longer copied by `scripts/build/package-win.ps1`
- macOS build/package:
  - `scripts/build/build-macos.sh`
  - `scripts/build/package-mac.sh`
  - `scripts/build/build-macos.sh` now installs `qtmultimedia`, `qtdeclarative`, `qtshadertools`, and `qtsvg`
- ffmpeg provisioning:
  - `scripts/ffmpeg/ensure-windows-ffmpeg.ps1`
  - `scripts/ffmpeg/ensure-macos-ffmpeg.sh`
- Script docs:
  - `scripts/README.md`
  - `scripts/README_EN.md`
- Asset helper scripts:
  - `scripts/assets/match_outline_canvas_ratio.py` pads/crops transparent outline PNGs so external 980-style art matches the alpha-bounds ratio of MiaCode's 1080x1080 outline canvases.
  - `scripts/gen_skin_mine_sprites.py` generates `<base>_mine.png` skin sprites by applying the MajMine luminance grayscale transform while preserving alpha.
  - `scripts/assets/build_skin_tool_exes.ps1` packages those two helpers as standalone Windows executables under `dist/skin-tools-win64`:
    - `miacode-outline-canvas-tool.exe`
    - `miacode-skin-mine-tool.exe`

## 6. Analysis And Debug Scripts

Current repo-local helper scripts include:

- `scripts/debug/Start_MiaCode_Debug.bat`
- `scripts/debug/Start_MiaCode_SoftwareVideoDecode.bat`
- `scripts/debug/Start_MiaCode_QtPluginDiag.bat`

Other one-off analysis and A/B scripts are local maintainer tools and stay ignored unless they become part of a repeatable public workflow.

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
- `scripts/ffmpeg/ensure-windows-ffmpeg.ps1`
- `scripts/ffmpeg/ensure-macos-ffmpeg.sh`
- packaging scripts
- any export documentation that mentions version assumptions

## 9. Update This File When

- a new asset directory is added
- a filename convention changes
- a new required external binary is packaged
- a helper script becomes part of normal maintenance workflow
- asset lookup order changes in code
