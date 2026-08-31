# 封面导出后续问题修复设计

## 背景

封面导出 v2 已有播放、图层拖动和预览隔离链路，但 GUI 实测仍发现谱面帧播放控件灰显、进度条不可拖动；右侧检查器仍把难度卡单独放在标签页；画板分辨率选项中的 `1920×1080` 等 Unicode 乘号显示乱码。

## 目标

1. 选中可用谱面帧后，播放按钮与时间滑块可用，实时画面随播放/拖动更新。
2. 播放到末尾暂停并保留末帧，再次播放从 0 秒开始；方向键单步、长按加速和鼠标拖动保持已有契约。
3. 删除独立“难度卡”标签，将难度卡设置放进“图层”标签页的下半部分，并保持单个纵向滚动容器。
4. 点击左侧图层行或画布中的图层后，检查器自动切换到“图层”标签。
5. 所有画板分辨率选项按 UTF-8 正确显示。

## 非目标

- 不改变封面布局 JSON 格式、图层坐标含义或导出渲染架构。
- 不移除 `liveChartSceneBound` 门控；没有实时场景时不能伪装成播放成功。
- 不引入第二套谱面解析、全局预览 transport 或每个播放 tick 的离屏截图。

## 根因与方案

### 谱面帧播放

`CoverExportPage.qml` 将 `session.chartSceneBinder` 传给 `CoverComposer`。但 `CoverComposer` 的 Loader 直接调用 `CoverFrameSceneBinder::bindLiveChartScene()`；这个内部 binder 只保存场景对象，不负责向 `PreviewQuickSceneRoot` 设置 layer flags/frame state，因此 `frameState_` 仍为空，`liveChartSceneBound` 永远为 false，页面的播放门控随之禁用。

修复方式是让 QML 把完整的 session 作为 scene binder 门面传给 Composer。session 的 `bindLiveChartScene(QObject*)` 负责类型校验、设置导出 overlay layer flags、把 renderer 的 borrowed `frameState()` 同时交给 live root 和内部 binder。`unbindLiveChartScene(QObject*)` 继续做对象身份检查。

同时，Loader 增加统一 `syncLiveChartBinding()`：记录 `boundBinder` 和 `boundItem`，在 `itemChanged` 与 `chartSceneBinderChanged` 两条路径都执行“按旧身份解绑 → 更新身份 → 绑定新对象”。这覆盖 Loader 重建、binder 更换以及属性绑定时序变化，不让迟到的旧 Loader 清除新场景。

播放控件继续要求活动层是 chart frame、duration 大于 0、live scene 已绑定且 session 不忙。这样启用状态与画面可更新状态一致。现有控制器的末尾停止和再次从头播放语义保留，并补充 session/契约覆盖。

### A 方案检查器布局

顶部标签只保留：`画板 | 图层 | 预设`。

同一个 `Flickable + ColumnLayout` 内，图层区先放通用属性和按类型显示的图片/文字/谱面帧选项；之后放分隔线和“难度卡设置”标题，再放原难度卡的类型、阴影、等级文字、长文本和字体控件。所有区块使用自然 `implicitHeight`，不嵌套第二个 Flickable、不写固定 y/高度补偿，避免 QML 布局裁剪。

左侧图层行的点击处理先切换 `inspectorTab = "layer"`，再调用 session 选择。画布选中则通过同一个页面选择门面处理，避免无条件监听 `activeLayerChanged` 把用户从画板/预设页意外拉走。

### 分辨率乱码

分辨率数组中的字面量以 UTF-8 保存，却使用 `QString::fromLatin1()` 解码。改为 `QString::fromUtf8()`，不改变数组结构和 QML-facing QVariantMap。

## 测试策略

- 扩展 `qml_cover_export_contract_spec`：要求 QML 传递完整 session binder、Loader 有双路径同步和身份安全解绑；要求不存在独立 card tab、图层选中路由切到 layer；要求 resolutionOptions 使用 UTF-8 解码。
- 保留并运行 `cover_frame_playback_controller_spec` 的末尾停止/再次从头、单步和长按加速测试。
- 保留 `cover_frame_scene_binder_spec`，继续覆盖 frame state 置空和旧 root 销毁通知。
- Release 构建后运行相关 CTest；对修改后的 QML 运行仓库既有 qmllint 校验。

## 验收

在封面导出页选中可用谱面帧，播放按钮不再灰显；点击后画面连续变化，末尾停在最后一帧，再次点击从头播放；拖动进度条和键盘操作正常。右侧只有三个标签，图层页下方显示难度卡设置；点击任意图层会切换到图层页；分辨率下拉框准确显示 `1920×1080 (16:9)`。
