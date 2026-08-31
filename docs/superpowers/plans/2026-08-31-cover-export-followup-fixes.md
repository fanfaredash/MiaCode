# Cover Export Follow-up Fixes Implementation Plan

> **For agentic workers:** Use the task-by-task workflow from `superpowers:executing-plans` or `superpowers:subagent-driven-development` when the execution environment permits it. This task is an explicit exception: the user requires direct edits in the existing `feature/qml-ui` workspace, so do not create a worktree, reset, stash, or overwrite unrelated changes. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复封面导出谱面帧播放灰显、检查器标签结构和分辨率乱码。

**Architecture:** 让 `QmlCoverExportSession` 成为 QML 唯一的 live-scene 绑定门面，保留内部 `CoverFrameSceneBinder` 的生命周期保护；`CoverComposer` 通过统一同步函数处理 Loader 的 item/binder 时序。检查器使用现有单一 Flickable/ColumnLayout 将难度卡设置移动到图层页下方，分辨率继续由 session 提供但改用 UTF-8 解码。

**Tech Stack:** Qt 6 C++/QObject, Qt Quick/QML, CMake dev-tool specs, CTest, qmllint.

**Execution constraint:** Work directly in `/Users/caoyusen/Desktop/MiaCode` on the current branch. Preserve the pre-existing untracked `.superpowers/` brainstorm output and any unrelated user changes; stage only files and hunks belonging to this request.

---

## 文件映射

- Modify: `src/app/qml_ui/export/CoverExportPage.qml` — session binder 传递、A 方案标签/布局、图层选中跳转。
- Modify: `src/intro/qml/CoverComposer.qml` — live Loader 的统一绑定同步和旧对象身份保护；画布顶层 TapHandler、DragHandler、图层 TapHandler 共用选中路由。
- Modify: `src/app/qml_ui/export/QmlCoverExportSession.cpp` — 保持/强化完整绑定门面调用链，必要时补绑定生命周期。
- Modify: `src/tools/qml_ui/QmlCoverExportContractSpec.cpp` — 增加播放绑定、A 布局与 UTF-8 回归契约。
- Modify: `src/app/qml_ui/export/QmlCoverExportSession.cpp` — `resolutionOptions()` 使用 UTF-8 解码。
- Modify: `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md` — 更新现有 cover-export 跨模块条目，记录 session 作为 live binder 门面的契约。

## Task 1: 为三条回归补失败测试

**Files:**

- Modify: `src/tools/qml_ui/QmlCoverExportContractSpec.cpp`

- [ ] **Step 1: 增加失败优先契约**

加入以下断言：

- `CoverExportPage.qml` 的 `chartSceneBinder` Binding 传递 `root.session`，而不是 `root.session.chartSceneBinder`；页面保留 `toggleActiveLayerPlayback()` 和 `liveChartSceneBound` 门控。
- `CoverComposer.qml` 同时有 `syncLiveChartBinding`、`onItemChanged` 和 `onChartSceneBinderChanged`，并记录 `boundBinder`/`boundItem`。
- 页面不再包含独立的 `cover.difficulty_card` AppTab，图层和难度卡区都由 `inspectorTab === "layer"` 控制；图层点击路由包含 `root.inspectorTab = "layer"`。
- session 的 `resolutionOptions()` 使用 `QString::fromUtf8(preset.label)`，而不是 `fromLatin1`。
- session facade 的 `bindLiveChartScene()` 依次包含 overlay layer flags、live scene/frame state、binder frame state 和 binder bind 调用。
- 用源码位置而不只用散落字符串确认：三个 tab 的数量/顺序、图层行先切 tab 再选中、难度卡位于图层区之后且页面只有一个 Flickable。
- 用源码位置确认 Loader 的解绑→更新 binder/item→重新绑定顺序。

- [ ] **Step 2: 运行并确认测试失败**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec --parallel 4`

Run: `ctest --test-dir build-macos -C Release -R '^qml_cover_export_contract_spec$' --output-on-failure`

Expected: FAIL，失败项对应尚未实现的绑定门面、A 布局和 UTF-8 契约；不得把已有环境问题误判为本次回归。

- [ ] **Step 3: Commit**

```bash
git add src/tools/qml_ui/QmlCoverExportContractSpec.cpp
git commit -m "test: cover follow-up regression contracts"
```

## Task 2: 修复完整 live-scene 绑定和 Loader 生命周期

**Files:**

- Modify: `src/app/qml_ui/export/CoverExportPage.qml`
- Modify: `src/intro/qml/CoverComposer.qml`

- [ ] **Step 1: 让 session 成为 QML 绑定门面**

把页面传给 Composer 的 `chartSceneBinder` 改为 `root.session`。Composer 仍调用 `bindLiveChartScene`/`unbindLiveChartScene`，但实际落到 session 的完整入口，负责给 `PreviewQuickSceneRoot` 设置 overlay flags 和共享 `frameState`。

- [ ] **Step 2: 实现统一同步函数**

在 `liveChartLoader` 内增加 `boundBinder`，实现 `syncLiveChartBinding()`：旧 binder+item 都存在时按身份解绑；再把当前 binder/item 写入状态；当前两者有效时绑定。`onItemChanged` 与父级 `onChartSceneBinderChanged` 都调用它。Loader 销毁、active 关闭和 item 替换时不允许旧对象清掉新绑定。

- [ ] **Step 3: 运行最小播放/绑定回归**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec cover_frame_scene_binder_spec --parallel 4`

Run: `ctest --test-dir build-macos -C Release -R '^(qml_cover_export_contract_spec|cover_frame_scene_binder_spec)$' --output-on-failure`

Expected: 两个测试 PASS。

## Task 3: 实现 A 方案检查器布局与图层选中路由

**Files:**

- Modify: `src/app/qml_ui/export/CoverExportPage.qml`
- Modify: `src/intro/qml/CoverComposer.qml`

- [ ] **Step 1: 移除 card 标签并合并区块**

删除难度卡 AppTab；把难度卡 ColumnLayout 的 `visible` 改为图层页条件，并移动到图层区通用/类型控件之后，保留分隔线、标题和所有现有控件绑定。

- [ ] **Step 2: 统一图层选择入口**

左侧图层行的 `onClicked` 先设置 `root.inspectorTab = "layer"` 再调用 `selectLayerKey`。在 Composer 的选择 binder 绑定到 session 的前提下，让页面提供一个选择路由函数，画布顶层 TapHandler、DragHandler 和图层 TapHandler 也走该路由；不要用无条件 `Connections.onActiveLayerChanged` 误切换程序化的 preset/导入操作。

- [ ] **Step 3: 做布局静态检查**

确认图层和难度卡区共享外层 `Flickable`，没有新增嵌套 Flickable、固定 y/height 或像素补偿；确认 `contentHeight: inspector.implicitHeight` 保持动态更新。

## Task 4: 修复分辨率解码并验证三条需求

**Files:**

- Modify: `src/app/qml_ui/export/QmlCoverExportSession.cpp`

- [ ] **Step 1: 使用 UTF-8 解码**

将 `QString::fromLatin1(preset.label)` 改为 `QString::fromUtf8(preset.label)`，不改 C++ 数组字段或 QML 数据结构。

- [ ] **Step 2: 运行契约和 QML 校验**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec --parallel 4`

Run: `ctest --test-dir build-macos -C Release -R '^qml_cover_export_contract_spec$' --output-on-failure`

Run the repository's existing Release QML lint target: `cmake --build build-macos --config Release --target MiaCode_qmllint --parallel 4`. The generated command is `/Users/caoyusen/Desktop/MiaCode/.qt/6.10.2/macos/bin/qmllint @/Users/caoyusen/Desktop/MiaCode/build-macos/.rcc/qmllint/MiaCode.rsp`; do not substitute the broken system `/opt/homebrew/bin/qmllint` binary. The response file includes `CoverExportPage.qml`; for the intro source, run the same bundled binary with the response-file arguments plus `/Users/caoyusen/Desktop/MiaCode/src/intro/qml/CoverComposer.qml`.

Expected: contract PASS；QML lint exit 0，允许既有 unresolved import/unqualified-access warning。

## Task 5: 全量验证和差异审查

**Files:**

- Review: all modified files and `git diff`.

- [ ] **Step 1: 构建相关 Release targets**

使用仓库 `miacode-concurrent-build` skill 的并发构建流程（先确认没有其它 MiaCode 构建进程，最多 `--parallel 4`）构建：

`cmake --build build-macos --config Release --target MiaCode cover_layout_model_spec cover_frame_playback_controller_spec cover_frame_scene_binder_spec qml_cover_export_contract_spec --parallel 4`

`cmake --build build-macos --config Release --target MiaCode_qmllint --parallel 4`

如果构建目录需要重新配置，使用 Release 配置并显式保持 `-DMIACODE_BUILD_DEV_TOOLS=ON`，再按同一并发上限构建。

- [ ] **Step 2: 运行相关 CTest**

Run: `ctest --test-dir build-macos -C Release -R '^(cover_layout_model_spec|cover_frame_playback_controller_spec|cover_frame_scene_binder_spec|qml_cover_export_contract_spec)$' --output-on-failure`

Expected: 4/4 PASS。

- [ ] **Step 3: 检查 diff 与工作树**

确认没有新 worktree、没有触碰 `.superpowers/` 用户侧脑暴产物；确认只修改上述功能文件和对应文档/测试。记录任何与本次无关的既有失败，不修改它们。

- [ ] **Step 4: GUI/窄窗口检查**

启动当前 Release 应用，在封面导出窗口确认：仅显示 `画板 / 图层 / 预设` 三个 tab；图层页下方出现难度卡设置；窄右栏能通过同一个滚动区域访问卡设置；点左侧图层、点画布或拖动图层都会把检查器切到图层页；播放按钮和时间滑块可用且播放到末尾停住。确认主编辑器预览在封面导出期间隐藏。
