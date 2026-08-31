# 封面导出交互修复设计

## 背景

最近迁移到 v2 QML 页面后，封面导出工作区出现三个用户可见问题：画布中的图层无法拖动；谱面帧只能通过时间滑块查看，不能播放；封面导出页面打开时，主编辑器的谱面预览仍然占用空间和渲染资源。

本设计针对当前的 `CoverExportPage`、`CoverComposer`、`QmlCoverExportSession` 和 `SceneFrameRenderer` 组合，保留现有导出渲染链路，不引入第二套谱面解析或预览模型。

## 目标

1. 所有未锁定的可见封面图层都能在中央画布中拖动，并保持现有的选中、z 顺序和缩放手柄行为。
2. 选中的谱面帧可以通过 Inspector 中的单个按钮播放/暂停；播放到末尾停止并停留在末帧，不循环。
3. 鼠标拖动时间滑块可以定位谱面时间；定位时暂停播放。
4. 方向键单击移动固定小步，按住后连续加速移动；释放按键、焦点丢失、切换图层或离开封面页时停止连续移动。
5. 封面导出页激活时隐藏并停用主编辑器 `PreviewPane`；离开后恢复。
6. 播放和拖动期间保持 QML 画面流畅，不在每个 tick 写文件或做离屏截图。

## 非目标

- 不改变封面布局 JSON 的格式和图层坐标语义。
- 不把封面导出工作区并入全局 `QmlPreviewModel` 播放控制。
- 不把 `SceneFrameRenderer` 的离屏 `QQuickWindow` 重新挂到可见 QML 场景。
- 不改变最终封面导出的离屏合成方式。

## 用户交互

### 图层拖动

`CoverComposer` 保留一个画布级 `DragHandler`，不为每个图层增加竞争性的拖动处理器。命中检测和拖动位移统一使用 `CoverComposer` 本地坐标：启动时捕获命中的图层及其归一化 `nx/ny`，后续使用本地坐标差换算为画布宽高比例。

命中规则以视觉顺序为准：选择命中区域内可见图层中 z 值最高者；锁定层可以被选中但不移动，也不穿透锁定层去移动其下方图层；缩放手柄优先由缩放处理器接管。拖动结束后由 session 持久化最终位置，而不是在每个移动事件中写布局文件。缩放手柄的正常释放和 pointer cancellation 也必须提交最终 `sizeFraction`。

### 谱面帧播放和定位

播放控件位于右侧 Inspector 的谱面帧设置区域，符合已选择的 B 方案。当前选中图层不是谱面帧时，该区域和播放控件不显示。

- 播放按钮只有一个，图标/可访问名称随播放状态变为播放或暂停。
- 点击播放时从当前时间继续；如果当前已经在末帧，则从 0 秒重新开始。
- 播放时间由单调时钟计算，不依赖计时器触发次数；每次更新都钳制到 `[0, duration]`。
- 到达 duration 后保留末帧、自动停止，并发出播放状态变化。
- 时间滑块使用现有控件。用户拖动时立即更新共享 playhead 并刷新可见谱面帧；该操作会暂停播放。
- 滑块键盘事件和页面快捷键不重复移动：时间滑块获得焦点时由页面显式处理左右键，消费事件并避免 Qt Controls 的默认步长叠加。

### 方向键

封面页在可编辑状态且焦点位于时间控制范围时处理未带修饰键的 Left/Right：

- 非自动重复的第一次按下立即移动 `1/120` 秒。
- 系统自动重复事件被吞掉，避免系统重复速度和应用加速叠加。
- 首次按下后先经过明确的 hold threshold；threshold 前释放不会产生第二次位移。之后由封面播放控制器按固定 tick 更新，并使用现有 `PreviewInteractionConfig` 的小步、加速度和最大速率参数。
- KeyRelease、FocusOut、窗口失活、切换活动图层、离开封面页都会取消连续移动。
- 任何手动定位都会暂停播放；移动不会越过 0 或 duration。

Space 仅在时间控制获得焦点且未带修饰键时切换播放/暂停；Home/End 定位到首帧/末帧并暂停，作为已有封面工作区键盘契约的一部分保留。

## 结构设计

### 播放控制器和 session

在 `QmlCoverExportSession` 内增加封面专用播放控制状态（如拆为小型 `CoverFramePlaybackController`，则该类只负责时间推进，session 负责图层和持久化）。session 是 QML 唯一入口，负责：

- 暴露当前活动谱面帧的播放状态和时间；
- 开始/暂停播放；
- 开始、持续和结束方向键定位；
- 开始、持续和结束鼠标滑块定位；
- 在图层切换、页面退出、导出前停止并提交当前静态图；
- 把共享 playhead 的变化通知 live scene。

时间推进使用 `QTimer` 作为刷新唤醒源，使用 `QElapsedTimer` 作为真实经过时间来源。每个 tick 只更新 `SceneFrameRenderer` 的 `PreviewFrameState::playheadSeconds`、活动图层的内存时间和 live `PreviewQuickSceneRoot::update()`；不调用 `renderAt()`，不触发图片 provider 的读回。

现有 `setActiveLayerFrameSeconds()` 继续作为离散提交接口；新增的播放/滑块热路径接口只更新内存中的活动层时间和共享 playhead。滑块需要暴露按下/释放生命周期：按下时暂停，`moved` 热更新，释放时调用提交接口；文本输入等非拖动修改直接走提交接口。提交接口负责一次静态图刷新和布局持久化。

### 可见 live chart scene

进入 session 并成功 bootstrap 后，`CoverExportPage` 将 session 作为 `chartSceneBinder` 绑定给 `CoverComposer`，并同时绑定 `activeChartFrameKey`。session 对外提供现有 Composer 所需的 `selectLayerKey(key)` 和新增的 `bindLiveChartScene(QObject*)`，后者验证 `PreviewQuickSceneRoot` 类型、设置 `kPreviewExportOverlayRenderLayers` 并绑定 renderer 的 `frameState()`；绑定对象用 `QPointer` 跟踪。

`CoverComposer` 只给当前活动、可见的谱面帧创建一个 `PreviewQuickSceneRoot`，它读取 `SceneFrameRenderer` 持有的同一份 `PreviewFrameState`。静态图不能只以 Loader 是否 active 判断，而要以真实的 `liveChartSceneBound` 状态判断：只有 root 已成功绑定且持有有效 frame state 时隐藏静态图并启用播放按钮，否则显示静态图并禁用播放。Loader 先创建、renderer 后 bootstrap 时，session 保存 root，并在 bootstrap 完成后主动重绑。

离屏 renderer 的 scene root 继续由其离屏窗口拥有，不能重新 parent 到封面页。可见 root 只借用 frame state 指针；session 在 renderer 重建或销毁前先解除引用，binder 使用 `QPointer` 和对象身份检查防止 Loader 重建时旧 root 清掉新 root。

当 live scene 不可用时，现有 `coverchart` 图片 provider 的静态图继续作为 fallback。暂停、滑块释放、切换活动图层以及导出前，session 按需调用 `renderAt()` 更新静态图并持久化最终时间；导出仍然对所有可见谱面帧按导出尺寸重新渲染。播放、滑块热更新和键盘定位只在 live scene 已绑定时可用。

### 拖动与持久化

QML 负责本地指针坐标到归一化坐标的计算，C++ `CoverLayer` setter 发出属性变化。拖动结束时调用 session 的提交接口，确保 JSON 文件和后续导出使用最终位置。选择、锁定、可见性和 z 顺序仍由现有 session API 控制。

### 主编辑器预览

`MainSplitView` 已有 `coverExportActive` 状态。封面页激活时：

```qml
visible: !root.coverExportActive
surfaceActive: !root.coverExportActive && !fullscreenPreview.visible
```

这样既隐藏主预览的布局占位，也停用其 Loader/场景渲染；封面页中央 `CoverComposer` 不受影响。离开封面页时由同一个状态绑定恢复。

## 状态和生命周期

1. `enter()` bootstrap renderer，建立 session 状态，然后绑定 Composer 的 live scene；如果 Loader 先于 bootstrap 创建，bootstrap 完成后主动补绑。
2. 选择非谱面帧或无可用 chart frame 时停止播放并解除 live scene 的活动显示。
3. 选择新的谱面帧时先停止旧帧播放并提交旧帧；切换后从新图层自己的 `frameSeconds` 恢复共享 playhead，不能继承旧图层时间。新建图层使用模型默认时间。
4. `leave()`、窗口失活、焦点丢失和导出开始时停止所有连续输入。
5. 切换 difficulty、导入布局、应用 preset、reset、增加/复制/删除图层、renderer bootstrap 失败或替换、`chartFrameAvailable`/duration 失效时，统一执行“停止 → 提交 → 解绑 → 修改模型或 renderer → 按新活动层重绑”。
6. 在 renderer 被释放前，先将 live scene 的 frame state 设为 null，再销毁 renderer；旧 Loader 的延迟析构不能清掉新 root 的绑定。
7. 每次导出开始前刷新所有可见谱面帧的静态图，保证导出不依赖 GUI timer 或可见 scene 的瞬时状态。导出事务保存活动 key 和其 `frameSeconds`，停止输入并完成合成后，将共享 playhead 恢复为活动层时间；页面仍处于封面页时重新绑定并刷新 live scene。播放自然到末尾也必须提交末帧静态图。

## 测试和验收

### 自动化

- 增加播放控制器的确定性测试：首帧/末帧钳制、到末尾停止、暂停不推进、方向键单击步长、按住加速、释放/失焦取消。
- 扩展 `qml_cover_export_contract_spec`：检查本地坐标命中、Inspector 单按钮、live binder、PreviewPane 双重 gating 和关键 session API。
- 保留并运行现有 cover layout model 和导出合成测试。
- Release 配置下运行相关 CTest；对修改后的 QML 运行 qmllint 或项目既有 QML 校验命令。

### GUI 验收

- 在中央画布分别拖动卡片、文本、图片和谱面帧；确认选中层、重叠层、锁定层和缩放手柄行为。
- 选中谱面帧，点击播放/暂停，确认画面连续更新且到末尾停止在末帧。
- 拖动滑块、左右键单击、按住左右键并松开，确认时间不越界且播放状态正确。
- 播放过程中切换图层、切换页面、点击其他控件，确认不会留下后台 timer 或悬空 scene 引用。
- 打开封面导出页确认主 `PreviewPane` 消失且不渲染；返回普通编辑页确认恢复。
- 最终导出并确认多谱面帧图层均使用各自最后提交的时间。

## 方案取舍

- “每个播放 tick 离屏截图”实现简单，但会把导出读回放入交互热路径，不能满足流畅播放，因此仅保留为 fallback。
- “全 QML Timer/状态”不能可靠覆盖 headless 导出、键盘加速和 C++ 生命周期，因此不采用。
- “复用主编辑器全局预览 transport”会让封面编辑状态与主编辑器耦合，也违背封面页独立工作区边界，因此采用封面 session 专用控制器。

全屏预览是独立覆盖层，不能只隐藏普通 `PreviewPane`：`showFullscreenPreview()` 在 `coverExportActive` 时拒绝打开，且 `coverExportActive` 变为 true 时关闭已打开的全屏层。`PreviewPane.exportPageActive` 保持原有视频导出语义不变。

播放按钮、暂停状态、键盘提示和不可用状态均通过 `UiText` 提供本地化文本及 accessible name；duration 为 0、无 renderer、live scene 未绑定或 session busy 时，播放和时间定位控件禁用。
