# Assets And Tools

Use this file for asset lookup rules, chart-directory conventions, scripts, helper executables, and packaged external tools.

## 1. Asset Root And Repo Asset Areas

- Shared asset root resolution:
  - File: `src/common/AssetPaths.h`
  - Functions: `findAssetRoot`, `assetPath`
- Main repo asset areas:
  - `assets/skin`
  - `assets/SFX`
  - `assets/background`
  - `assets/noteguide`
  - `assets/generated`
  - `assets/fonts`
- Qt resources:
  - `resources/app_icons.qrc`
  - `resources/fonts.qrc`
  - `resources/icons/*`

## 2. Runtime File Conventions Near A Chart

Current chart-directory conventions:

- chart text file: `maidata.txt`
- music track: `track.mp3`
- background media candidates:
  - `bg.mp4`
  - `pv.mp4`
  - `bg.jpg`
  - `bg.png`
  - `bg.jpeg`
- project render-state sidecar:
  - `.miacode_render_settings.json`

If these conventions change, update both code and this file.

## 3. Asset Consumers

- Skin textures:
  - Consumer: `PreviewCanvas`
  - Entry: `MainWindow::resolvePreviewSkinDir`, `PreviewCanvas::setSkinDirectory`
  - Native chart-review judge overlays load from:
    - `JudgeTextSkins/judge_text_normal.png`
    - `JudgeTextSkins/judge_text_break.png`
    - root-level `assets/skin/just_str_l.png`
    - root-level `assets/skin/just_str_r.png`
    - root-level `assets/skin/just_curv_l.png`
    - root-level `assets/skin/just_curv_r.png`
    - root-level `assets/skin/just_wifi_u.png`
    - root-level `assets/skin/just_wifi_d.png`
  - MaimuriDX-style bad-judge slide overlays load only from `assets/skin/SlideOKSkins/*.png`
  - Canonical filenames:
    - `just_str_l_fast_gd.png`
    - `just_str_r_fast_gd.png`
    - `just_curv_l_fast_gd.png`
    - `just_curv_r_fast_gd.png`
    - `just_wifi_u_fast_gd.png`
    - `just_wifi_d_fast_gd.png`
  - Do not add fallback from these overlays to root-level `assets/skin/just_*.png` or any `perfect`-style judge assets
- SFX clips:
  - Consumer: `QtPreviewSfxRuntime`, export SFX mixing
  - Entry: `miacode::preview_sfx::resolveSfxDirectory`
- Background outlines and auxiliary background art:
  - Consumers: preview and export overlay composition
  - Current references include `background/outline.png` and `background/outline_2.png`
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

Do not rename sound files casually; both preview-time and export-time behavior depend on these conventions.

## 5. Build And Packaging Scripts

- Windows build/package:
  - `scripts/build-win.ps1`
  - `scripts/package-win.ps1`
  - `scripts/package-win.ps1` defaults to `build/`, prechecks version freshness against `CMakeLists.txt` and `build/generated/AppVersion.h`, and auto-runs `cmake --build <BuildDir> --target MiaCode --config <Config> --parallel 8` when the packaged executable is stale or missing
- macOS build/package:
  - `scripts/build-macos.sh`
  - `scripts/package-mac.sh`
- ffmpeg provisioning:
  - `scripts/ensure-windows-ffmpeg.ps1`
  - `scripts/ensure-macos-ffmpeg.sh`
- Script docs:
  - `scripts/README.md`
  - `scripts/README_EN.md`

## 6. Analysis And Debug Scripts

Current repo-local helper scripts include:

- `scripts/analyze_ffmpeg_chain_variants.py`
- `scripts/analyze_video_duplicate_frames.py`
- `scripts/compare_log_vs_video_trajectory.py`
- `scripts/export_and_analyze_duplicates.py`
- `scripts/calc_hold_crop_ratio.py`

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
