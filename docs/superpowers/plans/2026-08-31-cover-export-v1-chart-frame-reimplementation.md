# 封面导出谱面帧 v1 复刻实施计划

> 当前工作区直接实施，不创建新的 git worktree。

## 目标

让封面导出进入页面后由实时 `PreviewQuickSceneRoot` 立即显示当前谱面帧；静态捕捉降级为切层/启用/导出所需的缓存机制，并保持 macOS scene graph 安全。

## 实施步骤

### 1. 写回归测试（先红）

- 扩展 `QmlCoverExportContractSpec`，锁定 `seedFromDifficulty()` 不再启动进入页面的全量预览捕捉重试。
- 锁定布局恢复会选择首个可见 chartFrame，而不是无条件保留 card。
- 锁定预览捕捉失败不触发进入页面的用户错误通知。
- 保留现有 `CoverFramePlaybackController` 对末尾停驻、空格/方向键状态机的覆盖。

### 2. 重做进入和活动帧状态机

- 从 `seedFromDifficulty()` 删除首屏静态捕捉定时器入口。
- 在 `applyCompositionJson()` 或其调用后的统一恢复步骤中选择首个可见 chartFrame；无可见帧时选择 card。
- 在选择活动谱面帧后同步 `frameSeconds`、playback、renderer state 和 live scene binder。
- 切换活动图层时仅 best-effort 更新旧帧静态缓存，并保留旧缓存。

### 3. 收窄静态捕捉职责

- 删除或停用面向首屏的 `scheduleVisibleChartFramePreviewRefresh()` 重试链。
- 新增/启用/切出实时状态时按需补 still，但不让失败影响实时预览。
- 最终导出继续同步准备所有可见谱面帧，失败仍由导出结果报告。
- 调整 `SceneFrameRenderer` 捕捉窗口，使其不因永久位于不可暴露屏幕外而失败；继续禁止嵌套事件处理和非 GUI 线程抓帧。

### 4. 验证

- 先运行新增测试确认红，再实现使其通过。
- 构建 `MiaCode` 和相关 cover export specs。
- 运行 cover layout、playback、scene binder、QML contract 和渲染策略测试。
- 使用最近的谱面文件启动应用，进入封面导出，验证首屏帧、切层、播放、拖动和导出；检查无新的 `.ips` 崩溃报告。
- 运行 `git diff --check`，检查改动范围和现有用户改动未被覆盖。
