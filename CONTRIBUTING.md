# Contributing to MiaCode

感谢你愿意为 MiaCode 贡献。这个项目是一个基于 Qt 6 / CMake 的 simai 谱面编辑、预览和导出工具，代码里有不少运行时、导出、音频、时间轴和诊断路径会互相影响。

在开始之前，先向愿意阅读和贡献这个仓库的开发者致歉：MiaCode 的大部分代码是在 GPT-5.5、Claude Opus 4.8 等当时较强的模型辅助下，以 vibecoding 的方式快速迭代出来的。虽然项目已经经历过多轮结构整理、代码审计、模块拆分、文档索引和构建验证，但它仍可能存在不够理想的地方，例如抽象边界不清晰、历史路径残留、重复逻辑、局部实现过长、命名不一致、文档滞后、测试覆盖不足、平台差异处理不完整，或某些功能只在主要维护者的使用路径上被充分验证。

如果你在贡献时遇到这些问题，这不是你的错。欢迎直接指出、修正，或在 PR 中说明你观察到的风险。这个项目会更感谢能让代码变得更清楚、更可维护的贡献，而不只是新增功能。提交 PR 前，请先按本文件确认你改的是当前主路径，并且没有漏掉同步面。

## 先读哪些文档

代码是最终事实来源。建议从 `README.md`、`src/README.md`、
`.agents/skills/miacode-dev-guide/SKILL.md` 和 `docs/INDEX.md` 开始。
开发 skill 提供模块、复用组件和跨模块同步地图；当前规范、验收清单、工作记录与历史方案在索引中分开。

`miacode-dev-guide` 只维护 `.agents/skills/miacode-dev-guide/`。
`.claude/skills/miacode-dev-guide/` 与 `.codex/skills/miacode-dev-guide/` 是生成镜像，禁止单独改正文。
修改后运行 `python3 scripts/governance/sync_guides.py --sync`，提交前运行 `--check`。
其他 skill 不属于此同步工具的范围。

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

新增规格放入 `src/tools/<domain>/`，在 `cmake/devtools/specs/` 的相应 manifest 用
`miacode_add_spec` 注册 owner、稳定 contract ID、类别及执行方式，复用现有 SOURCES/LIBS 分组。
CTest 自动获得 domain/kind/risk/platform 标签；诊断 helper 继续用 `miacode_add_dev_tool`。
源码必须恰好登记一次，具体必填字段见 `cmake/devtools/MiaCodeSpecRegistry.cmake`。
生成目录：`cmake -P cmake/devtools/SpecCatalog.cmake`。

日常只构建与运行相关规格（将占位符替换成实际值）：

```sh
cmake --build <dev-build> --config Release --target <spec-target> --parallel 4
ctest --test-dir <dev-build> -C Release -R '^<spec-target>$' --output-on-failure
```

保持独立编译、链接、进程边界；没有逐断言覆盖证据，不按名称相近删除或合并规格。
`playback_coordinator_construction_spec` 是既有的未闭合链接实验，排除默认构建、无 CTest；目录
明确标为 `blocked-link`，不能把它当成已通过的 compile-only 契约。

## 代码结构与同步面

- `src/app/ui/`：产品前端、C++ models 和共享 QML 组件。
- `src/app/services/`：ChartWorkspace、ApplicationServices、共享服务与 typed ports。
- `src/app/runtime/`：Session 装配与各领域 host；按领域归属扩展已有宿主。
- `src/core/chart/`：文档、解析和变换；`src/core/scene/`：无 GPU 依赖的场景数学。
- `src/preview/`、`src/audio/`、`src/timeline/`：QSG 预览、音频运行时和时间轴。
- `src/tools/`：导出/分析能力以及独立规格、probe；`src/common/`：共享配置与 helper。

具体所有权见 `docs/specs/ui/CURRENT_ARCHITECTURE_ZH.md`。新功能先找 owner 和可复用组件，
不要重建 MainWindow/v1 外壳或旧 DComp 图表渲染路径。现行 D3D11/QRhi、OpenGL 导出和导出 worker
仍保留，见 `docs/specs/preview/CURRENT_RENDER_EXPORT_CONTRACT_ZH.md`。

修改 note 属性、BPM、`&first` 或时值时，检查 parser、timeline、scene、audio、export、Muri 和
transform/normalization。文档变化需检查 revision、异步结果与保存点；导出设置需贯穿偏好、snapshot、
JSON 和 worker task。详见 skill 的 `references/cross-chain-linkage.md`。

## UI 和布局贡献规则

触碰 QML 对话框、窗口布局、主题、图层顺序或 hit area 时，请先参考 `.agents/skills/qt-ui-layout-pitfalls/SKILL.md`。

基本规则：

- 不要靠单个控件的像素微调修布局。先判断根因，再用结构性修复。
- 优先复用 `src/app/ui/components/` 的表单、对话框、菜单与滚动组件，并检查隐式尺寸和实际几何。
- QML 页面与控件使用 `theme/Theme.qml` 的 token 和 `qsTrId` 文案键，不复制颜色或本地化逻辑。
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
- 新增、删除或改变 `MIACODE_*` 调试 flag 时，同步更新 `docs/ops/DEBUG_INDEX.md`；skill 不维护第二份 flag 表。
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

当你改变常量语义时，查找所有使用者并更新相关规格；仅模块归属或共享入口变化时才更新开发 skill。

资源和文件约定：

- 资源根解析走 `src/common/AssetPaths.h`。
- skin、SFX、背景图/视频、outline、字体、slide 数据等路径规则不要散落复制。
- 支持新的背景媒体文件名、track 文件名或 SFX 文件名时，检查预览、导出、工具、打包脚本和文档。
- `slide_data.json` 影响 parser、preview 和 Muri；修改时也要确认 Qt resource / qrc 依赖。
- 不要提交 FFmpeg 二进制、构建产物、日志、dump、本地实验输出或打包产物。

## 文档维护规则

公开文档不提交本机路径、dump、原始日志、素材来源或未脱敏交接材料。被 Git 忽略的本地工作记录不进入索引。

- 当前规范放 `docs/specs/`，复用验收清单放 `docs/tests/`；阶段计划和审计标为 `working`。
- 历史方案标 `archive-legacy` 并指向当前入口，不能把其中的目标架构当作当前实现。
- 元数据格式、生成命令见 `docs/README.md`；索引从 frontmatter 生成，不手写第二份目录。
- skill 只在模块、复用入口、长期边界或同步面变化时更新，不随函数改名、像素调整、flag 增删频繁扩充。

提交前运行：

```sh
python3 scripts/governance/sync_guides.py --check
python3 scripts/governance/docs_index.py --check
cmake -DMIACODE_SPEC_CATALOG_CHECK=ON -P cmake/devtools/SpecCatalog.cmake
```

治理工具改动另运行 `python3 -m unittest discover -s scripts/governance -p '*_test.py'`。
公开规范与验收清单使用中文；生成目录的代码标识保持原样。

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
