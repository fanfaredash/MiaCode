# Cover Studio 封面导出实现计划

Date: 2026-06-25
Status: implementation-aligned plan

> ⚠ **UI 部分已被 `COVER_STUDIO_UI_REDESIGN_SPEC_ZH.md`（2026-06-25 重设计）取代。**
> 本文的渲染核心 / 数据模型 / `.miacover` JSON / 导出流程仍然有效并以本文为准；
> 但工作台交互层（三栏布局、检查器、播放条、图层操作、勾选框、快捷键）以重设计 spec 为准。

## 验收状态（2026-06-25）

Cover Studio UI 重设计已验收完毕，已有内容基本符合需求。当前仍需补充的功能记录在 `COVER_STUDIO_UI_REDESIGN_SPEC_ZH.md` 的 §13，包括图层列表顶部越界拖拽落位、点击画布空白取消选择、新谱面帧继承上次谱面帧配置、交叠图层点击只命中一个图层，以及暂缓的画布视觉缩放/还原能力。

## 目标

本文重写早期 Cover Export UI Research，将研究结论、用户反馈和当前实现统一成一份可执行的封面导出计划文档。

本轮目标不是引入通用图片编辑器，而是在 MiaCode 现有封面渲染链路上完成可维护的 Cover Studio：

- 支持多个谱面帧 chart-frame 图层；
- chart-frame 的时间、内圈背景、亮度和透明度都作为图层属性保存；
- 使用固定工作台窗口，避免旧 dialog 控件和新图层面板混在一起；
- 保留现有导出入口、标题、按钮等既有用户可见文案；
- 新增图层/属性/帧操作提供中文文字、中文 tooltip 和 accessible text；
- 不引入外部 UI 依赖，继续使用 Qt Widgets + QML + MiaCode 自有渲染核心。

## 当前基线

封面导出已经具备可复用的渲染核心，应继续保留并围绕它扩展：

- `src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp`
  - 从主窗口导出流程构造 seed `VideoExportTask`；
  - 打开封面编辑器；
  - 导出封面到谱面旁边。
- `src/tools/cover_export/ExportCoverDialog.*`
  - 保留兼容壳；
  - 不再承载主要编辑界面。
- `src/tools/cover_export/CoverStudioWindow.*`
  - 独立固定工作台窗口；
  - 负责顶层布局、屏幕尺寸适配和 toolbar action wiring。
- `src/tools/cover_export/CoverStudioPanel.*`
  - Cover Studio 的状态协调器；
  - 承接预览、布局导入导出、最终导出、播放/拖动、active layer、still 缓存刷新等逻辑。
- `src/tools/cover_export/CoverLayoutModel.*`
  - 管理稳定 `card` 图层和任意多个 `chartFrame` 图层；
  - 提供几何、显隐、锁定、透明度、z-order、chart-frame 样式属性。
- `src/tools/cover_export/CoverCompositionState.*`
  - 负责 v2 `.miacover` JSON、偏好恢复和 v1 迁移。
- `src/tools/cover_export/CoverComposerView.*`
  - 嵌入 `CoverComposer.qml`；
  - 维护一个 live chart scene；
  - 提供最终封面组合导出。
- `src/tools/cover_export/SceneFrameRenderer.*`
  - 按指定秒数渲染谱面帧 still。
- `src/intro/qml/CoverComposer.qml`
  - 负责背景、难度卡、chart-frame 图层、拖动缩放、辅助线和最终组合。

## 已采纳的研究结论

早期调研确认了几个边界，本计划沿用这些结论：

- 不集成 Krita、PhotoFlare、OpenToonz 等完整图片编辑器。
- 不集成 kImageAnnotator 或 KQuickImageEditor；它们的依赖和文档模型都不适合 MiaCode 的谱面帧渲染。
- 不引入 QtPropertyBrowser；当前属性数量适合用定制 inspector。
- 第一版不引入 Qt Advanced Docking System，也不继续使用可拖拽 `QDockWidget`。
- QtBitmapEditor 只作为图层列表交互参考，不作为代码依赖。

实际产品方向是：MiaCode 自有渲染核心 + 固定 native Qt 工作台 + 定制图层/属性/帧面板。

## 工作台结构

Cover Studio 使用固定根布局，不允许浮动、关闭、拖出或重新停靠面板。

```text
+----------------------------------------------------------------+
| toolbar: 保存布局 / 导入布局 / 重置 / 导出 / 关闭或取消          |
+-------------+-------------------------------+------------------+
| 图层列表     |  CoverStudioPanel 预览画布     | 属性 + 旧设置区域 |
|             |  CoverComposerView            |                  |
+-------------+-------------------------------+------------------+
| CoverFramePickerPanel: 当前 chart-frame 的时间选择与播放控制     |
+----------------------------------------------------------------+
```

职责划分：

- `CoverStudioWindow`
  - 创建固定三栏加底部帧选择器；
  - 创建顶部 toolbar；
  - 根据父窗口所在屏幕自动适配初始尺寸；
  - 不直接修改模型内部状态。
- `CoverStudioPanel`
  - 作为唯一状态协调器；
  - 对外暴露 layer、property、action API；
  - 隐藏旧 dialog 的右侧控件列和底部按钮区；
  - 通过 `takeSettingsPanel()` 把仍需保留的尺寸/背景/card 设置交给右侧区域承载。
- `CoverLayerListPanel`
  - 提供选择、显示/隐藏、锁定、添加谱面帧、复制、删除、上移、下移、置顶、置底。
- `CoverInspectorPanel`
  - 编辑当前图层的 visible、locked、opacity、位置、大小；
  - 对 chart-frame 额外编辑 frameSeconds、frameBgEnabled、frameBgBrightness。
- `CoverFramePickerPanel`
  - 只服务当前选中的 chart-frame；
  - 切换选中图层时同步 active frame；
  - 非 active chart-frame 回落到 cached still。

## 窗口尺寸策略

Cover Studio 打开时必须完整落在当前屏幕可用区域内。

规则：

- 优先使用父窗口所在 screen 的 `availableGeometry()`；
- 如果父窗口无 screen，则 fallback 到 primary screen；
- 默认尺寸取可用区域约 90%；
- 实际尺寸不得超过可用区域；
- minimum size 在小屏或高 DPI 下可按可用区域下调；
- 窗口打开后居中显示；
- 不再把 `1280x860` 作为无条件初始尺寸。

## 数据模型

`CoverLayer` 是封面图层的持久模型。所有几何信息都保存为相对画布的归一化值，确保预览和导出使用同一个布局。

稳定字段：

- `key`
- `kind`
- `label`
- `nx`
- `ny`
- `sizeFraction`
- `z`
- `visible`
- `locked`
- `opacity`

chart-frame 专属字段：

- `frameSeconds`
- `frameBgEnabled`
- `frameBgBrightness`
- `frameStyle`
- `imageRevision`
- `frameImage`，仅运行时缓存，不直接写入 JSON。

图层规则：

- `card` key 保持稳定；
- `card` 默认不可删除，但允许隐藏、锁定、移动和编辑；
- chart-frame 可以添加、复制、删除、排序；
- UI 选择态由 Cover Studio 持有，不写入布局文件；
- z-order 是最终绘制顺序的来源，必要时通过 `normalizeZOrder()` 归一化。

## JSON 与迁移

当前 `.miacover` 使用 v2 schema：

```json
{
  "kind": "miacode.cover",
  "version": 2,
  "size": {},
  "background": {},
  "card": {},
  "layout": {
    "layers": []
  }
}
```

迁移规则：

- v1 的单个 `chartFrame` 迁移为一个 v2 `chartFrame` layer；
- v1 根级 `chartFrame.innerBackground` 写入该 layer 的 `frameBgEnabled`；
- v1 根级 `chartFrame.innerBrightness` 写入该 layer 的 `frameBgBrightness`；
- `card` key 保持稳定，避免旧布局丢失卡片设置；
- 读取失败时保留可恢复路径，不应阻塞 card-only 导出。

## QML 与 live scene

`CoverComposer.qml` 继续使用 `Repeater` 渲染 `CoverLayoutModel.layers`。

关键约束：

- `CoverComposerView` 只维护一个 live `PreviewQuickSceneRoot`；
- root 暴露 `activeChartFrameKey`；
- 只有 active chart-frame 使用 live scene；
- 非 active chart-frame 使用 `image://coverchart/<key>` cached still；
- chart-frame 的背景、亮度、透明度从图层属性读取，不再使用单个全局设置；
- 拖动、缩放、锁定、显隐等交互继续通过模型属性驱动。

这个策略避免同时创建多个 live 预览场景，同时仍允许导出前逐层刷新 still。

## 导出流程

导出前应冻结播放状态并刷新所有可见谱面帧。

流程：

1. 停止 playback。
2. 遍历 `visibleChartFrameLayers()`。
3. 对每个 chart-frame：
   - 根据 layer 的 `frameSeconds` 设置 renderer 时间；
   - 按最终输出尺寸和 layer 大小计算 still 像素边长；
   - 调用 `SceneFrameRenderer::renderAt(seconds, sidePx)`；
   - 用 `CoverLayoutModel::setLayerImage(key, image)` 更新缓存。
4. 调用 `CoverComposerView::renderCoverComposite(...)` 输出最终组合图。
5. 透明背景导出 PNG，非透明背景导出 JPG。

导出结果必须满足：

- 多个 chart-frame 可以使用不同时间、位置、大小、透明度和亮度；
- 预览与导出共享同一个 `CoverLayoutModel`；
- 无 note 或 skin 失败时仍可导出 card-only 封面；
- 导出阶段渲染 still 的清晰度按最终输出尺寸计算，而不是复用低分辨率预览图。

## 本地化与文案

产品文案约束：

- 既有入口、标题、导出、取消、保存布局、导入布局等用户可见文案不改名；
- 新增功能必须补中文；
- 图标按钮必须有中文 tooltip；
- 没有图标库时可以使用简短符号按钮，但 tooltip 和 accessible text 必须中文化。

新增中文词汇应保持简短直接：

- 图层
- 属性
- 帧
- 显示
- 锁定
- 不透明度
- 位置
- 大小
- 帧背景
- 亮度
- 添加谱面帧
- 复制
- 删除
- 上移
- 下移
- 置顶
- 置底

## 构建与测试

Release 构建：

```powershell
cmake --build build --config Release --target MiaCode
```

模型测试：

```powershell
ctest --test-dir build -C Release -R cover_layout_model_spec --output-on-failure
```

`cover_layout_model_spec` 应覆盖：

- 多 chart-frame 增删复制；
- z-order 调整与归一化；
- `card` 删除约束；
- v1 到 v2 迁移；
- v2 JSON round trip；
- chart-frame 图层属性持久化。

## 手动验证清单

- 小屏或高 DPI 缩放下打开 Cover Studio，窗口不超出屏幕。
- 界面只出现一套布局控件，没有旧右侧控件列和新面板重复出现。
- 图层、属性、帧面板不可拖出、不可浮动、不可关闭。
- 新增图层、复制、删除、上移、下移、置顶、置底、显示/隐藏、锁定都有中文文字或 tooltip。
- 选择不同 chart-frame 时，只有当前图层是 live scene，其余显示 cached still。
- 三个 chart-frame 设置不同时间、位置、亮度和不透明度后导出正确。
- 透明背景导出 PNG，非透明背景导出 JPG。
- 无 note 或 skin 失败时 card-only 仍可导出。
- 导入旧 `.miacover` 不丢 card、chart-frame、inner background、brightness 设置。

## 后续边界

本轮完成 fixed workbench 和多 chart-frame 的基础能力。后续如果继续增强，应保持以下边界：

- 不因为单个控件需求引入大型编辑器或重型 UI 依赖；
- 如果需要图层缩略图，优先在 `CoverLayerListModel` 上生成轻量 preview；
- 如果 inspector 属性继续增长，先整理本地控件分组，再重新评估通用 property browser；
- 只有在确实需要可保存/可恢复的浮动工作区时，才重新评估高级 docking 系统；
- 独立可执行文件可以作为后续目标，但应复用 `CoverStudioPanel`，不要复制导出逻辑。

## 维护提示

修改 Cover Studio 时需要同步检查：

- `CoverStudioWindow.*` 的固定布局和屏幕适配；
- `CoverStudioPanel.*` 的状态协调 API；
- `CoverLayoutModel.*` 与 `CoverCompositionState.*` 的持久 schema；
- `CoverComposerView.*` 与 `CoverComposer.qml` 的 active live frame / cached still 约定；
- `SceneFrameRenderer.*` 与视频导出 Quick 渲染设置的同步风险；
- MiaCode dev guide 中 `feature-index.md`、`cross-chain-linkage.md`、`design-ledger.md` 的 Cover Studio 记录。
