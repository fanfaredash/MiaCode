# Cover Studio 封面工作台 · UI 重设计规范

Date: 2026-06-25
Status: design spec（推翻 `COVER_EXPORT_UI_RESEARCH.md` 的 UI 部分，渲染/导出链路保留）
Supersedes: `docs/specs/cover_export/COVER_EXPORT_UI_RESEARCH.md`（仅 UI；数据模型/导出流程仍以该文为准）

---

## 0. 背景：为什么推翻

旧版 Cover Studio 把"能跑通"放在第一位，UI 是把旧 dialog 控件硬塞进固定三栏的产物，因此粗糙：

- **画布与面板几乎不通信。** QML 里点击/拖拽图层只改 `selectedIndex`（QML 局部状态）并把几何直接写回 `CoverLayer`，**从不通知 C++**；而 `CoverInspectorPanel` / `CoverLayerListModel` 只监听 `activeLayerChanged` / `compositionChanged` 与少量属性，**没有监听 `nx/ny/sizeFraction`**。结果：选择和几何实际是单向的（面板→画布），画布→面板全断。这是诉求 1 的根因。
- **多谱面帧只显示一个。** 非激活的谱面帧图层回退到 `image://coverchart/<key>` 缓存 still，但 still 只在"勾选启用当前帧 / 导出 / 导入"时渲染。用 `+` 加出来的第二个帧从未被 grab → `imageRevision < 0` → 空白。这是诉求 7 的根因。
- **勾选框在暗色下几乎不可见。** Cover Studio 用 `exportDialogStyleSheet()`，其中只有 `QCheckBox { color … }`，**没有 `::indicator` 规则**，于是退回平台默认勾选框，在暗背景上近乎隐形。仓库其实早有解法 `darkAwareCheckBoxStyleSheet()`（显式描边 + 勾 SVG + accent 填充），只是没被用上。诉求 5。
- **播放条是裸 `QSlider` + 裸 `QPushButton`。** 而仓库已有一条打磨好的样例 `QuickShellPreviewTransport.qml`（圆角轨道 + accent 进度 + 圆形手柄 + 画出来的播放/暂停图标 + 精确时间气泡）。诉求 4。
- **左下角 7 个生僻字形按钮**（`+ ⧉ - ^ v ⇈ ⇊`）糊在一起。诉求 8。
- **缺快捷键、缺 tooltip、删除后跳回 card 无法连删。** 诉求 3、6。

**结论**：保留渲染核心（`CoverComposer.qml` / `CoverComposerView` / `SceneFrameRenderer` / `CoverLayoutModel` 的归一化几何 + 导出流程），**重写交互层**——画布双向同步、三层检查器、上下文播放条、暗色勾选框、精简图层操作、完整快捷键。

### 诉求映射总表

| # | 诉求 | 根因（代码） | 方案 | 阶段 |
|---|------|------|------|------|
| 1 | 点击/拖拽/缩放图层 → 右侧菜单及时更新 | QML 选择/几何不回传 C++；面板不听几何信号 | `selectedKey` 双向契约 + 检查器直连激活图层的 `nx/ny/sizeFraction/opacity` 信号 | P2 |
| 2 | 右侧菜单分三层：通用 / 图层通用 / 图层专属 | 旧 inspector 混排，画板设置在隐藏面板 | 检查器重构为「画板 / 图层通用 / 图层专属(多态)」三段栈 | P1 |
| 3 | 所有按钮有 tooltip | toolbar/部分控件无 tooltip | 全控件补 tooltip（含快捷键提示）+ accessibleName | P0 |
| 4 | 播放条参考已有样例 | 裸 QSlider/QPushButton | 新 `CoverFrameTransport` 复刻 `QuickShellPreviewTransport` 视觉 | P4 |
| 5 | 暗色勾选框配色 | `exportDialogStyleSheet` 无 `::indicator` 规则 | 套用 `darkAwareCheckBoxStyleSheet` 指示器规则 | P0 |
| 6 | 详尽快捷键 + 人性化（删后选下一个连删） | `removeActiveLayer` 固定跳回 card；无 keymap | 删后选相邻；完整 keymap | P0/P4 |
| 7 | 多谱面帧均可见 | 非激活帧 still 从未 grab | 失活即 grab still + 新增后即 grab | P3 |
| 8 | 左下角按钮简化 | 7 个生僻字形按钮 | 收敛为 2 个（加帧/删除）+ 右键菜单 + 拖拽重排 | P0 |
| 9 | …（照顾方方面面） | 粗糙 | 数值可直输、空状态、上下文菜单、布局菜单收纳、状态提示 | 贯穿 |

### 本次细化追加（2026-06-25）

在上述九条之外，追加三项细化：

- **R1 「布局」菜单进一步细化**为 重置 / 预设 / 文件 / 最近 的结构化菜单 → §2.1。
- **R2 难度卡也有专属选项**，**复用谱面帧第三段的同一槽位、内容不同**（多态切换）→ §3.3。
- **R3 左下角去掉复制**，只留 增 / 删 两键 → §7.2。

> 实现层面的逐条契约澄清（选择同步、信号生命周期、快捷键焦点分发、堆叠次序、预设 schema、列表 z 反转、退化态等）集中在 §12「实现澄清」——拿到文档先扫一遍那节。

---

## 1. 设计语境与原则

- **目标平台**：Windows 11 桌面，鼠标 + 键盘；约 60cm 视距。
- **设计系统**：复用 MiaCode `UiTheme`（`colors().accent/textPrimary/border/cardBg/...`），暗色优先、同时支持亮色（所有色取令牌，不写死十六进制；现有 `#14141C` 预览底色等保留）。
- **本地化**：中文优先（`UiText::isChineseUi()`），英文回退；新文案短直。
- **技术栈不变**：Qt Widgets 三栏 + 单个内嵌 QML 预览（`QQuickWindow` + `createWindowContainer`）+ 底部 Widgets 播放条。不引入新依赖、不引入 docking。
- **法则应用**：就近原则（同段控件归一组框）、识别优于回忆（图标 + 文案 + tooltip 三保险）、Hick（左下角 7→2）、Doherty（拖拽 < 100ms 反馈，导出 grab 显示忙）、错误预防（删除 card 直接忽略而非报错；导出前校验 still）。

---

## 2. 新窗口结构

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ 标题栏：导出封面 / Cover Studio                                       — □ ✕     │
├──────────────────────────────────────────────────────────────────────────────┤
│ 顶部工具条:  [布局 ▾]                                       [取消]  [ 导出封面 ] │  ← 文件类操作收进「布局▾」菜单；导出为主按钮
├───────────────┬──────────────────────────────────────┬─────────────────────────┤
│  图层          │                                      │  检查器（三段栈）          │
│ ┌───────────┐ │                                      │ ╔══ 画板（通用）═════════╗ │  ← §3.1 始终可见
│ │👁 🔒 难度卡 │ │          实时所见即所得预览           │ ║ 尺寸/比例 [1080×1080▾] ║ │
│ │👁    谱面帧2│◄┼─选中──►   (按输出比例 letterbox)     │ ║ 背景源   [曲绘 ▾]      ║ │
│ │👁    谱面帧 │ │      难度卡 + 多个谱面帧 still        │ ║ ☑ 背景虚化            ║ │
│ └───────────┘ │       选中层显示蓝框 + 缩放手柄         │ ╚═══════════════════════╝ │
│               │       拖拽/缩放/吸附中心·边缘           │ ╔══ 图层：谱面帧2 ═══════╗ │  ← §3.2 选中层通用
│ [＋帧]  [🗑]  │                                      │ ║ ☑ 显示    ☐ 锁定       ║ │
│               │                                      │ ║ 不透明度 ▭▭▭▭▭ [100%] ║ │
│  （右键=菜单） │                                      │ ║ 大小     ▭▭▭▭  [82%]  ║ │
│               │                                      │ ║ 位置 X[50] Y[50]      ║ │
│               │                                      │ ╚═══════════════════════╝ │
│               │                                      │ ╔══ 谱面帧（专属）═══════╗ │  ← §3.3 仅 chartFrame
│               │                                      │ ║ ☑ 内圈背景 亮度▭▭[80%]║ │  ← 选难度卡时此段换成卡片选项
│               │                                      │ ╚═══════════════════════╝ │
├───────────────┴──────────────────────────────────────┴─────────────────────────┤
│ 播放条:  [▶]  ●━━━━━━━━○──────────────  01:23.45 / 03:00.00      谱面帧2 时间 │  ← §5；上下文绑定到选中的谱面帧层
└──────────────────────────────────────────────────────────────────────────────┘
```

职责（沿用现有类，调整内部）：

- `CoverStudioWindow`：固定三栏 + 顶部工具条 + 底部播放条；屏幕适配（保留 `fitToScreen`）；**新增**：集中快捷键分发（§8、§12.4）、`布局▾` 结构化菜单（§2.1）。
- `CoverStudioPanel`：唯一状态协调器（保留）。**新增** `selectedKey` 双向同步入口（§4）、失活即 grab still（§7）、删后选相邻（§6）。
- `CoverComposerView` + `CoverComposer.qml`：渲染核心不动；**新增** QML→C++ 选择回传 + C++→QML 选择下推（§4）。
- `CoverInspectorPanel`：**重构为三段栈**（§3），并接管原 `takeSettingsPanel` 承载的画板设置。`takeSettingsPanel()` 间接层删除。
- `CoverLayerListPanel`：左下角按钮 7→2 + 右键菜单 + 行内 眼睛/锁 控件（§7 列表打磨）。
- `CoverFramePickerPanel` → 重写为 `CoverFrameTransport`（§5）。

### 2.1 顶部「布局」菜单（详细，诉求 R1）

`布局 ▾` 是一个 `QPushButton` + `QMenu`（`UiTheme::styleRoundedMenu`）。它管理"整张封面组合"——尺寸 / 背景 / 卡片 / 帧 / 几何这一整套（即 `.miacover` 与 preferences 里持久化的那份）。按三组分隔：

**① 当前布局**
- `重置为默认布局`（Ctrl+R）——**不可撤销**，弹二次确认「重置将丢弃当前所有图层与位置，继续？」。

**② 预设**（快速套用一整套组合）
- `应用预设 ▸`：
  - `卡片居中（默认）`
  - `卡片 + 谱面帧`
  - `双谱面帧拼贴`
  - `纯谱面帧（无卡片）`
  - ──（分隔）
  - 〔用户预设，动态列出〕
- `保存当前为预设…`——输入名称，存入 `app.cover_export.presets[]`（preferences）。
- `管理预设…`——小对话框，重命名 / 删除用户预设（可作本组的 stretch）。
- 套用 = 走 `applyCompositionJson` 同一路径；**需要谱面帧的预设在 `chartFrameAvailable_==false` 时置灰并 tooltip 说明**。内置预设是 size-agnostic 的常量 JSON；用户预设是保存时的组合快照。

**③ 文件**
- `保存布局到文件…`（Ctrl+S）——`.miacover`。
- `导入布局文件…`（Ctrl+O）。
- `打开最近 ▸`——最近 8 个 `.miacover` 路径（存 preferences），末尾 `清除最近`；文件缺失则置灰提示。

文案保留既有可见词（保存布局 / 导入布局 / 重置）；新增词短直（预设 / 应用预设 / 保存当前为预设 / 管理预设 / 打开最近）。所有项有中文 tooltip + 快捷键提示。

> 动机：旧版只有"重置 / 保存 / 导入"三颗散按钮；收进一个结构化菜单后既清爽（与诉求 8 同类化），又顺势补上"预设 / 最近文件"两个高频缺口。

---

## 3. 右侧检查器：三段栈（诉求 2）

检查器是一个 `QScrollArea` 内的纵向栈，三段都用 `QGroupBox`（标题即分组），按"通用→图层通用→图层专属"自上而下排（倒金字塔：越通用越靠上）。

### 3.1 画板（通用，始终可见）
绑定整张封面，不随选中层变化。把现在藏在 `hiddenControlsHost_` 的画板设置搬上来：

| 控件 | 绑定 | 备注 |
|---|---|---|
| 尺寸/比例 `QComboBox` | `currentSize()` | 预设沿用 `kCoverResolutionPresets`；改尺寸即重排预览 letterbox |
| 背景源 `QComboBox` | 曲绘 / 自定义 / 透明（/ 纯色，可选） | 自定义时展开 路径 + 浏览；完整规格见 §3.4 |
| 背景虚化 `QCheckBox` | `blurBackground` | 透明背景时禁用；强度固定（可选滑块见 §3.4.5） |
| **背景亮度** 滑块 + 数值（新增） | `coverBgBrightness`（见 §3.4.4） | 全幅底图暗化，默认 45%；透明背景时禁用 |

> 背景图片的完整规格（来源 / 裁剪 / 亮度 / 虚化 / 与内圈共享 / 格式 / 回退 / 导出耦合）见 **§3.4**——这块旧文档没讲清，本次补全。

> 难度卡的"类型 DX/SD、阴影、等级文本、超长文字"等卡片专属项**不在画板段、也不在 §3.2 通用段**，而是落在 §3.3「图层专属」多态槽位的难度卡分支（A 组），与谱面帧专属项复用同一位置。

### 3.2 图层（选中层通用）
标题随选中层变化：`图层 · 难度卡` / `图层 · 谱面帧2`。所有图层共有：

| 控件 | 绑定 `CoverLayer` | 说明 |
|---|---|---|
| ☑ 显示 | `visible` | = 列表里的眼睛，双向 |
| ☐ 锁定 | `locked` | 锁定后画布不可拖；微移快捷键也忽略 |
| 不透明度 滑块 + 数值 | `opacity` 0–100% | 数值用 `EditableValueLabel` 可点击直输 |
| 大小 滑块 + 数值 | `sizeFraction` 5–200% | |
| 位置 X/Y 数值 | `nx`/`ny` 0–100 | 紧凑双输入；拖拽时实时回填（§4） |

（难度卡的专属项**不在本段**，移到 §3.3 的「图层专属」多态槽位——见下。）

### 3.3 图层专属（复用同一槽位，内容随选中层 `kind` 切换，诉求 R2）

检查器第三段是一个**多态槽位**：位置固定在「图层（通用）」段下方，标题与子控件随选中层切换；无选中层时整段隐藏（省空间）。即"复用谱面帧的位置，内容不同"。

**A. 选中难度卡 → 标题「难度卡选项」**

| 控件 | 绑定 | 说明 |
|---|---|---|
| 类型 `QComboBox` | `cardModeCombo_` DX/SD | SD 显示 スタンダード 并镜像肩标 |
| ☑ 阴影 | `cardShadowCheck_` | 各背景模式均生效（含透明） |
| ☑ 等级文本渲染 | `levelTextRenderCheck_` | 等级以文本而非图集渲染 |
| 文字超长 `QComboBox` | `textOverflowCombo_` 缩小/省略 | 标题等过长时的策略 |

**B. 选中谱面帧 → 标题「谱面帧选项」**

| 控件 | 绑定 `CoverLayer` | 说明 |
|---|---|---|
| ☑ 内圈背景 | `frameBgEnabled` | 透明背景时禁用（无图可填） |
| 亮度 滑块 + 数值 | `frameBgBrightness` | 依赖内圈背景开启 |
| 帧时间（只读读数，点击直输） | `frameSeconds` | 播放/拖动在底部播放条（§5） |

实现：`CoverInspectorPanel::refresh()` 按 `layer->kind()` 切换该段子控件组（用 `QStackedWidget` 或显隐两组 `QGroupBox`）。原先散落在 `hiddenControlsHost_` 的卡片设置（`cardModeCombo_`/`cardShadowCheck_`/`levelTextRenderCheck_`/`textOverflowCombo_`）全部迁入此槽位的 A 组。未来若加更多卡片专属项（如标题/曲师文本覆盖）也落在 A 组，不撑宽通用段。

### 3.4 背景图片规格（调研 + 设计）

背景是**画布级全幅底图**（不是 `CoverLayer`，无独立 z/opacity/拖拽），属于 §3.1 画板段。它同时是谱面帧"内圈背景"的**同一张图源**。本节把旧文档没讲清的部分一次说全，并标出**当前实现**与**本次新设计**。

#### 3.4.1 三种来源（模式）

`CoverBackgroundMode`（`CoverComposerView.h`）：

| 模式 | 底图 | 不透明？ | 导出格式 | 谱面帧内圈背景 |
|---|---|---|---|---|
| `Jacket`(0) 曲绘 | 谱面 曲绘（`jacketPath`） | 是（黑底+图） | JPG（q95） | 可用 |
| `Custom`(1) 自定义 | 用户图片（`backgroundPath`） | 是 | JPG | 可用 |
| `Transparent`(2) 透明 | 无底图，卡片落在 alpha 上 | 否 | PNG | **禁用**（无图可填） |

QML 自下而上的图层栈（`CoverComposer.qml`）：黑色 `Rectangle`（非透明模式可见）→ 裸图 `bgSrc`（`PreserveAspectCrop`，仅 非透明+不虚化+已加载）→ `MultiEffect` 虚化（仅 非透明+虚化）→ **暗化 `Rectangle`**（`dimColor` × `dimOpacity`）。

#### 3.4.2 来源解析与回退

- **曲绘** 来自 `task.intro.jacketPath`（即 `banner_.jacketPath`）。**曲绘为空** → 卡片 jacket 槽显示 logo，底图无源 → 落到"黑底 + 暗化"（不是报错）。
- **自定义** 来自文件选择；格式 `*.png *.jpg *.jpeg *.bmp *.webp`。
- **缺失回退**：自定义路径为**绝对路径**存入组合 JSON（`background.customPath`）；重开/换机找不到该文件 → **回退曲绘**并提示（现有 `applyCompositionJson` 的 fellBack 逻辑，保留）。

#### 3.4.3 填充与裁剪

- 当前唯一行为：`PreserveAspectCrop`——按画布**等比覆盖、居中裁切**。画布本身已按输出比例 letterbox，所以底图最终铺满输出。
- **已知限制（v1 不做）**：无背景**平移 / 缩放 / 定位**——若自定义图的主体不在中心，只能靠裁切碰运气。也**不提供 contain**（会给底图留黑边，封面观感差）。是否加 pan/zoom 见 §3.4.9。

#### 3.4.4 亮度 / 暗化（**本次新设计**——旧版缺失的核心）

- **现状**：暗化是**固定**的——`dimColor #0A0414` × `dimOpacity 0.55`（来自模板 `background`），用户**无法调**。但谱面帧**内圈**背景却有 `frameBgBrightness` 可调（0..1）。**这处不对称正是"背景规格没讲清"的根**：全幅底图压根没有亮度控件。
- **设计**：新增画布级 `coverBgBrightness ∈ [0,1]` + §3.1「背景亮度」滑块（0..100%，可点击直输）。暗化 `Rectangle` 的不透明度改为 `clamp(1 − coverBgBrightness)`，着色仍用模板 `dimColor`（保留那层冷色调）。
  - **默认 0.45**（→ 不透明度 0.55，**与当前观感逐像素一致**，不破坏老封面）。
  - 语义与内圈一致（都是"亮度越低、黑/冷色覆盖越重"），用户心智统一。
  - 可选：默认值改为从 `task.backgroundBrightnessOuter` 取，与实时预览的"外圈亮度"对齐（待定，缺省仍 0.45）。
- 持久化：组合 JSON `background.brightness`（缺省 0.45）。透明模式无底图 → 控件禁用。

#### 3.4.5 虚化

- 当前：**仅 on/off**；强度固定 `blurAmount 0.9`（模板）；`blurMax = round(96 × 画布高 / 1080)` 随分辨率缩放，**保证 preview==export 的相对虚化一致**（别动这个缩放）。
- 内圈背景**不受虚化影响**——内圈恒清晰（见 §3.4.6）。
- 可选增强（stretch）：暴露虚化强度滑块（0..1 → `blurAmount`）。默认仍 on + 0.9。

#### 3.4.6 与谱面帧内圈背景的关系（一张图、两个消费者）

- 内圈背景启用时，谱面帧圆盘显示的是**同一张 `backdropSourceUrl`**（曲绘/自定义），但：**圆盘恒清晰**（不吃全幅的虚化）、按**逐帧** `frameBgBrightness` 暗化（黑覆盖 `1 − brightness`，与实时预览内圈模型一致）。
- 故"全幅底图"与"内圈圆盘"是**同源、独立调**：全幅 = 虚化开关 + 全局 `coverBgBrightness`；内圈 = 恒清晰 + 逐帧 `frameBgBrightness`。
- **透明模式**：无 `backdropSourceUrl` → 内圈背景控件禁用（§3.4.1 表）。

#### 3.4.7 透明模式细节

无底图；卡片**阴影落在 alpha 上**（`MultiEffect` 阴影对 PNG 透明区投影）；内圈背景禁用；导出 **PNG（ARGB32）**。非透明模式导出 **JPG（RGB32，q95）**。该"模式↔导出格式"耦合是既有约定，保留。

#### 3.4.8 格式 / 大图 / 持久化

- 格式：`png/jpg/jpeg/bmp/webp`。
- **大图（新建议）**：当前 `Image{ asynchronous:false; mipmap:true }` 会把整张原图传成纹理，超大自定义图（如 6000×6000）浪费显存、拖慢首帧。建议给底图 `Image` 设 `sourceSize` 上限到输出分辨率量级（或加载时 `QImageReader::setScaledSize` 降采样），封面用不到原始像素。
- 持久化：`background.{mode,customPath,blur,brightness}` 进组合 JSON（与预设 §12.10 一致；预设剔除尺寸但保留背景）。

#### 3.4.9 可选增强（待产品决定，非 v1 必做）

- **纯色背景模式**：在 曲绘/自定义/透明 之外加"纯色"（一个取色器），用于极简封面。低成本、独立。
- **背景平移 / 缩放**：解决 §3.4.3 的中心裁切局限；需要一组背景 pan/zoom 控件 + 持久化，成本中等。
- **虚化强度滑块**：§3.4.5。

#### 3.4.10 控件归属与契约影响

- 新控件「背景亮度」落在 §3.1 画板段；新增 `CoverComposerInputs.coverBgBrightness` + QML root 属性 `coverBgBrightness`，暗化 `Rectangle.opacity` 改读它（替换写死的 `dimOpacity()`，`dimColor()` 保留）。
- 持久 schema 加 `background.brightness`；`CoverCompositionState` 读写 + 迁移（旧文件无该键 → 默认 0.45）。
- 不改：模式枚举、导出格式耦合、`PreserveAspectCrop`、blurMax 缩放、内圈共享同源逻辑。

---

## 4. 画布 ↔ 面板 实时双向同步（诉求 1，核心）

这是重设计的脊梁。引入单一选择真相 + 几何实时回填。

### 4.1 `selectedKey` 双向契约
- **QML 暴露** `property string selectedKey`（取代纯局部 `selectedIndex`；`selectedIndex` 退化为由 `selectedKey` 派生的只读）。
- **C++→QML**：`CoverComposerView::setSelectedKey(key)` 写 root 属性；`CoverStudioPanel::setActiveLayerKey()` 末尾调用它。于是在列表/检查器里选层 → 画布蓝框立刻跟随（**当前完全缺失**）。
- **QML→C++**：`TapHandler.onTapped` 与 `DragHandler.onActiveChanged(active)` 里 `chartSceneBinder.selectLayerKey(layerItem.ld.key)`（`CoverComposerView` 暴露 `Q_INVOKABLE selectLayerKey`，转发 `CoverStudioPanel::setActiveLayerKey`）。于是在画布点/拖任意层 → 列表高亮 + 检查器 + 播放条全部切到该层。
- 选择是 key 而非 index：z 重排、增删都不会错位。

### 4.2 几何实时回填
- `CoverInspectorPanel` 在 `refresh()` 时，除了读值，还把 X/Y/大小/不透明度滑块连接到**当前激活 `CoverLayer` 的** `nxChanged/nyChanged/sizeFractionChanged/opacityChanged`（切换激活层时断开旧层、连接新层）。QML 拖拽写 `ld.nx=…` 触发 `nxChanged` → 检查器滑块即时更新（带 `QSignalBlocker` 防回环）。
- `CoverLayerListModel::connectLayerSignals` 已连 `frameSecondsChanged`（保留）；若要行内副标题随拖动实时刷新，再加连几何信号。
- 净效果：**拖动/缩放/吸附时，右侧数值与列表实时联动**；反向改数值时画布即时动（现已可用，保留）。

### 4.3 验收锚点
拖动谱面帧 → 检查器 X/Y 跟着跳；画布点难度卡 → 列表高亮跳到难度卡且检查器换标题；列表选谱面帧2 → 画布蓝框移到谱面帧2。三向一致。

---

## 5. 播放条重设计：`CoverFrameTransport`（诉求 4）

照搬 `QuickShellPreviewTransport.qml` 的视觉语言，落成一个可复用、自绘的 `QWidget`（不再裸 `QSlider`）。它**上下文绑定到当前选中的谱面帧层**；选中非谱面帧层时整条禁用并提示"选择谱面帧图层以编辑帧时间"。

视觉规格（与样例对齐，取 `UiTheme` 令牌）：

- **轨道**：高 6px，圆角 3px，底色 `inputDisabledBg`；**已播放进度**用 `accent` 填充，圆角。
- **手柄**：直径 18px 圆，填充近白、`borderSoft` 1px 描边；位置由 `displayedProgress` 唯一推导（手柄与落点不会打架）。
- **播放/暂停按钮**：30×28，自绘三角/双竖条（沿用 `makeTransportIcon` 的绘制，但配色走令牌）；hover/pressed 背景态。
- **时间读数**：`MM:SS.cs`（沿用 `formatFrameTime`），右侧 `01:23.45 / 03:00.00`。
- **精确时间气泡**：拖动时手柄上方浮出 `CC10151C` 半透明气泡显示精确时间（样例同款），停拖 600ms 淡出。
- **命中带**：交互高度 < 行高，点轨道线附近才 seek（样例 `scrubInteractiveHeight` 思路），避免误触。
- **键盘**：聚焦播放条时 ←/→ = 逐帧 + 长按加速（**保留**现有 `preview_interaction` held-seek）；Home/End = 跳 0/末尾（新增）；Space = 播放/暂停。

行为不变量：拖动/点击轨道/键盘任何 seek 都先暂停 → 移共享播放头 → 重绘 live scene（零 readback）；timer 的 `setValue` 仍 `QSignalBlocker` 包裹防自暂停（保留现有逻辑）。

> 实现取舍：底部条用自绘 `QWidget`（而非再塞一个 QML 窗口）——单窗口里再嵌 QML 太重，且与三栏 Widgets 不协调。视觉规格逐项对齐样例即可达到"一样好看"。

---

## 6. 勾选框与暗色配色（诉求 5）

- 给 Cover Studio 的样式表追加 `darkAwareCheckBoxStyleSheet()` 的 `::indicator` 规则（或直接对各 `QCheckBox` setStyleSheet）：
  - `::indicator` 16×16，`borderStrong` 1px 描边，`windowAltBg`(暗)/白(亮) 填充，3px 圆角；
  - `:hover` 描边转 `accent`；
  - `:checked` 填 `accent` + `image: :/icons/checkmark.svg`（白勾）；`:checked:hover` 转 `accentHover`。
- 单选/复选都不靠颜色单独表意——勾本身是形状线索（符合无障碍）。
- 一处加规则，画板/图层/谱面帧三段及列表内复选全部受益。

---

## 7. 多谱面帧可见 + 图层列表与左下角简化（诉求 7、8）

### 7.1 多帧可见（诉求 7）
规则：**任何可见谱面帧层，要么是激活层显示 live scene，要么显示一张已渲染的 still**。补两处 grab：

1. `CoverStudioPanel::setActiveLayerKey()` 切换前：若**旧激活层**是谱面帧且可见，调用 `renderChartFrameLayerNow(oldLayer)` 把它定格成 still（捕获其当时帧时间/尺寸），再切走。于是失去 live 身份的帧立刻有图。
2. `addChartFrameLayer()` 之后对新层 grab 一次（R3 已去掉复制，故只剩"新增"这一处需要补 grab）。

辅助：still 的渲染像素 `chartFrameRenderPx` 随层尺寸；帧时间改变后该层若非激活，下次失活/导出会重 grab，保证清晰度。导出路径（遍历 `visibleChartFrameLayers` 重 grab）保留。grab 的 warm 路径 / 重入 / 脏判定见 §12.8。

> 备选（更省）：仅在"即将切走"时 grab，不在新增时 grab——但新增后若用户立刻去拖第二个，第一个会短暂空白，故选"新增也 grab"。

### 7.2 列表打磨 + 左下角 7→2（诉求 8）
- **行内控件**（自定义 delegate 或行 widget）：`👁 显示` 切换 + `🔒 锁定` 字形 + 名称 + 谱面帧副标题（帧时间 `mm:ss.cs`）。点行=选中（双向同步 §4），点眼睛=切显隐（不改选中）。眼睛/锁取代裸复选框，顺带绕开复选框可见性问题。
- **底部按钮 7→2**（诉求 R3）：`＋ 添加谱面帧`、`🗑 删除`。两者都有清晰 tooltip（含快捷键）。**取消复制**——用户反馈复制非必要；新建帧用 `＋` 即可，模型 `duplicateLayer` 仅留作测试/未来，**无 UI 入口、无 Ctrl+D、右键菜单也不含复制**。
- **z 序与其余操作**移交：① 列表内**拖拽重排**（✅ 已实现：`QListView` InternalMove → `CoverLayerListModel::dropMimeData` → `CoverLayoutModel::moveByViewRows`，内部做 view↔z 反转 §12.12，drop 返回 false 抑制视图自带的 move/remove）；② **右键上下文菜单**（删除/上移/下移/置顶/置底/显隐/锁定）；③ 快捷键（§8 `[ ] / Ctrl+[ ]`）。生僻字形 `⧉ ^ v ⇈ ⇊` 退场。

---

## 8. 快捷键与人性化（诉求 6）

集中在 `CoverStudioWindow` 装一个 key 事件过滤器（同时装到内嵌 QML 窗口，复用现有 Esc 过滤器模式），分发到 `CoverStudioPanel`。←/→ 按**焦点上下文**分流：焦点在画布/列表 = 微移；焦点在播放条 = seek。

| 键 | 作用 | 人性化/备注 |
|---|---|---|
| `Delete` / `Backspace` | 删除当前图层 | **删后自动选相邻**（优先下一个，没有则上一个；难度卡不可删→忽略）→ 可连按连删 |
| `A` | 添加谱面帧 | = ＋ 按钮；无可渲染谱面时禁用 |
| `↑↓←→`（画布/列表焦点）| 微移 1px 等效 | 锁定层忽略；到边界夹紧（沿用 `clampCentre`） |
| `Shift+↑↓←→` | 粗移 10px | |
| `+` / `=` , `-` | 放大 / 缩小 2% | 等比，约束 5–200% |
| `[` / `]`（或 `PageDown/Up`）| 下移 / 上移一层 | z 序 |
| `Ctrl+[` / `Ctrl+]` | 置底 / 置顶 | |
| `V` | 切换显示/隐藏 | |
| `L` | 切换锁定 | |
| `Space` | 谱面帧 播放/暂停 | 仅谱面帧层；非谱面帧层时无操作 |
| `←` / `→`（播放条焦点）| 逐帧后退/前进 + 长按加速 | **保留**现有 held-seek |
| `Home` / `End`（播放条）| 跳 0 / 末尾 | 新增 |
| `Ctrl+S` / `Ctrl+O` | 保存 / 导入布局 | |
| `Ctrl+R` | 重置布局 | 需二次确认（不可撤销） |
| `Esc` | 关闭窗口 | 保留 |
| 双击数值标签 / `Enter` | 数值直输并提交 | `EditableValueLabel` |

所有快捷键在 tooltip 里以 `（快捷键 X）` 形式提示（诉求 3 协同）。

---

## 9. Tooltip / 无障碍 / 文案（诉求 3、9）

- **每个**按钮/滑块/下拉都有中文 tooltip + `accessibleName`；图标按钮 tooltip 末尾附快捷键。
- 工具条 `布局▾`、`导出封面`、`取消` 补 tooltip（当前完全没有）。
- Tab 顺序遵循视觉阅读序：工具条 → 图层列表 → 检查器三段 → 播放条。每个可交互元素键盘可达、有焦点框。
- 文案保留既有可见词（导出封面/取消/保存布局/导入布局/重置），新增词短直（显示/锁定/不透明度/大小/位置/帧背景/亮度/添加谱面帧/删除）。

---

## 10. 其它打磨（诉求 9 汇总）

- **数值可直输**：检查器不透明度/大小/位置/亮度、播放条时间，均用 `EditableValueLabel`（仓库已有，`src/app/ui/`）点击直输，四舍五入到整数/两位。
- **空状态/禁用提示**：无可渲染谱面 → 谱面帧相关控件禁用并 tooltip 说明；仅卡片可导出时检查器只显画板 + 难度卡段。
- **上下文菜单**：画布右键选中层、列表右键，均给"删除/置顶/置底/上移/下移/显隐/锁定"（无复制，R3）。
- **工具条收纳**：文件/预设/重置 收进 `布局▾`（详见 §2.1），工具条只剩 `布局▾` + 右侧 `取消`/`导出封面`，更清爽（von Restorff：导出为唯一主色按钮）。
- **导出忙反馈**：grab still 时 `WaitCursor` + 状态栏提示（保留并补可见进度文案）。
- **吸附线/缩放手柄**：保留现有中心·边缘吸附与右下缩放手柄（视觉良好，仅确保跟随 `selectedKey`）。

---

## 11. 数据模型 / 契约影响

**不动**（关键，别破坏）：
- `CoverLayer` 归一化几何（`nx/ny/sizeFraction/z`）、`CoverLayoutModel` v2 JSON、迁移规则、`coverchart` image provider、`SceneFrameRenderer` 离屏 grab、`renderCoverComposite` 导出、预览==导出 约束。

**新增/改**：
- QML root：`selectedKey`（双向）取代纯局部 `selectedIndex`；tap/drag 回调调 `chartSceneBinder.selectLayerKey`。
- `CoverComposerView`：`Q_INVOKABLE selectLayerKey(key)` 转发；`setSelectedKey(key)` 下推。
- `CoverStudioPanel`：`setActiveLayerKey` 末尾下推选择 + 切换前 grab 旧帧 still；`removeActiveLayer` 改为选相邻；集中快捷键处理。
- `CoverInspectorPanel`：三段栈（画板 / 图层通用 / **图层专属多态槽位**：难度卡选项 ⇄ 谱面帧选项）+ 直连激活层几何信号；接管画板设置（删 `takeSettingsPanel` 间接层）。
- `CoverLayerListPanel`：**2 按钮（增/删）** + 右键菜单（无复制）+ 拖拽重排 + 行内眼睛/锁。
- `CoverFramePickerPanel` → `CoverFrameTransport`（自绘）。
- `CoverStudioWindow`：`布局 ▾` 结构化菜单（重置确认 / 预设 / 文件 / 最近，§2.1）。
- `CoverCompositionState`：新增 `presets[]` 与 `recentFiles[]` 持久化；`duplicateLayer` 模型方法保留但无 UI 入口。
- 背景（§3.4）：新增 `CoverComposerInputs.coverBgBrightness` + QML root `coverBgBrightness`，暗化 `Rectangle.opacity` 改读它（替换写死的 `dimOpacity()`，`dimColor()` 保留）；组合 JSON 加 `background.brightness`（缺省 0.45，旧文件迁移取默认）；大图按 `sourceSize` 降采样。可选：纯色模式 / 背景平移缩放（§3.4.9，非 v1）。
- 样式：Cover Studio 样式表并入 `darkAwareCheckBoxStyleSheet` 指示器规则。

---

## 12. 实现澄清（拿到文档常见疑点 · 逐条契约）

前面是"做什么"；这一节是"怎么做才不踩坑"。每条都对应一个别人接手时容易卡住或做反的地方。

### 12.1 两个选择属性别混：`selectedKey` vs `activeChartFrameKey`
- `selectedKey`（新）=**哪个层显示选中蓝框**，可为任意 kind（含难度卡）；`activeChartFrameKey`（已有）=**哪个谱面帧托管 live scene**，仅 chartFrame，选中难度卡时为空。两者独立。
- QML 里：`selectionBorder` / `scaleHandle` 绑 `selectedKey`；live `Loader.active` 绑 `activeChartFrameKey`。`selectedIndex` 退化为**派生只读**（`indexOf(layers, key===selectedKey)`），不再被直接赋值。
- **防回环接线（关键）**：用户手势（`TapHandler.onTapped` / `DragHandler.onActiveChanged(active==true)`）**直接**调 `chartSceneBinder.selectLayerKey(key)` → C++ `CoverStudioPanel::setActiveLayerKey`（唯一真相，键不变即早退）→ C++ 回写 `setSelectedKey` **只更新蓝框、不再 echo 回手势路径**。属性写入对"未变化"早退，故无抖动、无死循环。

### 12.2 检查器几何信号的生命周期与防写回环
- `activeLayerChanged` 时：先 `disconnect` 旧层、再 `connect` 新层的 `nxChanged/nyChanged/sizeFractionChanged/opacityChanged`。inspector 持 `QPointer<CoverLayer>` —— 当前层被删除会 `deleteLater`，必须先断连再让其析构，否则悬空。
- 信号 → 控件 的回填一律 `QSignalBlocker` 包裹（slider 与 `EditableValueLabel` 同理），否则 `valueChanged → setActiveLayerCenter → nxChanged → setValue → valueChanged …` 死循环。

### 12.3 微移 / 缩放步长的归一化定义
- `nx/ny/sizeFraction` 是 0..1 归一化，所以"1px"必须换算：**细移 = 1 / 输出维度**（X 用 `currentSize().width()`，Y 用 `height()`）；**粗移（Shift）= 10×**；缩放 `+/-` = **±0.02 绝对**（加减，非乘除），夹 `0.05..2.0`。到边界用 `clampCentre`。锁定层忽略移/缩（见 §12.6）。
- 位置数值显示 `0..100`（= `nx×100` 四舍五入）；**底层 `nx` 保留小数精度**直到被直输覆盖（拖拽产生的 50.3 不会因显示成 50 而丢精度）。`clamp01ish` 允许 overscan（-0.5..1.5）：数值框按"夹显示到 0..100、但不阻止画布拖到画外"处理。

### 12.4 快捷键的焦点分发（原生子窗口陷阱）
- 内嵌 QML 预览是**原生子 `QQuickWindow`**；它持焦时普通 `Qt::WindowShortcut` / `QShortcut` **不触发**（这正是现有 Esc 要靠 eventFilter 绕过的原因）。方案：在 `CoverStudioWindow` 与该 quick window 上**各装一个 keyPress eventFilter**，转发到同一个 `handleShortcut(QKeyEvent*)`。
- `←/→` 上下文判定：`frameTransport_->hasFocus()`（或其内部 slider 持焦）→ **seek**；否则→ **微移当前层**。无明确焦点时默认微移。
- **文本输入优先**：当 `focusWidget()` 是 `EditableValueLabel` 的编辑态 / `QLineEdit` 时，所有单键快捷键（V/L/Delete/方向/+-/Space）让位给文本编辑——过滤器先判焦点控件类型再决定是否拦截。

### 12.5 "删后选相邻"的确切次序
- 次序基于**图层列表的可见行序**（z 降序，行 0 = 最前）。删除行 `i` 后：新选 = 原**行 i+1**（下一项）；若 `i` 是末行则选**行 i-1**；若删后只剩难度卡则选难度卡。
- 难度卡自身按 Delete = **忽略**（不报错、不改选、不弹框）。删除按钮在选中难度卡时**置灰** + tooltip 说明。该规则保证"连按 Delete 连续删谱面帧"。

### 12.6 锁定（locked）到底冻结什么
- **冻结**：位置（`nx/ny`）+ 大小（`sizeFraction`）的一切来源——画布拖拽（已有 `!locked` 门）、检查器 X/Y/大小 控件、方向/缩放快捷键。检查器在 locked 时**禁用**这些控件并 tooltip 说明。
- **不冻结**：显示（`visible`）、不透明度（`opacity`）、谱面帧专属（帧时间 / 内圈背景 / 亮度）。

### 12.7 新谱面帧的默认落位（堆叠次序由用户自由调整）
- **产品决策（已定）**：难度卡与谱面帧的 z 次序**完全由用户自由调整**——拖拽重排 / `[ ]` / 置顶置底 / 右键菜单都能改，**任何一方都不被钉死在最前/最后**。`addChartFrameLayer` 维持 `z = maxZ()+1`（新帧在最前且即被选中，符合"我刚加的就在手边"）。**不要加 `bringToFront(card)` 之类的强制钉前逻辑。**
- **只修一个开箱即坏的点**：新帧默认居中 `0.5/0.5` 且 `sizeFraction 0.82` 会与居中卡片 `0.85` 几乎完全重叠 → 新帧把卡片盖死、看不出加了东西。解法是**落位错开**而非锁 z：新帧按"已有帧数"做级联偏移（每多一帧 `nx/ny` 各 `+0.04`，到边界回绕），让"可见性不依赖 z"——无论谁在上，两者都各露一部分，用户再自行调 z 与位置。
- 即：**z 自由、落位错开**。

### 12.8 失活即 grab 的细节（性能 / 重入 / 脏判定）
- 在 `setActiveLayerKey` 切走旧帧**前** grab：走 **warm 路径**（`settleEvents(cold=false)`，一两帧即可），不是 cold 的 ~0.3s；受 `rendering_` 重入门保护，在飞则跳过（still 稍旧，下次 idle / 导出再刷）。
- **脏判定**：仅当该帧自上次 still 后 `frameSeconds` 或 `sizeFraction` 变过才 grab，避免来回点选反复 grab。
- 导出仍按 §7.1 对全部 `visibleChartFrameLayers` 以**输出分辨率**重 grab（清晰度），与编辑期的 warm 小图无关。

### 12.9 `CoverFrameTransport` 的 C++ 接口（纯视图，面板驱动）
- 播放时钟 / 播放头仍在 `CoverStudioPanel`（`playClock_` 等）——transport 是**哑视图**，不持有播放逻辑。
- **信号**：`seekRequested(double seconds)`、`playToggleRequested()`、`scrubBegan()` / `scrubEnded()`。
- **槽**：`setEnabled(bool)`、`setRangeSeconds(double max)`、`setPositionSeconds(double)`、`setPlaying(bool)`、`setContextHint(QString)`。
- 面板在 `activeLayerChanged` 与每个 play tick 推 position/range/enabled；选中谱面帧 → 该帧成 live、transport 跟随其 `frameSeconds`；**选中难度卡 → `activeChartFrameKey` 置空、所有帧回落 still（无 live）、transport `setEnabled(false)` + 提示"选择谱面帧图层以编辑帧时间"**。即"编辑卡片时无活动帧"，符合预期。

### 12.10 预设 / 最近文件的持久化 schema
- 与现有"单组合恢复"（`app.cover_export` 存最后一次组合）**分开存**：
  - `app.cover_export.presets` = `[{ "name": str, "version": 1, "composition": <CoverCompositionState JSON，剔除 size> }]`（用户预设）。
  - `app.cover_export.recentFiles` = 路径字符串数组（最多 8，去重，最近在前）。
- 预设 **size-agnostic**：保存时剔除 `size`；套用走 `applyCompositionJson` 但**保留当前画板尺寸**。
- 内置预设是常量（不入 preferences）。套用含帧的预设要**真正创建帧层并 grab**（复用 §7.1）；`chartFrameAvailable_==false` 时该预设置灰。

### 12.11 内置预设的几何（给实现一个确定起点）
归一化初值（可微调，但别留空让人猜）：
- `卡片居中(默认)`：card 显示 `0.5/0.5` size `.85`；无帧。
- `卡片 + 谱面帧`：card `0.64/0.5` size `.78`（右）；frame#1 `0.32/0.5` size `.82`（左）；card 在上。
- `双谱面帧拼贴`：card 隐藏；frame#1 `0.30/0.40` size `.56`；frame#2 `0.66/0.60` size `.56`。
- `纯谱面帧(无卡片)`：card 隐藏；frame#1 `0.5/0.5` size `.92`。

### 12.12 列表拖拽重排的 z 反转陷阱
- 列表视图按 z **降序**（`layerAt` 排 `a->z() > b->z()`，行 0 = 最前 = 最高 z）；而 `moveLayerBefore/After` 作用于 `layers_`（`normalizeZOrder` 后 z **升序**，index 0 = 最后）。**两者方向相反**。
- 直接把"视图行 i 拖到行 j"喂给 `moveLayerBefore/After` 会**上下颠倒**。封装 `moveByViewRows(from, to)`：先把视图行换算成模型 index（`N-1-row`）再调模型，或在拖放层统一反转 + `assignZOrderFromList`。

### 12.14 live 场景绑定必须与顺序无关（多帧不可见的根因）
- **现象**：加第 2/3 个谱面帧后只见最初那个——加帧触发 `layersChanged` → Repeater **整体重建所有 delegate**，旧帧 loader 卸载时 `unbindLiveChartScene()` 会把新激活帧**刚建立的绑定清掉**（QML binding 求值顺序不保证），新帧从未 `update()` → 渲染空白，只剩最初帧的 still。
- **修复**：QML `onItemChanged` **只在 item 非空时 bind、绝不在 item==null 时 unbind**（`liveChartScene_` 是 `QPointer`，loader 真正卸载时自动置空）；`bindLiveChartScene` 末尾加 `root->update()` 强制首帧立即绘制。teardown 仍由析构里的 `detachLiveChartScene` 负责。
- 深层：`layers` 是 `QList<QObject*>`（每次返回新列表）→ Repeater 每次全量重建，是上面脆弱性的来源；要根治需换 `QAbstractItemModel`（暂不做）。

### 12.13 退化 / 空态（集中处理，别散落）
- `chartFrameAvailable_==false` 时统一：`＋帧` 禁用、`A` 快捷键禁用、含帧预设置灰、transport 隐藏/禁用并提示、§3.3-B（谱面帧选项）永不出现、导出走 card-only。难度卡照常可编辑。
- **点击画布空白**（任意层之外）：**取消选择**（见 F2，推翻原"保持选中"决定）——画布无蓝框、检查器只剩画板段、转场条禁用。

---

## 13. 验收结论与待补功能（2026-06-25）

本轮 Cover Studio UI 重设计已验收完毕，已有内容基本符合需求。P0/P1/P2/P3/P4 的主路径和当前文档约定的工作台结构、检查器分段、多谱面帧显示、播放条、快捷键与图层基础操作可以作为当前实现基线。

补充实现状态：

1. ✅ 图层列表行拖拽超过列表顶部时，判定为拖拽到第一行位置。
2. ✅ 点击画布空白区域会取消图层选择；画布无蓝框，图层检查器隐藏，播放条禁用。
3. ✅ 添加谱面帧会继承上一次选中的谱面帧配置（位置除外）；若上次选中的帧不存在，则回退到列表中的第一个谱面帧配置。
4. ✅ 画布交叠区域点击/拖拽只命中一个图层。优先级为：正在选中的图层 > 图层顶层 > 次顶层 ... > 图层底层。
5. ✅ 画布视觉缩放、还原已实现；该功能仅影响 Cover Studio 编辑视图，不写入 `.miacover`，也不改变最终导出尺寸。顶栏提供 `− / 100% / +` 控制，快捷键为 `Ctrl++` / `Ctrl+-` / `Ctrl+0`。
6. ✅ P5 图层列表 delegate 已实现，待人工 GUI 验收：行内显示缩略图、眼睛、锁、名称与副标题；点击眼睛/锁不改变当前选中层。谱面帧缩略图优先使用 cached still，尚未 grab 时使用占位图。

---

## 14. 分阶段实现 + 验收

每阶段独立可交付、可 GUI 验收；Release 构建 + 既有 `cover_layout_model_spec` 绿，模型行为变更处补断言。本轮验收已完成，以下分阶段状态保留用于追溯实现范围；仍需补充项以 §13 为准。

### P0 · 快速整顿（低风险，先见效）　✅ 已实现并验收
做：暗色勾选框（§6）、全控件 tooltip + 快捷键提示（§9）、**左下 2 键（增/删）+ 右键菜单（§7.2）**、删后选相邻（§6 核心）、**`布局▾` 菜单基础：重置(二次确认)/保存/导入/打开最近（§2.1 ①③）**。
验收：暗色下勾选框清晰可辨；每个按钮悬停有中文 tooltip；连按 Delete 能连续删谱面帧（难度卡保留、被跳过）；**左下仅 增/删 两键、无复制**，右键能完成上移/下移/置顶/置底；`布局▾` 展开为结构化菜单，重置有确认，最近文件可用。
测试：`cover_layout_model_spec` 加"删除后下一个 key"用例。

### P1 · 检查器三段栈 + 多态专属段（§3，诉求 2 / R2）　✅ 已实现并验收
做：检查器重构为 **画板 / 图层通用 / 图层专属(多态槽位)** 三段；画板设置上移、删 `takeSettingsPanel`；**难度卡选项与谱面帧选项复用第三段同一槽位、按 `kind` 切换**；数值直输（§10）；**新增「背景亮度」控件 + 底图大图降采样（§3.4.4、§3.4.8）**。
验收：选不同层 → 第三段标题/内容正确切换——**选难度卡显 类型/阴影/等级文本/文字超长，选谱面帧显 内圈背景/亮度/帧时间，二者占同一位置**；画板段始终在顶；**背景亮度滑块改变底图明暗，默认 45% 与旧版逐像素一致**。
测试：手动 GUI；`background.brightness` round-trip + 旧文件无该键取默认 0.45 的迁移断言。

### P1.5 · 布局预设与最近文件（§2.1 ②，诉求 R1）　✅ 已实现
做：内置预设（套用走 `applyCompositionJson`）+ 保存用户预设 + 管理预设；`CoverCompositionState` 加 `presets[]` / `recentFiles[]` 持久化。
验收：套用「卡片 + 谱面帧」立即重排；无谱面时帧类预设置灰；保存的用户预设重开后仍在；最近文件能打开、缺失置灰。
测试：`cover_layout_model_spec` 覆盖用户预设保存、重命名、删除。

### P2 · 画布↔面板双向同步（§4，诉求 1）　✅ 已实现并验收
做：`selectedKey` 双向 + 检查器直连激活层几何信号。
验收（§4.3 三向一致）：画布拖动 → 检查器 X/Y/大小实时跳；画布点层 → 列表高亮 + 检查器换层 + 播放条切层；列表/检查器选层 → 画布蓝框跟随。无回环抖动。
测试：手动 GUI（同步是 UI 行为）。

### P3 · 多谱面帧可见（§7.1，诉求 7）　✅ 已实现并验收
做：失活即 grab still + 新增后 grab（warm 路径/脏判定/堆叠见 §12.7、§12.8）。
验收：加 3 个不同时间/位置/大小的谱面帧，全部同时可见（激活层 live，其余 still）；切换激活层不致某层变空白；导出含全部帧。
测试：`cover_layout_model_spec` 验"新增帧后 `imageRevision>=0`"（若 grab 可在无 GUI 环境跳过则保留导出路径断言）。

### P4 · 播放条重写 + 完整快捷键（§5、§8，诉求 4、6）　✅ 已实现并验收
做：`CoverFrameTransport` 自绘复刻样例；落地完整 keymap（焦点上下文分流 ←/→）。
> 实现说明：沿用 `CoverFramePickerPanel` 类名（不改 CMake）但重写为带 **播放/暂停按钮** 的转场条（自绘图标 + 圆角 accent 轨道 + 18px 圆手柄 + `mm:ss.cs / 总时长` 读数）；panel 出 `playheadChanged`/`playbackStateChanged` 信号驱动；keymap 经 panel `handleShortcutKey`（画布原生窗口聚焦时生效）：Space 播放、A 加帧、Delete 删、V/L 显隐锁、`[ ]`/`Ctrl+[ ]` z 序、`+/-` 缩放、方向键微移（Shift 粗移）；转场条聚焦时 ←/→ 逐帧、Home/End 跳 0/末。**精确时间气泡 + 非画布焦点的 keymap 暂缓**（widget 聚焦时 Delete 仍由列表处理）。
验收：播放条外观与 `QuickShellPreviewTransport` 一致（轨道/进度/手柄/图标/气泡/时间）；非谱面帧层选中时禁用并提示；keymap 全表逐项可用；微移/缩放/z 序/显隐/锁定/播放快捷键就位且锁定层忽略微移。
测试：手动 GUI。

### P5 · 列表 delegate 打磨（§7.2 行内，stretch） ✅ 已实现，待人工 GUI 验收
做：自定义 delegate（缩略图 + 眼睛 + 锁 + 名称 + 帧时间副标题）。
验收：行内眼睛切显隐不改选中；缩略图反映该层近似外观；拖拽重排即时反映 z 序。
测试：手动 GUI。

P5 人工验收清单：

1. 图层列表每行显示缩略图、眼睛、锁、名称和副标题；谱面帧副标题显示帧时间。
2. 点击眼睛只切换该层显示/隐藏，不改变当前选中层和画布蓝框。
3. 点击锁只切换该层锁定状态，不改变当前选中层。
4. 谱面帧已有 still 时缩略图使用 still；没有 still 时显示占位图，不出现空白破布局。
5. 把非激活谱面帧从隐藏切回显示时，画布能显示 cached still。
6. 拖拽重排后列表顺序、画布 z 序和右键菜单操作保持一致。

---

## 15. 不做 / 边界

- 不引入通用图片编辑器、不引入 QtPropertyBrowser、不引入 docking、不引入新外部依赖（沿用旧研究结论）。
- 不改归一化几何模型、不改 v2 JSON/迁移、不改导出渲染管线。
- 不做每字母动画等与本功能无关的花哨；动画仅用于状态反馈（≤200ms，仅 transform/opacity）。
- 缩略图（P5）若拖慢交互则降级为占位。

---

## 16. 维护同步点

改 Cover Studio 时同步检查：
- `CoverStudioWindow.*`（布局/适配/快捷键分发）、`CoverStudioPanel.*`（协调 API/选择/grab）、`CoverInspectorPanel.*`（三段栈/几何信号）、`CoverLayerListPanel.*`（按钮/菜单/delegate）、`CoverFrameTransport.*`（新）。
- `CoverComposer.qml` ↔ `CoverComposerView.*` 的 `selectedKey` / live-frame / still 约定。
- `CoverLayoutModel.*` / `CoverCompositionState.*` 持久 schema 不破坏。
- 同步更新 `.claude/skills/miacode-dev-guide/references/feature-index.md` 的 Cover Studio 记录与本 spec。
