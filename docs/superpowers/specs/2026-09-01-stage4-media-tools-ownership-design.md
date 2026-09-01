# 阶段 4：MediaTools 非 Widget 所有权迁移设计

日期：2026-09-01
状态：已获用户确认；规格审查修订中

## 目标

继续 QML UI v2 阶段 4 的拆除工作，把 `MediaToolsEngine` 六个接口的实现所有权从隐藏的 `MainWindow` 迁移到独立的非 Widget 服务。迁移后，QML 媒体工具调用链不再依赖 `MainWindow` 的继承实现，同时保持现有媒体处理行为、进度/通知语义、预览媒体释放与恢复配对，以及 PV 批量压缩队列的所有权不变。

六个接口为：

- `convertTrackTo44100Hz()`
- `compressBackgroundVideo()`
- `mediaBlankContext(bool)`
- `detectMediaBlankTiming(bool)`
- `restoreMediaBlankBackup(bool)`
- `applyMediaBlank(bool, double, double)`

## 当前状态与约束

`ApplicationServices` 已提供 `MediaToolsEngine*&` slot，`QmlMediaToolsModel` 已通过该 slot 转发 QML 请求，现有实现仍位于隐藏 `MainWindow` 的 dialogs section。阶段 4 前三批的删除和替换改动已存在于工作树中，不能被回滚、覆盖或混入本设计文档提交。

本轮不尝试一次性删除 `src/app/mainwindow/`、`UiComponents`、旧菜单/工具栏壳、`QStyleFactory`、`topLevelWidgets` 或 `main.cpp` 的 `QApplication`。这些对象仍被旧隐藏窗口路径或其他功能使用，留待后续批次在依赖收口后处理。

## 设计

### 1. 新的服务所有权

新增 `MediaToolsService`，放在 `src/app/v2/` 的非 Widget 模块中。服务实现 `MediaToolsEngine`，但不包含 `MainWindow` 或 Qt Widgets 头文件，不拥有 QWidget，也不缓存隐藏窗口的裸指针。

`QmlUiBootstrap` 持有 `std::unique_ptr<MediaToolsService>`，并在创建隐藏 `MainWindow` 前创建服务。服务通过现有的基础服务引用和窄接口访问运行时状态：

- `UiRequestService`：确认、通知和错误提示；成功完成提示继续使用
  `requestNoticeAction`，保留“打开文件夹”动作及其打开产物目录的行为；
- `JobProgressService`：长任务进度、取消和生命周期；
- `ChartWorkspace`/`DocumentBridge`：当前文件、文档文本、难度和媒体路径等权威文档状态；
- `PreviewSurface*&` slot：每次操作时动态解析预览侧实现，完成文件操作前的 quiesce 和操作后的恢复。

服务不把这些 slot 转换成长期缓存的实现指针；调用时从 slot 读取，关闭时按明确顺序清空 slot。

### 2. 启动和销毁顺序

启动顺序为：

1. 创建 `ApplicationServices`；
2. 创建并持有 `MediaToolsService`，注册 `MediaToolsEngine` slot；
3. 创建隐藏 `MainWindow`，由它注册剩余的旧接口；
4. 创建 QML context、模型和 engine。

关闭顺序为：

1. 先销毁 QML engine/root/context，停止新的 QML 请求；
2. 清空 `MediaToolsEngine` slot；
3. 销毁 `MediaToolsService`；
4. 销毁隐藏 `MainWindow`，由其清理剩余接口 slot；
5. 最后销毁 `ApplicationServices`。

该顺序避免服务销毁后 QML 仍能调用悬空接口，也避免 `MainWindow` 销毁时错误地清理已转移的媒体工具实现。其余七个旧接口在本轮继续由隐藏 `MainWindow` 提供。

### 3. 媒体文件操作协调

媒体处理会继续遵守现有的文件操作协议。`PreviewSurface` 增加两个明确的
文件操作协调方法：`beginMediaFileOperation()` 和
`endMediaFileOperation(bool reloadTrack)`。前者执行停止播放、清 route、释放
decoder 和必要的事件排空，只有在预览已 quiesce 后才返回 `true`；后者只允许
在前者返回 `true` 后调用，负责一次恢复/重载并返回恢复是否成功。服务以这
一对调用构成 transaction，不把“已调用 begin”误认为“已 quiesce”。

服务可以在 begin 前读取并校验当前文档快照和目标媒体路径；如果路径或文档
状态无效，则直接报告错误，不进入文件操作 transaction。随后调用 begin。
如果 `PreviewSurface` slot 为空、`beginMediaFileOperation()` 返回 `false`，或
在关闭竞态中无法确认预览已 quiesce，服务必须拒绝本次文件修改，向用户
报告错误，并且不得运行 ffmpeg、备份恢复或媒体空白填充。只有 begin 返回
`true` 后才执行以下步骤：

1. 调用 `beginMediaFileOperation()`；该调用内部一次性停止/释放预览，服务不
   再重复请求 stop、clear 或 release；
2. 执行 ffmpeg、备份恢复或媒体空白填充；
3. 无论成功、失败还是取消，都调用一次 `endMediaFileOperation(reloadTrack)`，
   恢复预览 route；需要时重载 track、波形和相关运行时状态。每个 begin 成功
   的 transaction 恰好结束一次；若结束返回 `false`，仍报告恢复失败，但不得
   重复结束或再次写文件；
4. 通过 `UiRequestService` 报告结果。成功产物提示必须调用
   `requestNoticeAction`，其 action callback 使用 `QDesktopServices` 打开
   产物所在目录，保持现有“打开文件夹”用户行为。

为此，在 `PreviewSurface` 增加最小的文件操作协调接口，并由现有 PreviewSection
复用实际的停止、释放、route 同步和重载逻辑。服务只依赖该接口，不直接访问
MainWindow 的 UI 引用。协调失败是硬失败，不允许“无预览时继续写文件”的隐式
分支；关闭时先清空 MediaTools slot 并阻止新请求，再销毁服务，服务的待处理
确认/通知 continuation 必须通过生命周期 token 失效，不能回调已析构的服务。
TrackMetadata 仍使用旧 dialogs 路径的共享辅助函数；本轮不会为了迁移
MediaTools 而删除尚有调用者的旧 helper。

### 4. 数据流与兼容性

数据流保持为：

`QmlMediaToolsModel -> MediaToolsEngine slot -> MediaToolsService -> 基础服务 / ChartWorkspace / PreviewSurface slot`

`QmlMediaToolsModel` 的公开 QML API 不变，PV 批量压缩扫描和队列仍由模型持有。媒体空白上下文与探测结果继续返回现有 `QVariantMap` 结构；文档字段从 workspace/document bridge 读取，不再从 `metadataExtraEdit_` 等 QWidget 控件读取。若旧实现依赖的字段目前尚未暴露，优先增加文档/服务层的窄只读访问，不把 Widget 引用泄漏到新服务。

旧 QAction/隐藏窗口入口在本轮保留，但压缩和 44100Hz 转换两个 QAction
必须改成仅调用 `ApplicationServices::mediaToolsEngine()` 的薄转发；不得再
调用 `DialogsSection::onCompressBackgroundVideo()` 或
`DialogsSection::onConvertTrackTo44100Hz()` 的完整旧实现。媒体空白等入口也
必须统一走同一个 `MediaToolsEngine` slot。这样隐藏窗口仍可承载未迁移的旧
功能，但不存在两套 MediaTools 行为或不同的确认/恢复语义。

## 文件范围

预计修改范围如下，最终以依赖审计为准：

- 新增 `src/app/v2/MediaToolsService.h/.cpp`；
- 修改 `QmlUiBootstrap` 的创建、slot 注册和关闭顺序；
- 扩展 `PreviewSurface` 的媒体文件操作协调契约，并接入 MainWindow PreviewSection；
- 移除 `MainWindow` 对 `MediaToolsEngine` 的继承、override 声明、slot 注册/清理和旧转发实现；
- 将 dialogs 中的 MediaTools 实现迁移到服务，保留仍被 TrackMetadata 使用的共享 helper；
- 增加独立的 MediaTools ownership/residue 规格守卫及 CMake/CTest 注册；
- 更新实际阶段 4 Todolist
  `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`、MiaCode feature index 和
  受影响的跨模块契约说明。

不修改工作树中与本轮无关的现有改动，也不在本轮宣称 `MainWindow` 已完全删除。

## 测试与验证

实现遵循测试先行：先增加能在旧实现上描述当前契约的 characterization/spec 测试，再迁移实现并让测试通过。至少覆盖：

- 成功执行、错误和用户取消的进度/通知结果；
- 文件操作前释放与操作后恢复的成对调用；
- track 重载、波形/预览 route 恢复；
- slot 注册、清空和 `QmlUiBootstrap` 关闭顺序；
- 新服务源码不依赖 `MainWindow`/Qt Widgets；
- QML MediaTools API 与 PV 批量队列未回归。

先运行受影响的 focused test/spec 和 `git diff --check`，再使用现有 `build-macos` 的 Release 配置构建受影响目标并运行相关 CTest；必要时补充完整的 Release 验证。提交前检查完整 diff，确认阶段 4 前三批的用户改动均保留。

## 风险与回退

最大风险是旧媒体实现隐含依赖 QWidget 字段或 MainWindow 私有状态，导致新服务无法得到同等信息。处理顺序是先补充窄的非 Widget 读接口或共享协调接口，再迁移实现；不以复制第二套实现来规避依赖。本轮的完成条件是六个接口全部由新服务实现，旧入口全部经同一个 slot 薄转发；如果某个行为仍无法等价表达，则停止本批并报告阻塞，不提交 MainWindow 与新服务并存的半迁移状态。

回退时可恢复 `MainWindow` 的 `MediaToolsEngine` 继承与 slot 注册，将 QML slot 指回旧实现；新服务和契约改动应保持可独立删除，不影响阶段 4 前三批已完成的拆除。
