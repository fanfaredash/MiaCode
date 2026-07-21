# Third-Party Notices

This file inventories libraries, tools, fonts, media assets, and reference projects used by MiaCode.

## Project License

- Project-owned MiaCode source code is licensed under the MIT License; see [LICENSE](LICENSE).
- The repository as a whole, bundled runtime assets, packaged binaries, and release packages are intended for non-commercial use; see [LICENSE_SCOPE.md](LICENSE_SCOPE.md).
- The project license does not replace the licenses of bundled libraries, tools, fonts, SFX, image assets, packaged runtime dependencies, or reference material.

## Direct Dependencies And Bundled Code

| Component | Location / Use | License / Notes |
| --- | --- | --- |
| Qt 6 | Application framework, widgets, Qt Quick, multimedia, deployment tools | Qt is available under commercial and open-source licenses. Confirm the intended distribution model and comply with Qt redistribution obligations. |
| FFmpeg | Export executable and Windows preview decode SDK | The repository documents an LGPL shared baseline in [third_party/ffmpeg/README.md](third_party/ffmpeg/README.md). Keep GPL/nonfree build flags out unless the project license/distribution plan changes. |
| QtAVPlayer | Vendored preview video decode backend under [third_party/QtAVPlayer](third_party/QtAVPlayer) | MIT License, see [third_party/QtAVPlayer/LICENSE](third_party/QtAVPlayer/LICENSE). |
| SoundTouch | Audio processing under [third_party/soundtouch](third_party/soundtouch) | LGPL 2.1, see [third_party/soundtouch/COPYING.TXT](third_party/soundtouch/COPYING.TXT). Static linking and redistribution obligations need review before public release. |
| miniz | ZIP packaging under [third_party/miniz](third_party/miniz) | MIT License, see [third_party/miniz/LICENSE](third_party/miniz/LICENSE). |
| miniaudio | Audio playback / decode helper under [third_party/miniaudio](third_party/miniaudio) | Public Domain or MIT-0, see the license block in [third_party/miniaudio/miniaudio.h](third_party/miniaudio/miniaudio.h). |
| BASS, BASSmix, BASS_FX, BASS_AAC, BASSOPUS | Windows and macOS audio backends, offline export mixing, and waveform decoding; packaged DLL/dylib runtimes | Kept for non-commercial MiaCode builds/releases. BASS is not a general open-source dependency; do not use MiaCode's bundled BASS files for commercial redistribution without appropriate BASS licensing. |

## Fonts

| Asset | Location / Use | License / Notes |
| --- | --- | --- |
| Resource Han Rounded | Intro and packaged font assets | SIL Open Font License 1.1 text is stored in [licenses/Resource-Han-Rounded-OFL.txt](licenses/Resource-Han-Rounded-OFL.txt). |
| Xiaolai Mono subset | [assets/fonts/XiaolaiMono-Regular.subset.ttf](assets/fonts/XiaolaiMono-Regular.subset.ttf) | Distributed with MiaCode's non-commercial repository/release package. Keep upstream source and license notes available when publishing. |
| M PLUS fonts | Former Remotion prototype assets | Not distributed and removed from the repository; the main application has no direct Remotion dependency. |
| Consolas | [assets/fonts/consola.ttf](assets/fonts/consola.ttf) | Distributed with MiaCode's non-commercial repository/release package. Keep redistribution status visible because this is commonly a Microsoft font. |

## Media And Runtime Assets

| Asset Group | Location / Use | Status |
| --- | --- | --- |
| Skin textures | [assets/skin](assets/skin) | Distributed with MiaCode's non-commercial repository/release package. |
| SFX clips | [assets/SFX](assets/SFX) | Distributed with MiaCode's non-commercial repository/release package. |
| Background outlines and noteguide images | [assets/background](assets/background), [assets/noteguide](assets/noteguide) | Distributed with MiaCode's non-commercial repository/release package. |
| Intro templates and generated visual assets | [src/intro](src/intro) | Distributed with MiaCode's non-commercial repository/release package; intro transition behavior references gfdfdxc/maimai-transition. |
| OpenMoji assets | Former Remotion prototype assets | Not distributed and removed from the repository; the main application has no direct OpenMoji dependency. |

## Reference Implementations

These projects are acknowledged as behavioral or engineering references. They should not be treated as vendored code unless copied source or assets are present in this repository.

| Project | How It Was Used | Status |
| --- | --- | --- |
| Minepig/MaiMuriDX | simai parsing and rendering behavior reference | Behavioral reference only; no direct source or asset copy. |
| gfdfdxc/maimai-transition | Intro transition behavior and visual reference | Behavioral/visual reference for the recreated intro transition; no direct source or asset copy. |
| MajdataPlay | Preview/audio behavior and architecture reference mentioned in design docs | Behavioral reference only; no direct source or asset copy. |

## Before Public Release

- Keep this file in sync with [README.md](README.md), [README_EN.md](README_EN.md), release notes, and package contents.
- Keep [LICENSE_SCOPE.md](LICENSE_SCOPE.md) in sync with actual repository and release package contents.
- Re-run a package-content audit before each release and remove entries that are development-only.
- If a dependency is downloaded by script instead of committed, document the script, version, URL, checksum, and license.
