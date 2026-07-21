# 封面图层扩展 & 难度卡自定义字体 · 调研与方案

Date: 2026-07-21
Status: research + implementation plan（未实现，待排期）
Scope: 封面导出新增图层类型（自定义图片 / 自定义文字）+ 难度卡自定义字体（封面 **与** 片头两处 UI）

> 本文只是调研结论与实现方案，尚未落地。渲染核心 / 数据模型 / `.miacover` JSON 以
> [`COVER_EXPORT_UI_RESEARCH.md`](COVER_EXPORT_UI_RESEARCH.md) 为准，工作台交互层以
> [`COVER_STUDIO_UI_REDESIGN_SPEC_ZH.md`](COVER_STUDIO_UI_REDESIGN_SPEC_ZH.md) 为准。

---

## 1. 需求

1. 封面导出支持更多图层类型：**自定义图片**、**自定义文字**。
2. 难度卡支持**自定义字体**：不仅封面导出 UI 要有字体切换选项，**导出-片头 UI 也要有**。

---

## 2. 现状架构（三个功能共用的地基）

### 2.1 图层模型

- `CoverLayer`（`QObject` + `Q_PROPERTY`）+ `CoverLayoutModel`（有序列表 + JSON 序列化）。
  见 [`CoverLayoutModel.h`](../../../src/tools/cover_export/CoverLayoutModel.h)。
- 当前只有两种 `kind`：`"card"`、`"chartFrame"`（`"chartFrame2"` 只是第二帧的 key，不是新类型）。
- **通用几何字段** `nx / ny / sizeFraction / z / opacity / visible / locked` 以及拖拽、缩放、
  吸附、命中测试（`hitKeyAt`）**全部与 kind 无关**，任何新 kind 都能直接复用。

### 2.2 渲染与导出（同一份 QML）

- [`CoverComposer.qml`](../../../src/intro/qml/CoverComposer.qml) 的 `Repeater` delegate 按
  `kind` 用 `Loader` + `Component` 分发：`cardComponent`（`MaimaiBannerCard`）/
  `liveChartComponent`（`PreviewQuickSceneRoot`）/ 静态帧 `Image`。
- 实时预览和导出走**同一份 QML**：导出经 `renderCoverComposite()` 离屏
  `grabWindow`（[`CoverComposerView.cpp`](../../../src/tools/cover_export/CoverComposerView.cpp)）。
  → **新 kind 一次编写，预览与导出同时生效。**
- `layerContentW/H` 目前对非卡片图层一律按正方形（aspect = 1.0）。

### 2.3 图层序列化

- `toJson/fromJson` 的字段是**硬编码**的（见
  [`CoverLayoutModel.cpp`](../../../src/tools/cover_export/CoverLayoutModel.cpp) 约 588 / 617 行）：
  `key/kind/nx/ny/sizeFraction/z/visible/locked/opacity/frame*`。新 kind 的字段要在这里补齐，
  并对旧存档做兼容（缺字段用默认值）。

### 2.4 难度卡与字体

- 难度卡 = [`MaimaiBannerCard.qml`](../../../src/intro/qml/MaimaiBannerCard.qml)（透明模式）。
- 两个 `FontLoader`（约 78–91 行）：
  - `displayFont`（标题）← `template.fonts.display`
  - `bodyFont`（正文）← `template.fonts.body`
  - 路径 = `(template.fontsRoot || "qrc:/intro/assets/fonts") + "/" + <文件名>`。
- **数据驱动已就绪**：字体来自 `template.fonts`，改字体本质上是改这个 map。

---

## 3. 调研结论：难度卡现在用了哪几种字体

**只有 2 个内置字体**，都是 **Resource Han Rounded CN（思源圆体，OFL 授权）**，
是 SEGA 专有圆体（原 M PLUS 1p Black / Rounded 1c Bold）的免费替代：

| 用途 | 文件 | 卡面元素 |
| --- | --- | --- |
| display（标题） | `ResourceHanRoundedCN-Heavy.ttf` | 曲名、`text` 模式的 LV 数字 |
| body（正文） | `ResourceHanRoundedCN-Bold.ttf` | 曲师、达成率、谱师、BPM 等 |

- 资源目录：`src/intro/assets/fonts/`（仅此 2 个 ttf）。
- 配置来源：[`maimai_banner.json`](../../../src/intro/templates/maimai_banner.json) 的 `fonts` 块（约 62 行）。

---

## 4. 关键约束：难度卡被封面和片头共用

`MaimaiBannerCard` 同时被：

- **封面**：`CoverComposer.qml`；
- **片头**：[`IntroOverlay.qml`](../../../src/intro/qml/IntroOverlay.qml)（约 254 行）。

而 banner 模板（含 `fonts`）从**同一个 JSON** `:/intro/templates/maimai_banner.json` 被
**三条独立路径**各自加载一份：

| # | 渲染路径 | 加载位置 | 注入点 |
| --- | --- | --- | --- |
| 1 | 封面预览 + 封面导出 | `CoverStudioPanel::loadBannerTemplate()` → `cachedTemplate_` | 封面字体注入到这里 |
| 2 | 片头**预览**（导出对话框内） | `IntroPreviewWidget::loadBannerTemplateMap()` → `bannerTemplateData` | 片头字体注入到这里 |
| 3 | 片头**导出**渲染 | `VideoExportQuickRenderBackend`（约 307 行）→ `setIntroBannerData(...)` | 片头字体注入到这里 |

> **含义**：难度卡自定义字体必须**按渲染路径分别注入模板副本**，不能改公共 JSON，
> 否则封面的字体选择会污染片头（反之亦然）。片头预览（#2）和片头导出（#3）必须注入**同一份**
> 用户选择，否则"所见非所得"。

---

## 5. ⭐ 现成参考（可直接复用）

App 内**已有一套完整的"自定义字体"子系统**：HUD 字体设置
[`HudFontSettings.h`](../../../src/tools/video_export/HudFontSettings.h) /
[`HudFontSettings.cpp`](../../../src/tools/video_export/HudFontSettings.cpp)。已实现：

- 便携字体库目录：`<preferences>/fonts`；
- `QFileDialog` 导入 `.ttf/.otf` → 拷入字体库 → `QFontDatabase::addApplicationFont` →
  `applicationFontFamilies` 解析字族（含去重）；
- **可嵌入 widget** `createHudFontSettingsWidget(parent, onFontChanged, refreshOut)`：
  当前字体读出 + 字体库下拉 + 实时样例 + 导入/重置；
- 通过 setter 持久化。

→ **难度卡字体选择器和"自定义文字图层"的字体选择都直接复用这套**，不必从零做。
HUD 字体已挂在导出对话框的**共享「皮肤」tab**（`buildSkinSettings`）里，片头侧字体 UI
可放在同一处或片头 tab（`VideoExportDialog.IntroControls.cpp`）。

---

## 6. 方案与难度

评级：★ = 半天内，★★ = 1 天上下，★★★ = 2 天上下（含联调，不含验收往返）。

### 6.1 自定义图片图层（`kind="image"`）— ★★☆☆☆（低中，~1~1.5 天）

- **模型**：`CoverLayer` 加 `imagePath`（绝对路径）+ `fillMode`；补 `toJson/fromJson`。
- **渲染**：delegate 加一支 `kind==="image"` 的 `Image{}`；`layerContentW/H` 需按图片**真实宽高比**——
  用 `QImageReader` 在 C++ 读 intrinsic size 存进 layer（推荐，导出更稳），或 QML `Image.onStatus` 回填。
- **UI**：图层列表加"添加图片"按钮；inspector 加文件选择（复用 `backgroundPathEdit_` 的
  "浏览 + 绝对路径 + 跨机缺失回退"套路）。
- **参考**：`chartFrame` 的静态图分支（`image://coverchart` provider）、背景自定义图路径逻辑。

### 6.2 自定义文字图层（`kind="text"`）— ★★★☆☆（中，~1.5~2.5 天）

- **模型**：`CoverLayer` 加 `text / fontPath / color / bold / align`（可选 `outline`）；补序列化。
- **渲染**：delegate 加 `Text{}`（描边可用 `Text` + layer effect）；文字"大小"用 `sizeFraction`
  （高度占比）映射到 `font.pixelSize`。
- **UI**：inspector 加多行文本框 + 颜色选择 + 字体下拉（复用 §5 字体库 widget）。
- **参考**：§5 字体库；卡面里 `Text{ font.family: displayFont.name }` 的写法。

### 6.3 难度卡自定义字体（封面 + 片头）— ★★★☆☆（中，~2~3 天，因为要接三条渲染路径）

- **QML**：让 `FontLoader.source` 支持**绝对路径**——当 `fonts.display/body` 是绝对路径时不再拼
  `fontsRoot`（否则维持 qrc 行为，向后兼容）。
- **模板注入（三处，见 §4）**：
  1. 封面：`CoverStudioPanel` 的 `cachedTemplate_["fonts"]` 注入封面选择；
  2. 片头预览：`IntroPreviewWidget` 的 `bannerTemplateData.fonts` 注入片头选择；
  3. 片头导出：`VideoExportQuickRenderBackend` 的 `templateMap["fonts"]` 注入**同一份**片头选择。
- **UI（两处）**：
  - 封面：难度卡选项组加 display/body 字体选择器；持久化到封面偏好（`CoverCompositionState`）。
  - 片头：导出对话框「皮肤」/「片头」tab 加同样的选择器；持久化到视频导出偏好
    （`video_export::*DialogPreferences`）。
- **参考**：§5 全套；`template.fonts` 已是数据驱动，接口天然就绪。

---

## 7. 建议的共享基建（先做一次，三项都受益）

1. **抽出通用字体选择器**：把 `HudFontSettings` 的"字体库 + 导入 + 字族解析 + 可嵌入 widget"
   下沉成与 HUD 解耦的通用组件，供①文字图层、②封面卡字体、③片头卡字体三处复用。
2. **统一 banner 模板加载 + 字体覆盖**：目前同一 JSON 在三处各自 `QFile` 读取（§4）。建议提供一个
   `loadBannerTemplate(fontOverride)` 帮手，三条路径共用，避免"片头预览改了、片头导出没改"这类漏改。
3. **图层 JSON 扩展**：`toJson/fromJson` 补齐新 kind 字段（`imagePath / text / fontPath / color …`），
   做好旧 `.miacover` / 旧偏好的兼容默认值。

---

## 8. ⚠ 风险与注意

- **inspector 生命周期陷阱**（已踩过坑）：新加的封面选项组会被 `CoverStudioWindow` reparent 到
  检视栏——正是 2026-07-21 修复的关闭闪退根因。新组必须遵守"关闭时（`closeEvent`）保存、析构不读
  被 reparent 的控件"的规则，别在析构里读它们（裸指针置空判断拦不住野指针）。
- **跨机可移植性**：图片路径、字体路径都存**绝对路径**，跨机缺失要走和背景自定义图一致的回退
  （找不到就忽略 / 占位 / 回退默认字体），不要让缺失文件导致导出失败或空白。
- **字族去重**：`addApplicationFont` 注册的 family 需去重（`HudFontSettings` 已处理，复用即可）。
- **封面 vs 片头隔离**：难度卡字体必须按路径注入模板副本，严禁改公共 JSON（§4）。
- **命中测试**：`hitKeyAt` 基于图层几何，天然支持任意 kind，无需额外适配。

---

## 9. 建议排期顺序

1. 共享字体选择器（§7.1）+ **难度卡自定义字体**（§6.3，封面 + 片头）——投入产出比最高，且验证了三条模板路径。
2. 自定义图片图层（§6.1）。
3. 自定义文字图层（§6.2，复用①的字体选择器）。
