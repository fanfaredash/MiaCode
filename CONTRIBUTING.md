# Contributing to MiaCode

感谢你愿意为 MiaCode 贡献。这个项目是一个基于 Qt 6 / CMake 的 simai 谱面编辑、预览和导出工具，代码里有不少运行时、导出、音频、时间轴和诊断路径会互相影响。

在开始之前，先向愿意阅读和贡献这个仓库的开发者致歉：MiaCode 的大部分代码是在 GPT-5.5、Claude Opus 4.8 等当时较强的模型辅助下，以 vibecoding 的方式快速迭代出来的。虽然项目已经经历过多轮结构整理、代码审计、模块拆分、文档索引和构建验证，但它仍可能存在不够理想的地方，例如抽象边界不清晰、历史路径残留、重复逻辑、局部实现过长、命名不一致、文档滞后、测试覆盖不足、平台差异处理不完整，或某些功能只在主要维护者的使用路径上被充分验证。

如果你在贡献时遇到这些问题，这不是你的错。欢迎直接指出、修正，或在 PR 中说明你观察到的风险。这个项目会更感谢能让代码变得更清楚、更可维护的贡献，而不只是新增功能。提交 PR 前，请先按本文件确认你改的是当前主路径，并且没有漏掉同步面。

## 先读哪些文档

代码永远是最终事实来源；当文档和代码冲突时，以代码为准，并在同一个改动里修正文档。

建议阅读顺序：

1. `README.md` / `README_EN.md`：项目功能、依赖、基础构建入口。
2. `src/README.md`：`src/` 目录结构和当前默认实现路径。
3. `.codex/skills/miacode-dev-guide/SKILL.md`：当前维护用索引入口。
4. `.codex/skills/miacode-dev-guide/references/feature-index.md`：按用户功能定位负责文件、类和函数。
5. `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`：检查 parser、timeline、preview、audio、export、Muri 的同步面。
6. `docs/README.md`：公开规格、测试清单、调试和发布文档入口。

`.claude/skills/` 保留了更细的历史经验和 UI/布局问题排查知识，尤其是 `miacode-dev-guide`、`qt-ui-layout-pitfalls`、`qt-ui-design`。如果 `.claude`、`.codex`、`src/README.md` 的路径或架构描述互相冲突，以当前代码和 `.codex/skills/miacode-dev-guide/` 为准；同时修掉被证伪的旧说明。

## 开发环境与构建

基础要求：

- CMake 3.21+
- C++20 编译器
- Qt 6.8+，包含 `Core`、`Gui`、`Widgets`、`Network`、`OpenGL`、`Qml`、`Quick`、`QuickControls2`、`ShaderTools`、`Multimedia`、`Svg`
- Windows 推荐 Visual Studio 2022 / MSVC

常规开发、编译、测试和验证都使用 `Release` 配置。不要为了普通调试另起一个 Debug 构建；MiaCode 的日常诊断入口是运行时 `--debug`。

Windows 推荐构建入口：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

如果本机已有 Qt，也可以走 CMake preset：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

开发工具和 spec/CTest 目标默认关闭。需要运行规格测试时，用 `MIACODE_BUILD_DEV_TOOLS=ON` 配置，再用 Release 跑 CTest：

```powershell
cmake --preset vs2022-qt6 -DMIACODE_BUILD_DEV_TOOLS=ON
cmake --build --preset release
ctest --test-dir build -C Release
```

新增 spec 目标时，应使用 `CMakeLists.txt` 里的 `miacode_add_dev_tool(NAME TEST ...)` 约定，让目标自动注册到 CTest；不要手写重复的 `add_executable` / `target_link_libraries` / `add_test` 样板。

## 代码结构原则

`src/` 按模块职责组织。除入口文件外，源码应放在已有二级职责目录中，避免创建平行别名目录。

核心分层：

- `src/app/`：应用入口、窗口编排、QuickShell 胶水。`MainWindow` 只做 orchestration，新窗口功能优先落到 `src/app/mainwindow/sections/<feature>/`。
- `src/core/chart/`：simai 文档、解析、转换、规范化。不要依赖 scene 或 runtime。
- `src/core/scene/`：纯 frame-state 数学和图层描述。不得依赖 QSG / D3D11。
- `src/audio/`：BASS / miniaudio 后端与 SFX runtime。其他模块不要直接链接 BASS 或 miniaudio。
- `src/preview/runtime/` 和 `src/preview/quick_scene/`：当前默认 Qt Quick/QSG 预览与导出渲染路径。
- `src/timeline/quick/`：当前 QuickShell 时间轴 QSG 表面。
- `src/render/` 和 `src/sources/`：保留的 D3D11 / DirectComposition 诊断路径，默认关闭。
- `src/tools/`：独立工具、spec、probe、导出和分析辅助。

当前默认实现路径：

- GUI：`QuickShellBootstrap` 是正常 GUI 启动路径。
- 预览渲染：`PreviewRuntime` + `preview/quick_scene/*` 是主路径。
- 时间轴：`timeline/quick/TimelineQuickItem` 和相关 QSG layers 是 QuickShell 主路径。
- 预览音频：`QtPreviewSfxRuntime` 选择 Windows BASS 或非 Windows miniaudio。
- 导出音频：Windows 走 BASS export backend，非 Windows 保留 legacy export backend。

可以只更新默认路径，但如果保留路径仍是用户可见或该行为确实影响诊断路径，必须同步更新。若保留路径未更新，请在 PR 说明里明确写出原因。

## 修改前的定位流程

开始写代码前，请先定位：

1. 用户可见能力是什么？
2. 负责入口在哪个文件、类、函数？查 `feature-index.md`。
3. 这个行为是否存在 preview/export、runtime/export audio、widget/Quick、parser/timeline 等镜像路径？查 `cross-chain-linkage.md`。
4. 是否涉及持久化、导出 snapshot、worker 协议、资源查找、调试 flag 或文档规格？
5. 是否已有共享配置头或 helper 可以复用？

常见高风险同步面：

- `SimaiNativeParser` 的解析、时值、note 字段变化通常影响 timeline、preview、audio、export、Muri 和 normalization。
- `&first`、timing offset、`SimaiTimingMetadata` 会影响解析、预览定位、导出、延迟检测和工具。
- Runtime SFX 与 export SFX 必须保持一致。
- 背景媒体解析、track 路径解析、skin/SFX 查找在预览和导出都有消费者。
- 新增导出设置必须跨过 `VideoExportSnapshot::toJson/fromJson`、worker task 重建和 UI/偏好保存。
- 新增渲染设置要同时考虑实时预览、导出、封面导出和持久化。

## UI 和布局贡献规则

触碰 Qt Widgets / QML 对话框、窗口布局、主题、图层顺序或 hit area 时，请先参考 `.claude/skills/qt-ui-layout-pitfalls/SKILL.md`。

基本规则：

- 不要靠单个控件的像素微调修布局。先判断根因，再用结构性修复。
- 不要信任 styled widget 的 `sizeHint()`；QSS border/padding 经常被低估。必要时 `ensurePolished()` 后测量，或用 live geometry。
- 新增 styled dialog/page 必须能响应主题重应用，颜色来自 `UiTheme::colors()` 或现有主题 token，不要硬编码浅色/深色。
- QML 图层顺序优先靠声明/绘制顺序表达，不要随手加局部 `z` hack。
- hit test 和视觉位置必须来自同一套 canonical geometry。
- 所有可交互控件应可键盘访问，文本输入获得焦点时不要让预览快捷键劫持普通方向键。
- 导出可见动画不能依赖 QML `Timer` 或 `PropertyAnimation` 的事件循环 tick；headless export 需要 frame-driven math。

视觉或布局改动应尽量用截图、实际渲染、像素探针或明确的手动验收结果验证。只读代码通常不足以证明 UI bug 已修好。

## 日志与调试

首选调试入口是：

```powershell
.\build\Release\MiaCode.exe --debug
```

或 Windows 发布包里的 debug launcher：

```powershell
scripts\debug\Start_MiaCode_Debug.bat
```

日志规则：

- 新日志走 `miacode::debug_log`，不要新增裸 `qDebug`、`std::cout`、`printf`、`OutputDebugString`。
- Runtime、Audio、StartupTiming、PreviewProfile 的详细输出由 `--debug` 控制。
- Fatal 不受 `--debug` gate。
- Export 即使非 debug 也保留简要阶段/失败摘要；详细诊断仍需 `--debug`。
- 新增、删除或改变 `MIACODE_*` 调试 flag 时，同步更新 `.codex/skills/miacode-dev-guide/references/debug-flags.md` 和 `docs/ops/DEBUG_INDEX.md`。
- 尽量不要新增新的环境变量。现有 `MIACODE_*` 已很多，优先复用或收敛。

常见日志路径：

- 共享目录 override：`MIACODE_LOG_DIR`
- 默认项目日志：绑定 chart 后使用 `<chart folder>/.miacode/logs/`
- Runtime：`miacode_runtime_debug.log`
- Audio：`miacode_audio_debug.log`
- Export：`miacode_video_export.log`
- Startup：`miacode_startup_timing.log`
- Fatal：`miacode_fatal.log`
- Preview profile：`miacode_preview_profile_summary.txt`

## 常量、资源和文件约定

共享常量优先放在 `src/common/*.h`，尤其是 preview/export、timeline/preview、工具/spec 或文档都需要引用的值。只有纯局部实现细节才留在 `.cpp`。

当你新增、移动、删除或改变常量语义时，检查 `.codex/skills/miacode-dev-guide/references/hardcode-registry.md`。

资源和文件约定：

- 资源根解析走 `src/common/AssetPaths.h`。
- skin、SFX、背景图/视频、outline、字体、slide 数据等路径规则不要散落复制。
- 支持新的背景媒体文件名、track 文件名或 SFX 文件名时，检查预览、导出、工具、打包脚本和文档。
- `slide_data.json` 影响 parser、preview 和 Muri；修改时也要确认 Qt resource / qrc 依赖。
- 不要提交 FFmpeg 二进制、构建产物、日志、dump、本地实验输出或打包产物。

## 文档维护规则

公开文档只放适合随仓库发布的内容。不要提交本机路径、dump、日志原始证据、未公开素材来源、内部交接上下文或未脱敏记录。

变更后请同步维护：

- 文件移动、类/函数入口变化：更新 `.codex/skills/miacode-dev-guide/references/feature-index.md`。
- 跨模块行为变化：更新 `cross-chain-linkage.md`。
- 产品行为、默认值、决策边界变化：更新 `design-ledger.md`。
- 常量、阈值、magic number 归属变化：更新 `hardcode-registry.md`。
- 资源文件名、查找规则、打包依赖、脚本变化：更新 `assets-and-tools.md`。
- 调试 flag、日志 channel、诊断开关变化：更新 `debug-flags.md` 和 `docs/ops/DEBUG_INDEX.md`。
- 新增公开规格：放入 `docs/specs/*` 并在 `docs/README.md` 增加入口。
- 新增测试/验收清单：放入 `docs/tests/`，并和对应规格互相引用。

`.codex/skills/miacode-dev-guide/` 的英文文件是维护用事实源，不要创建平行翻译树。`docs/` 的公开规格和测试清单当前使用中文。

## 许可证与第三方内容

项目自有源码使用 MIT License。仓库整体、随仓库分发的资源、打包二进制和 release archive 当前定位为非商业使用，除非具体文件或第三方许可证另有说明。

贡献涉及依赖、资源、字体、SFX、皮肤、背景媒体、BASS、FFmpeg、Qt、参考实现或打包内容时，请先阅读：

- `LICENSE`
- `LICENSE_SCOPE.md`
- `THIRD_PARTY_NOTICES.md`

不要复制来源不清或授权不兼容的素材、代码、音频、字体或二进制。

## 提交前检查清单

提交 PR 前，请确认：

- 改动命中了当前默认实现路径，或已说明为什么只改保留/诊断路径。
- 已检查相关同步面，尤其是 parser/timeline/preview/audio/export/Muri。
- Release 构建通过；涉及 dev specs 时，`ctest --test-dir build -C Release` 通过或已说明无法运行的原因。
- UI 改动经过实际渲染、截图、像素探针或明确手动验收。
- 新增/修改 debug flag、资源规则、常量、脚本、规格时，相关索引文档已同步。
- 没有提交 build output、logs、dump、local experiment、未脱敏材料或第三方二进制。
- 已阅读 staged/worktree diff，删除明显重复逻辑、临时代码和无关改动。
- PR 描述写明用户可见变化、验证方式、未覆盖的保留路径或残余风险。
