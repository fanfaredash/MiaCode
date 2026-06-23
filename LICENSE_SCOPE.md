# License Scope

This repository uses the following licensing split for public prerelease distribution.

## Summary

- Project-owned source code is licensed under the MIT License. See [LICENSE](LICENSE).
- The repository as a whole, bundled runtime assets, packaged binaries, and release archives are intended for non-commercial use unless a file or third-party license explicitly says otherwise.
- Third-party components keep their own licenses and redistribution terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Reference projects listed in the notices were used as behavioral or engineering references only, unless copied code or assets are explicitly identified.

## What The MIT License Covers

The MIT License in [LICENSE](LICENSE) applies to MiaCode project-owned source code and project-owned build/maintenance scripts.

It does not relicense third-party code, binary libraries, fonts, SFX, skin textures, background media, intro visual assets, sample content, or packaged runtime dependencies.

## Non-Commercial Distribution Scope

The public repository and prerelease packages may include assets and binaries whose current distribution position is non-commercial. This includes, but is not limited to:

- bundled application assets under [assets](assets)
- intro templates and generated visual assets under [src/intro](src/intro)
- bundled fonts, SFX, skins, background media, and note-guide images
- BASS-related headers, import libraries, and runtime DLLs used by the Windows audio backend
- generated release packages and packaged application archives

Commercial redistribution is not covered by this repository-level distribution position. A commercial distributor would need to replace or relicense restricted assets/dependencies and independently satisfy all third-party license obligations.

## 中文说明

本仓库采用“方案 A”：

- MiaCode 自有源代码使用 MIT License，正文见 [LICENSE](LICENSE)。
- 仓库整体、随仓库分发的资源、打包产物和首发 prerelease 发布包定位为非商业使用，除非具体文件或第三方许可证另有说明。
- 第三方库、字体、音效、皮肤、背景素材、BASS、FFmpeg、Qt 等仍受各自许可证或分发条款约束，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
- 鸣谢/参考项目默认仅表示行为或工程参考；没有直接复制源码或素材，除非文档中另行明确标注。
