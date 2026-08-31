# Cover Export Interaction Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复封面导出 v2 页面图层拖动、谱面帧播放/定位和主编辑器预览隐藏行为。

**Architecture:** 保留 `CoverComposer.qml` 作为唯一封面画布，使用本地坐标完成命中和拖动。新增封面专用 C++ 播放控制器，由 `QmlCoverExportSession` 对 QML 暴露状态和操作；它驱动 `SceneFrameRenderer` 的共享 `PreviewFrameState`，可见 `PreviewQuickSceneRoot` 只负责实时绘制，离屏截图只用于提交和最终导出。封面页激活状态同时控制普通 `PreviewPane` 和全屏预览覆盖层。

**Tech Stack:** Qt 6 C++/QObject/QTimer/QElapsedTimer, Qt Quick/QML, QSG `PreviewQuickSceneRoot`, CMake dev-tool specs, Release CTest, qmllint.

---

## 文件映射

- Create: `src/tools/cover_export/CoverFramePlaybackController.h/.cpp` — 封面播放、单步定位和按住加速的无 UI 控制器。
- Create: `src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp` — 控制器的确定性回归测试。
- Create: `src/tools/cover_export/CoverFrameSceneBinder.h/.cpp` — 单 live `PreviewQuickSceneRoot` 的身份安全绑定和 borrowed frame-state 生命周期。
- Create: `src/tools/cover_export/CoverFrameSceneBinderSpec.cpp` — Loader 重建、旧 root 延迟解绑和 frame-state 置空的回归测试。
- Create: `src/tools/cover_export/CoverFrameExportPlan.h/.cpp` — 可测试的多谱面帧导出快照计划，保存每层独立时间并定义恢复活动 playhead 的边界。
- Modify: `src/app/qml_ui/export/QmlCoverExportSession.h/.cpp` — 播放器生命周期、热更新/提交接口、live scene binder、导出恢复事务。
- Modify: `src/intro/qml/CoverComposer.qml` — 本地坐标拖动、绑定状态/fallback、Loader 身份安全。
- Modify: `src/app/qml_ui/export/CoverExportPage.qml` — Inspector 播放按钮、滑块按下/移动/释放和焦点键盘路由。
- Modify: `src/app/qml_ui/components/LabeledSlider.qml` — 暴露拖动结束信号或等价的 released 生命周期。
- Modify: `src/app/qml_ui/layout/MainSplitView.qml` — 封面页隐藏/停用主预览和关闭全屏预览。
- Modify: `src/tools/qml_ui/QmlCoverExportContractSpec.cpp` — 失败优先的 QML/C++ 源契约回归。
- Modify: `CMakeLists.txt` — 新控制器源文件、测试目标和应用源列表。
- Modify: `src/app/ui/UiText.cpp` — 仅在现有 `preview.play`、`preview.pause`、`cover.play_pause_space` 不足时补齐可访问文案；不在 QML 写硬编码可见文案。
- Modify: `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md` and `design-ledger.md` — 记录 v2 session binder、单 live scene、共享 playhead 恢复契约。

## Task 1: 建立播放控制器的失败测试

**Files:**

- Create: `src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 添加确定性行为测试和测试目标**

测试不依赖真实 QTimer 时间：通过控制器提供的 elapsed/advance 测试 seam 驱动时间。覆盖：

- duration 和 seconds 被限制在 `[0, duration]`；
- `play()` 推进，`pause()` 不推进；
- 到 duration 保持末帧并自动停止；
- 左右单击各只移动 `1/120` 秒；
- hold threshold 前不额外移动，threshold 后按已持续时间加速并受最大速率限制；
- key release、cancel/focus-loss 等价路径停止 hold seek；
- 末尾重新播放从 0 秒开始；
- duration 为 0 时无法播放。

在 `CMakeLists.txt` 的 dev-tools 区域新增 `cover_frame_playback_controller_spec`，此时只列出已经创建的 spec 文件，链接 `Qt6::Core`，包含 `src`。现有 `miacode_add_dev_tool(... TEST)` 会同时调用 `add_test`，无需另造测试注册逻辑；如果修改该 helper，必须保留其 source-tree working directory 和 Qt PATH 注入。

- [ ] **Step 2: 运行失败测试**

Run: `cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DMIACODE_BUILD_DEV_TOOLS=ON -DCMAKE_PREFIX_PATH="$PWD/.qt/6.10.2/macos" && cmake --build build-macos --config Release --target cover_frame_playback_controller_spec -- -j4`

Expected: FAIL，因为控制器接口尚未实现；不得把失败误判为基线回归。

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp
git commit -m "test: specify cover frame playback behavior"
```

## Task 2: 实现封面播放控制器

**Files:**

- Create: `src/tools/cover_export/CoverFramePlaybackController.h/.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 定义最小控制器接口**

提供 QObject 属性/信号 `playing`, `seconds`, `duration`，以及以下 C++ API：

- `setDuration(double)`、`setSeconds(double)`、`play()`、`pause()`、`toggle()`；
- `seekBy(double)` 用于 Home/End 和离散定位；
- `beginKeySeek(int direction)`、`endKeySeek()`、`cancelInput()`；
- `advanceForElapsed(double)` 作为生产 timer 和确定性 spec 共用的推进入口。

控制器内部用 `QTimer` 以 16 ms 唤醒、`QElapsedTimer` 记录播放/按键持续时间。`advanceForElapsed(double)` 的参数明确是“本次调用以来的秒数增量”，不是绝对时间；播放 tick 计算真实 wall-clock delta 后只调用一次。首次 key press 立即执行 `±1/120` 秒；固定 hold threshold 为 150 ms，threshold 前不运行连续位移，之后调用 `PreviewInteractionConfig` 的共享加速函数。QML 负责过滤 `event.isAutoRepeat` 和修饰键，C++ 只接受一次 `beginKeySeek`；所有推进用 elapsed 时间积分。控制器发出 `secondsChanged`、`playingChanged` 和 `reachedEnd`；`reachedEnd` 由 session 连接到一次末帧提交。结束时精确钳制并停止，不取模循环。

- [ ] **Step 2: 把正式源文件加入测试和应用目标**

将 `CoverFramePlaybackController.h/.cpp` 加入本测试目标和 `MiaCode` 应用源列表；保持测试目标使用与应用相同的 `src/common/PreviewInteractionConfig.h`。然后重新构建同一测试目标，确认失败测试转为可执行而非因 CMake 找不到源文件失败。

- [ ] **Step 3: 运行控制器测试**

Run: `cmake --build build-macos --config Release --target cover_frame_playback_controller_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^cover_frame_playback_controller_spec$' --output-on-failure`

Expected: PASS。

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/tools/cover_export/CoverFramePlaybackController.h src/tools/cover_export/CoverFramePlaybackController.cpp
git commit -m "feat: add cover frame playback controller"
```

## Task 3: 先补 QML/C++ 契约失败回归

**Files:**

- Modify: `src/tools/qml_ui/QmlCoverExportContractSpec.cpp`

- [ ] **Step 1: 增加失败断言**

让现有契约 spec 明确要求：

- session 声明 `chartFramePlaying`、`liveChartSceneBound`、`activeChartFrameSeconds`、`chartFrameDuration` 和活动帧时间/播放 API；
- session/Composer 具有 `bindLiveChartScene` 与身份安全的 unbind；
- `CoverExportPage` 同时绑定 `activeChartFrameKey` 和 `chartSceneBinder`；
- Inspector 只有一个播放控件并连接播放/暂停接口；
- Slider 有 pressed/released 生命周期，QML 走热更新和提交接口；
- `CoverComposer` 使用 `centroid.position`/本地映射，不再用 `scenePressPosition` 做本地命中；
- live scene 未成功绑定时静态图保持可见；
- `MainSplitView` 同时 gate `PreviewPane.visible`、`surfaceActive`，并在封面页屏蔽/关闭 fullscreen preview；
- CMake 包含新的控制器和测试目标。

- [ ] **Step 2: 运行失败契约测试**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^qml_cover_export_contract_spec$' --output-on-failure`

Expected: FAIL，失败项对应尚未实现的契约。

- [ ] **Step 3: Commit**

```bash
git add src/tools/qml_ui/QmlCoverExportContractSpec.cpp
git commit -m "test: pin cover export playback and preview contracts"
```

## Task 4: 将控制器接入 QmlCoverExportSession

**Files:**

- Modify: `src/app/qml_ui/export/QmlCoverExportSession.h/.cpp`
- Create: `src/tools/cover_export/CoverFrameSceneBinder.h/.cpp`
- Create: `src/tools/cover_export/CoverFrameSceneBinderSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 增加 QML-facing 状态和热路径 API**

新增 `chartFramePlaying`、`liveChartSceneBound`、`activeChartFrameSeconds` 属性和对应通知信号；新增 `toggleActiveLayerPlayback()`, `previewActiveLayerFrameSeconds(double)`, `commitActiveLayerFrameSeconds()`, `beginActiveLayerKeySeek(int)`, `endActiveLayerKeySeek()`, `cancelActiveLayerInput()`, `commitActiveLayerGeometry()`, `commitCompositionChanges()` 等 invokable。保留 `setActiveLayerFrameSeconds()` 作为离散提交接口，避免现有文本输入/其他调用失去持久化语义。

调用职责固定为：`previewActiveLayerFrameSeconds()` 只改活动层内存值/共享 playhead/实时 scene；`commitActiveLayerFrameSeconds()` 负责 clamp、一次静态图刷新和 preferences 持久化；`commitActiveLayerGeometry()` 只持久化 `nx/ny/sizeFraction`，不做 chart-frame 截图；`commitCompositionChanges()` 作为布局变更兼容入口。播放结束、滑块释放、Home/End 和文本输入均使用帧时间提交接口。

- [ ] **Step 2: 接入活动层与共享 frame state**

播放/热定位只更新活动 `CoverLayer::frameSeconds` 和 `SceneFrameRenderer::setPlayheadSeconds()`，发出模型已有 NOTIFY，并通过 `setFrameState(frameRenderer_->frameState())`/`update()` 请求 live root 刷新，不调用 `renderAt()`、不写 preferences。切换活动层时执行“停止 → 提交旧层 → 切换 key → 读取新层自己的 `frameSeconds` → 更新共享 playhead”，不得复制旧层时间。

由 session 持有 `CoverFrameSceneBinder`，session 仍是 QML 唯一入口：`CoverExportPage` 将 session 自身作为 `chartSceneBinder` 传给 Composer，session 转发 `bindLiveChartScene(QObject*)`、identity-safe unbind 和 `liveChartSceneBound` 状态。内部 binder 对 root 做类型校验，配置 `PreviewQuickSceneRoot` 的 `kPreviewExportOverlayRenderLayers` 和 borrowed `frameState()`；用 `QPointer` 保存当前 root，旧 Loader 的 delayed teardown 不能解除新绑定。bootstrap 完成后主动重绑已经保存的 root。

- [ ] **Step 3: 先运行 binder 生命周期失败测试，再实现最小绑定**

创建 `CoverFrameSceneBinderSpec`，用两个 `PreviewQuickSceneRoot` 对象验证：旧 root 延迟 unbind 不会解除新 root；`setFrameState(nullptr)` 让绑定状态变为 false；renderer 替换前 detach 会清空 borrowed pointer。先运行并确认失败，再实现 binder 并运行通过。

- [ ] **Step 4: 覆盖所有生命周期入口**

在 `enter/leave`、difficulty 切换、layout import/preset/reset、add/duplicate/remove layer、renderer 替换/失败、chart frame unavailable/duration 变化、session 析构前统一执行停止连续输入和 borrowed-state detach。renderer 销毁前必须先将 live root 的 frame state 置空。

- [ ] **Step 5: 实现提交和导出事务**

提交接口执行一次 `[0, duration]` clamp、`renderChartFrame()` 和 `persistComposition()`。自动播放到末尾也走同一提交路径。`renderChartFrame()` 不得让非活动帧的抓图改变 live scene 的当前 playhead；必要时保存并恢复旧 playhead。导出计划和全量导出恢复事务在 Task 5 完成，Task 4 不提前引用尚未创建的类型。

`exportCover()` 的完整导出事务在 Task 5 接入：停止/提交活动层，保存活动 key/time，暂时 detach live root，使用 `CoverFrameExportPlan` 刷新所有可见 chart-frame 静态图并合成，恢复活动层时间到共享 playhead，页面仍在 cover 时重新绑定并刷新 live root；`setBusy(false)` 放在恢复状态之后。通过 `renderVisibleChartFramesForExport()`/事务 seam 验证多图层不同 `frameSeconds` 各自渲染，非活动帧抓图不会污染共享 playhead，导出后活动时间恢复。

- [ ] **Step 6: 运行契约测试、控制器和 binder 测试**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec cover_frame_playback_controller_spec cover_frame_scene_binder_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^(qml_cover_export_contract_spec|cover_frame_playback_controller_spec|cover_frame_scene_binder_spec)$' --output-on-failure`

Expected: PASS。

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/tools/cover_export/CoverFrameSceneBinder.h src/tools/cover_export/CoverFrameSceneBinder.cpp src/tools/cover_export/CoverFrameSceneBinderSpec.cpp src/app/qml_ui/export/QmlCoverExportSession.h src/app/qml_ui/export/QmlCoverExportSession.cpp
git commit -m "feat: connect cover frame transport to live scene"
```

## Task 5: 固化多谱面帧导出事务

**Files:**

- Create: `src/tools/cover_export/CoverFrameExportPlan.h/.cpp`
- Modify: `src/tools/cover_export/CoverLayoutModelSpec.cpp`
- Modify: `src/app/qml_ui/export/QmlCoverExportSession.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 添加失败的多图层计划测试**

给模型创建至少两个可见 chart-frame layer，设置不同 `frameSeconds`，断言导出计划保留各自 key/time，且计划不以当前共享 playhead 覆盖每层时间；测试同时固定活动层时间的恢复值。

Run: `cmake --build build-macos --config Release --target cover_layout_model_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^cover_layout_model_spec$' --output-on-failure`

Expected: FAIL，因为 `CoverFrameExportPlan` 尚未存在或未接入。

- [ ] **Step 2: 实现计划并接入导出**

让 session 在导出前保存活动 key/time，按计划逐层调用现有 `renderChartFrame(layer, frameSide)`，合成后恢复 renderer playhead 和活动层时间。任何失败路径也执行恢复和 live detach cleanup。

- [ ] **Step 3: 运行多图层回归并 Commit**

Run: `cmake --build build-macos --config Release --target cover_layout_model_spec MiaCode -- -j4 && ctest --test-dir build-macos -C Release -R '^cover_layout_model_spec$' --output-on-failure`

```bash
git add CMakeLists.txt src/tools/cover_export/CoverFrameExportPlan.h src/tools/cover_export/CoverFrameExportPlan.cpp src/tools/cover_export/CoverLayoutModelSpec.cpp src/app/qml_ui/export/QmlCoverExportSession.cpp
git commit -m "test: preserve independent cover chart frame times"
```

## Task 6: 修复 CoverComposer 的拖动和 live fallback

**Files:**

- Modify: `src/intro/qml/CoverComposer.qml`

- [ ] **Step 1: 修复本地坐标命中/位移并提交几何变更**

将 move handler 的 `dragLayerAt()` 输入和移动基准改为 handler parent 的本地 `centroid.pressPosition`/`centroid.position`，保持 normalized `nx/ny` 和 snap/clamp 计算。按视觉 z-order 命中最高可见层；锁定层可选中但不拖动且不穿透。move 和 scale 的正常 release/cancellation 分别调用 `commitActiveLayerGeometry()`，不得触发 chart-frame 离屏截图；`commitCompositionChanges()` 只负责布局持久化的公共兼容入口。

- [ ] **Step 2: 使 live Loader 与静态图状态一致**

让 Composer 绑定 `chartSceneBinder.liveChartSceneBound`，静态 `Image` 仅在该图层不是已成功绑定的活动 live frame 时可见；未 bootstrap、bind 失败或 frame state 暂时为空时显示 cached still。Loader 每个实例保存自己的 bound item，`onItemChanged` 先以旧指针身份 unbind，再 bind 新 item，避免 Repeater 重建时误清新 root。增加 QML 可测的绑定状态/回退条件，覆盖 Loader 先创建后 bootstrap、旧 Loader 延迟析构、renderer 重建和 frame state 置空。

- [ ] **Step 3: Commit**

```bash
git add src/intro/qml/CoverComposer.qml
git commit -m "fix: restore cover layer dragging and live fallback"
```

## Task 7: 在 Inspector 加入播放、滑块和键盘交互

**Files:**

- Modify: `src/app/qml_ui/components/LabeledSlider.qml`
- Modify: `src/app/qml_ui/export/CoverExportPage.qml`
- Modify: `src/app/ui/UiText.cpp` only if needed
- Modify: `CMakeLists.txt` only if QML resources need an entry

- [ ] **Step 1: 暴露 Slider release 生命周期**

保留现有 `moved` 兼容调用，增加 released 信号或等价的 pressed transition；滑块 pressed 必须暂停播放并取消 key hold，`moved` 只调用热更新，released 只提交一次。EditableValue 的 typed commit 直接走提交接口。

- [ ] **Step 2: 添加 Inspector 单播放控件**

在 chart-frame options 中添加一个 `IconButton`，使用现有 `resources/icons/play.svg`/`pause.svg`，tooltip/Accessible.name 走 `UiText`。按钮只在活动谱面帧、renderer/live binding 成功、`chartFrameDuration > 0` 且非 busy 时可用；点击调用唯一的 toggle API。Slider 也必须在活动层不是 chart frame、renderer 不存在、live binding 未成功、`chartFrameDuration <= 0` 或 busy 时 disabled；它单向绑定 `activeChartFrameSeconds`，拖动时间范围为 `[0, chartFrameDuration]`，session 的 NOTIFY 在播放/热定位时保证 UI 跟随。

- [ ] **Step 3: 路由方向键和焦点生命周期**

在时间控制区域使用 `Keys.BeforeItem` 或等效明确路由，消费未带修饰键的 Left/Right/Space/Home/End，避免 Qt Slider 默认步长重复执行；明确检查并过滤 `event.isAutoRepeat`。Left/Right 调用 session begin/end key seek；release、focus out、window deactivation 和页面离开调用 cancel。只在时间控件获得焦点、活动层是 chart frame、live scene bound、`chartFrameDuration > 0` 且非 busy 时处理。

- [ ] **Step 4: 运行契约测试、QML 静态校验并 Commit**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^qml_cover_export_contract_spec$' --output-on-failure`

```bash
git add CMakeLists.txt src/app/qml_ui/components/LabeledSlider.qml src/app/qml_ui/export/CoverExportPage.qml src/app/ui/UiText.h src/app/ui/UiText.cpp src/app/qml_ui/resources/icons/play.svg src/app/qml_ui/resources/icons/pause.svg
git commit -m "feat: add cover frame playback controls"
```

## Task 8: 隐藏主 PreviewPane 和全屏预览

**Files:**

- Modify: `src/app/qml_ui/layout/MainSplitView.qml`

- [ ] **Step 1: 用 coverExportActive 控制普通预览**

将 `PreviewPane.visible` 改为 `!root.coverExportActive`，`surfaceActive` 改为 `!root.coverExportActive && !fullscreenPreview.visible`；保留 `exportPageActive` 的视频导出含义。

- [ ] **Step 2: 处理 fullscreenPreview 生命周期**

`showFullscreenPreview()` 在封面页直接 return；增加 coverExportActive 变化时关闭已打开 fullscreen overlay 的路径。确认离开封面页后普通 PreviewPane 和 fullscreen 入口都恢复。

- [ ] **Step 3: 运行契约测试并 Commit**

Run: `cmake --build build-macos --config Release --target qml_cover_export_contract_spec -- -j4 && ctest --test-dir build-macos -C Release -R '^qml_cover_export_contract_spec$' --output-on-failure`

```bash
git add src/app/qml_ui/layout/MainSplitView.qml
git commit -m "fix: hide editor preview during cover export"
```

## Task 9: 更新跨模块契约文档

**Files:**

- Modify: `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`
- Modify: `.codex/skills/miacode-dev-guide/references/design-ledger.md`

- [ ] **Step 1: 记录新契约**

说明 v2 `QmlCoverExportSession` 取代旧 `CoverComposerView` 作为 QML binder/transport owner；仅一个活动 chart-frame 使用 live root，其余使用 cached still；播放/热定位不截图，提交/导出前截图并恢复共享 playhead；主 PreviewPane 只在 cover page 隐藏。

- [ ] **Step 2: 检查文档差异并 Commit**

Run: `git diff --check`

```bash
git add .codex/skills/miacode-dev-guide/references/cross-chain-linkage.md .codex/skills/miacode-dev-guide/references/design-ledger.md
git commit -m "docs: record cover export transport contract"
```

## Task 10: Release 验证和人工验收

**Files:** none beyond fixes discovered during verification.

- [ ] **Step 1: 构建相关目标**

Before build, read and follow `miacode-concurrent-build` instructions. Use Release configuration only.

Run: `cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DMIACODE_BUILD_DEV_TOOLS=ON -DCMAKE_PREFIX_PATH="$PWD/.qt/6.10.2/macos" && cmake --build build-macos --config Release --target qml_cover_export_contract_spec cover_frame_playback_controller_spec cover_frame_scene_binder_spec cover_layout_model_spec MiaCode -- -j4`

Expected: all requested targets build successfully.

- [ ] **Step 2: 运行回归集**

Run: `ctest --test-dir build-macos -C Release -R '(^cover_frame_playback_controller_spec$|^qml_cover_export_contract_spec$|^cover_frame_scene_binder_spec$|^cover_layout_model_spec$)' --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 3: 最终 QML 静态校验**

Run: `./.qt/6.10.2/macos/bin/qmllint -I build-macos/qml_spec_imports -I src/app/qml_ui -I src/preview/runtime/qml src/app/qml_ui/components/LabeledSlider.qml src/app/qml_ui/export/CoverExportPage.qml src/app/qml_ui/layout/MainSplitView.qml src/intro/qml/CoverComposer.qml`

Expected: no QML syntax/type errors. The executable is the same Qt 6.10.2 installation used by `build-macos`; reconfigure with `-DMIACODE_BUILD_DEV_TOOLS=ON` first if `build-macos/qml_spec_imports` is absent.

- [ ] **Step 4: 检查静态差异和状态**

Run: `git diff --check && git status --short`

Expected: no whitespace errors; only intended source/docs changes remain. Do not add `.superpowers/` visual-companion artifacts unless explicitly requested.

- [ ] **Step 5: GUI 验收**

在真实桌面 GUI 中：拖动卡片、图片、文字和谱面帧；验证锁定层、重叠层、缩放手柄和最终持久化。选中谱面帧后用单按钮播放/暂停，拖动滑块，方向键单击和按住加速，确认末尾停在末帧且不循环。切换图层、difficulty、页面和全屏入口，确认没有后台 timer 或悬空 scene。封面页打开时确认主 PreviewPane 消失且不渲染，返回普通编辑页恢复。最后导出含多个 chart-frame layer 的封面，确认每层仍使用自己的时间。

- [ ] **Step 6: Commit any verification-only fix and report evidence**

任何修复先补失败回归再改实现；完成后记录实际 Release 构建、CTest、QML 校验和 GUI 结果，不把未执行的 GUI 验收写成已通过。
