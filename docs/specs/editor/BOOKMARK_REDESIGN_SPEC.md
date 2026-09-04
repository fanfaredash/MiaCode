# 书签侧边栏与 simai 内嵌存储重设计方案

状态：2026-07-06 的 simai 内嵌存储版已实施；2026-07-07 决定进入「注释绑定版」重设计，以下追加方案优先于文末旧实施版。旧实施版保留为历史和迁移参考。

调研日期：2026-07-06。

## 追加方案：注释绑定版（2026-07-07）

新的产品约束：

> 非控制类 `|| 注释` 就是书签；没有注释就没有书签。

书签不再是独立数据对象，而是谱面文本中普通注释的侧边栏索引。侧边栏只负责展示、跳转和编辑注释，不再维护一份隐藏书签数据库。

本节覆盖下文旧方案中关于 `&miacode_bookmarks=`、JSON 导入导出、手动书签对象和书签重锚的设计。

### A1. 书签来源

- 每个难度的 chart 文本按行扫描。
- 行内第一个 `||` 后的非控制类注释生成一个书签。
- 空注释和控制类注释不生成书签；当前控制类注释至少包括 `|| 3/4` 这种纯拍号形式。
- 书签行号来自注释所在行，时间轴跳转秒数运行时由 `timelineSecondForCursor(line, col)` 或 timeline model 解析得到，不持久化。
- 书签列表在文本变更、难度切换、谱面整理、undo/redo 后重新扫描生成。

### A2. 侧边栏标签命名与存储

侧边栏标签名存储在注释正文里，使用可读的显式前缀：

```txt
|| [Intro] 开头铺垫
|| [Break 12] 这里注意节奏
|| 普通注释也会作为书签
```

解析规则：

- 若注释正文以 `[标签]` 开头，且 `标签` trim 后非空，则侧边栏标签名为该标签。
- 标签前缀之后的剩余文本仍是普通注释正文，tooltip 可显示完整注释或显示去掉标签前缀后的正文。
- 标签中不支持 `]`；如果用户需要右中括号，应使用全角字符或把它放在正文部分。
- 若没有 `[标签]` 前缀，侧边栏标签名按自动规则派生：先取注释首个短 token，失败时取注释前缀。
- `|| [标签]` 没有正文也合法，表示一个只有侧边栏名的书签注释。

编辑规则：

- 侧边栏重命名已有显式标签：只替换 `[标签]` 内部文本。
- 侧边栏重命名没有显式标签的注释：在注释正文前插入 `[新标签] `。
- 清空显式标签：删除 `[标签]` 前缀，回到自动派生命名。
- 重命名是普通文本编辑，必须进入编辑器 undo/redo、dirty、autosave 和“不保存则丢弃”的同一事务。

不再引入 `&miacode_bookmark_labels=`、`.miacode` label map 或其它隐藏标签表。否则会重新产生同步边界。

### A3. 侧边栏结构与折叠

- 不新增单独书签 tab。
- 不再使用独立 `bookmark_group` 可见行作为标题；难度行本身右侧只绘制书签折叠图案，例如 `▾` / `▸`，不显示书签计数。
- 点击难度行主体切换难度；点击右侧折叠图案只切换该难度书签展开状态。
- 展开后显示真正的二级书签项。
- 书签子行高度应低于难度行，标题字体比难度名小 `1-2pt`，行号 badge 更矮，让它读作难度的附属导航。
- 未显式操作过的难度默认「当前 active 难度展开，其它折叠」。
- 切换到 Metadata、Export、Toolbox、Timeline、Syntax、Muri 等其它标签页时，不强制折叠书签；只在首次默认态和 reveal 目标书签时自动调整展开状态。

### A4. 创建、重命名、删除、移动

- 创建书签 = 在目标行插入或编辑 `|| 注释`。
  - 目标行已有非控制类 `|| 注释`：只 reveal。
  - 目标行没有注释：追加 ` || [新书签]` 或 `|| [新书签]`（根据该行是否已有谱面内容决定），并进入重命名。
- 重命名书签 = 修改注释的 `[标签]` 前缀。
- 删除书签 = 删除对应 `|| 注释`。确认文案必须明确「会删除该行注释」。
- 移动书签 = 把对应 `|| 注释` 从源行移动到目标行。
  - 目标行已有非控制类注释时，默认取消并提示；后续可加「合并/替换」选择，但不作为第一阶段必需。
- 行号高亮、侧栏跳转、时间轴跳转保留，但它们导航的是注释行，不是持久书签对象。

### A5. 保存、放弃保存、autosave

- 书签和侧边栏标签都只存在于 chart 文本里。
- 保存文件时不写 `&miacode_bookmarks=`。
- 用户选择不保存文档时，未保存的书签创建、删除、移动、命名会和文本一起被丢弃。
- autosave/crash recovery 无需特殊混入书签状态，只需保存当前文本快照。
- 书签操作不得调用 `saveProjectRenderState()` 写项目 JSON。

### A6. JSON 与旧隐藏字段迁移

- 删除工具箱「书签」子菜单中的 JSON 导入/导出入口。
- 删除 `miacode_bookmarks.json` 作为用户可见交换格式。
- 旧 `.miacode/miacode_settings.json` 的 `editor_bookmarks` 只作为一次性迁移输入读取，不再写回。
- 旧 `&miacode_bookmarks=` 字段只作为一次性迁移输入读取，不再保存。
- 迁移策略：
  1. 打开文件时若发现旧书签数据，按 difficulty + line 找到目标行。
  2. 目标行已有非控制类注释且没有显式 `[标签]`：给该注释添加 `[旧书签名]` 前缀。
  3. 目标行无注释：向该行追加 `|| [旧书签名]`。
  4. 行号越界或目标行不适合追加时，在该难度末尾追加独立注释行 `|| [旧书签名]`。
  5. 迁移会修改 chart 文本，应标记文档 dirty，并在状态栏提示「旧书签已迁移为谱面注释，保存后生效」。
  6. 下次保存时移除 `&miacode_bookmarks=`，项目 JSON 中的旧 `editor_bookmarks` 也不再保留。

### A7. 谱面整理联动

- 谱面整理不再调用独立书签重锚。
- 整理器必须保留普通 `||` 注释及其 `[标签]` 前缀；整理后重新扫描注释生成书签。
- 现有「普通中途 `||` 注释拆到独立行」行为继续保留，这正是书签随文本移动的依据。
- 需要补充测试：
  - `|| [标签] 正文` 经过全文整理后标签和正文都保留。
  - `|| 普通注释` 经过整理后仍生成自动命名书签。
  - `|| 3/4` 这类控制注释不生成书签。
  - 选区整理只影响选区内注释；侧边栏在整理后按文本重新扫描。

### A8. 变为冗余或应删除的能力

- `SimaiBookmarkData` 主数据模型。
- `&miacode_bookmarks=` 的常规解析、序列化、隐藏字段过滤和 round-trip 测试。
- `.miacode/miacode_settings.json` 的 `editor_bookmarks` 常规写入。
- 工具箱 JSON 导入/导出。
- `EditorBookmark::title` / `nameLocked` 作为持久书签名。
- `source=manual` 与「无注释也可存在的手动书签」语义。
- `commentFingerprint` / `contextBefore` / `contextAfter` / `second` 的持久重锚辅助字段。
- `syncBookmarksIntoDocument()`。
- `markBookmarksMutatedByUser()` 作为独立书签 dirty 入口；书签操作应通过文本编辑自然置 dirty。
- `reanchorActiveBookmarksAfterChartTransform()`。
- 删除书签但保留注释的行为和文案。
- 侧边栏内联编辑直接提交到 bookmark 对象的路径；应改为编辑注释文本。

### A9. 第一阶段实施顺序

1. 新增注释解析器：返回 `line`、`col`、完整注释、可选显式标签、自动标签、是否控制注释。
2. 让侧边栏从注释解析结果构建书签，不再从 `state_.editorBookmarks_` 构建。
3. 将创建/重命名/删除/移动改为文本编辑操作，并接入 undo/redo、dirty、autosave。
4. 移除工具箱 JSON 导入/导出入口。
5. 改造难度行右侧的书签折叠图案交互，压低书签子行高度和字体。
6. 增加旧 `&miacode_bookmarks=` / 旧项目 JSON 到注释的迁移。
7. 删除或降级旧书签持久化代码和测试。
8. 补充谱面整理、保存/不保存、undo/redo、折叠状态的验证用例。

### A10. 侧边栏视觉规格（IDE 树形，2026-07-07 实施）

对首版实现的 UI 复审结论：三级仅有行高差、缩进感弱；右侧折叠药丸过于突出且位置反直觉；折叠钮与删除钮挤在行尾；书签独有的左竖线比一二级选中态更抢眼，且点击书签后难度行高亮丢失。重设计参照 VS Code / JetBrains 树形导航惯例，规则如下（实现全部在 `OutlineItemDelegate` + `rebuildFieldSidebar` + FrameBootstrap 点击接线，未迁移 `QTreeView`）：

统一选中语言（一套语言、两档强度；二轮复审 2026-07-07 收紧）：

- 「当前所在」（激活难度 / 谱面信息页 / 导出页）= 无描边填充 + 左缘 3px accent 圆角竖条。由 `kOutlineItemActiveRole` 驱动（难度行 = `activeDifficultyId_ && activeOutlineKey_=="chart"`；metadata 行含 latency 子页；export 行同理），**不跟随列表 selection**，因此选中书签后难度行高亮保留。悬停激活行时填充轻微提亮。
- 旧的选中态圆角描边框删除（描边是视觉笨重的主因）。
- **QListWidget selection 一律不绘制**（二轮复审：hover 与"按下"同色被判不合理，且书签驻留选中读作卡住的按压态）。悬停 = 弱一级纯填充（hoverFill），是唯一的瞬时反馈；「当前」只由 active 标记表达。
- 书签行无任何持久标记（跳转是一次性动作）：无左竖线、无选中变色徽章、无驻留填充；徽章恒为中性灰。书签行的 hover 填充左边界收进到缩进导线右侧（`kDifficultyFoldGlyphX`+5），不得压线。

树形结构：

- 折叠箭头移到难度行行首：1.6px 路径 chevron（右=收起 / 下=展开），textSecondary，无底色；行首 `kDifficultyFoldHitZone`(24px) 内点击切折叠，行体点击切难度；无书签的难度不画箭头但保留空列（`kDifficultyChevronColumn`=14，色点列对齐）；icon-only 窄栏（<120px）无箭头、整行切难度。激活难度**不加粗**（首版 DemiBold 被复审否决）。
- 书签行画 1px 缩进导线，x=`kDifficultyFoldGlyphX`(13) 对齐箭头列，每行上下各溢出 1px 以桥接 2px item spacing。
- 书签徽章左缩进 `kBookmarkRowIndent`=44，行高 22→24；徽章在同难度内定宽（按最大行号 `kOutlineItemMaxLineRole` 计），数字**居中**，名称列垂直对齐。
- 徽章默认中性灰（textSecondary @ alpha 40/34），不再整列 accent；名称默认 textSecondary，选中/激活时 textPrimary。
- 一级分区之间插入 4px 不可交互 `spacer` 项（kind="spacer"，`Qt::NoItemFlags`）：文档页 | 难度树 | 导出/工具箱。⚠ delegate `sizeHint` 必须给 spacer 早退——通用分支的 28px 最小行高会把 4px 间隙撑成整行空白（首版踩坑）。

行尾动作：

- 悬浮删除难度按钮（`deleteDifficultyButton_`）**整体移除**（2026-07-07 复审拍板）：删除难度只走右键菜单（无图标，中文「删除 %1」）；确认弹窗与状态栏文案已中文化。行尾不再有任何行内控件。

配套修复（二轮复审）：

- 导出 busy spinner：`positionOutlineExportBusySpinner()` 从 show 拆出，`rebuildFieldSidebar()` 末尾在 spinner 活跃时重新锚定——切到导出页时 `activeDifficultyId_→0` 会让原激活难度的自动展开书签组收起、Export 行上移，旧实现的圈圈会留在原位。
- 侧栏滚动条主题：`applyUiTheme()` 重刷 `outlineList_` 样式时必须同步重刷其 vertical scrollbar 的 `scrollBarStyleSheet()`（构造时捕获的是当时主题，暗色下否则残留浅色）。

## 背景

近期新增的书签功能已经具备基本的创建、跳转、行号高亮、拖拽换行和 JSON 导入导出能力，但当前交互仍偏“管理弹窗”模型，和 MiaCode 现有窄侧边栏、谱面编辑流不够贴合。

本方案整理当前实现位置、问题边界和推荐改造路径。目标是让书签成为侧边栏内的轻量导航信息，而不是独立详情对象。

## 当前实现地图

- 书签数据结构：`src/app/mainwindow/MainWindow.h` 的 `MainWindow::EditorBookmark`。
- 项目 JSON 读写：`src/app/mainwindow/sections/editor/MainWindow.EditorState.cpp` 的 `loadProjectRenderState()` / `saveProjectRenderState()`，当前写入 `.miacode/miacode_settings.json` 的 `editor_bookmarks`。
- 注释扫描与自动同步：`src/app/mainwindow/sections/editor/MainWindow.EditorDisplay.cpp` 的 `syncBookmarksFromEditorText()`。
- 创建、详情弹窗、管理弹窗：`src/app/mainwindow/sections/editor/MainWindow.EditorDisplay.cpp` 的 `showCreateBookmarkDialogForLine()`、`openBookmarkAtLine()`、`showBookmarkManager()`。
- 侧边栏重建：`src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp` 的 `rebuildFieldSidebar()`。
- 侧边栏点击、双击、右键：`src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp` 中 `outlineList_` 的 signal 连接。
- 行号区书签绘制和拖拽：`src/editor/PlainCodeEditor.Layout.cpp`、`src/editor/PlainCodeEditor.Bookmarks.cpp`、`src/editor/PlainCodeEditor.Internal.h`。
- 编辑器右键菜单：`src/editor/PlainCodeEditor.Input.cpp` 的 `PlainCodeEditor::contextMenuEvent()`。
- simai 文档字段读写：`src/core/chart/document/SimaiDocument.cpp` 的 `fromText()`、`parseUnmanagedFields()`、`toText()`。

注意：仓库技能指南中仍有旧路径 `src/simai/document/`，实际代码路径是 `src/core/chart/document/`。如果实施本方案，应同步更新对应 repo guide 引用。

## 设计目标

- 书签只保留用户可见的“名称”，不再暴露详情文本。
- 书签创建和重命名主要在侧边栏完成，避免弹窗打断编辑。
- 默认名称来自谱面注释，但只在首次创建时生成，之后不自动改名。
- 行高亮/行号右键提供书签操作入口。
- 书签持久化进入 simai 文件的一个受管隐藏 `&key=value` 字段，不再把主数据放在 `.miacode/miacode_settings.json`。
- 受管字段不出现在元数据页的“Other &xx Fields”里，避免用户误删或误改。

非目标：

- 不把书签变成跨谱面数据库。
- 不让第三方播放器解析或理解书签。
- 不在第一版里做复杂全文搜索或标签系统。

## 侧边栏交互方案

当前侧边栏是扁平 `QListWidget`，书签通过文本前缀空格伪装为二级项。推荐保留 `QListWidget`，但新增明确 item kind：

- `difficulty_chart`：难度一级项，沿用现有 badge 和选择样式。
- `bookmark_group`：难度下的书签分组项，显示展开/收起状态。
- `bookmark`：书签二级项。

推荐视觉层级：

- 难度项：保持当前行高、字体和 badge。
- 书签组项：小一号字体，左侧显示 `▾` / `▸`，文本为 `书签 3` 或 `Bookmarks 3`。
- 书签项：使用真正二级绘制，不再把缩进写进文本。建议显示短行号 badge 加名称，例如 `8  Intro`。
- 书签项名称右侧省略，完整名称和行号放 tooltip。
- 活动书签可使用轻量 accent 左边线或圆点，不应和一级难度选中态混淆。

展开/收起规则：

- 每个 difficultyId 维护独立折叠状态。
- 当前 active difficulty 默认展开。
- 用户点击 `bookmark_group` 切换展开/收起，不切换难度。
- 重建侧边栏时保留折叠状态、当前选中项和滚动位置。

滚动规则：

- `outlineList_` 显式使用 `Qt::ScrollBarAsNeeded`。
- 套用现有 `UiTheme::scrollBarStyleSheet()`。
- 书签跳转或创建后调用 `scrollToItem(..., QAbstractItemView::PositionAtCenter)`。
- 如果单个难度书签很多，不做分页按钮；窄侧栏里滚动比翻页更符合当前 Qt 列表行为。

## 书签名称与编辑方案

移除“书签详情弹窗”作为主路径。推荐行为：

- 单击书签：跳到对应行。
- 双击书签：进入侧边栏内联重命名。
- 右键书签：菜单提供 `重命名`、`删除`、`跳到时间轴位置`。
- 右键书签组或难度项：提供 `插入书签`，默认插入当前编辑器光标行。
- 工具箱里的 `创建书签` 和 `书签管理` 可以删除，或短期保留为兼容入口但不再作为推荐路径。

内联重命名建议：

- 使用 `QListWidget::editItem()` 或 `QStyledItemDelegate` 创建窄 `QLineEdit`。
- 编辑器宽度受侧边栏宽度约束，提交后仍使用 elide 显示。
- 空名称提交时回退到默认名，不保留空字符串。
- `Esc` 取消，`Enter` 提交，失焦提交。

右键菜单直接输入方案：

- 菜单第一项可以是一个 `QWidgetAction`，内嵌 `QLineEdit`，placeholder 为 `书签名称`。
- 用户输入后按 `Enter` 提交并关闭菜单。
- 该入口适合“右键相应位置，展开菜单直接输入”的需求，但实现复杂度高于 `editItem()`；可以作为第二阶段。

## 默认命名规则

默认名称只在首次创建时生成，之后不再被注释同步逻辑改写。

来源优先级：

1. 当前行的 `|| 注释`。
2. 同次扫描发现的注释候选。
3. 行号兜底，例如 `L8` 或中文 UI 下 `第 8 行`。

注释转名称规则：

- 先 trim，并把连续空白压缩成一个空格。
- 忽略控制类注释，例如 `|| 3/4`。
- 优先按空白、半角逗号、全角逗号、顿号、分号、冒号、斜杠等分隔符取第一个词。
- 如果第一个词过长，截到 12 到 16 个字符。
- 如果没有可用分词，则取前 20 个字符。
- 中英文都保留原文，不做翻译。

当前 `syncBookmarksFromEditorText()` 会在部分场景下根据新注释改写 `title`。实施时应改为：

- 自动创建时写入默认 `name`。
- 后续同步只更新 line、second、comment fingerprint、context 等定位辅助信息。
- 用户显式重命名后设置 `nameLocked` 或等价状态，任何自动逻辑都不得改名。

## 行高亮与右键菜单

当前行号区已支持：

- 双击已有书签行：激活书签。
- 双击非书签行：请求创建。
- 拖拽已有书签行：移动书签到目标行。
- 行号绘制：书签行和拖拽目标行有 accent 高亮。

推荐补充：

- 在 `PlainCodeEditor::contextMenuEvent()` 里根据右键坐标解析行号。
- 如果该行没有书签，菜单顶部加入 `插入书签`。
- 如果该行已有书签，菜单顶部加入 `重命名书签`、`删除书签`、`在侧边栏显示`。
- 行号区右键也发出同样的上下文信号，例如 `lineNumberBookmarkContextMenuRequested(line, globalPos)`。

为了避免 editor 层直接依赖 MainWindow，`PlainCodeEditor` 只发信号：

- `bookmarkCreateRequested(int line)`
- `bookmarkRenameRequested(int line)`
- `bookmarkDeleteRequested(int line)`
- `bookmarkContextMenuRequested(int line, QPoint globalPos)`

实际菜单动作仍由 `MainWindow::EditorSection` 或 `FrameSection` 承接。

## 数据模型方案

用户可见层只保留：

- `difficultyId`
- `line`
- `name`

内部仍建议保留最小定位辅助字段：

- `second`：用于双击书签时定位时间轴。
- `source`：`manual` / `comment`。
- `commentFingerprint`：用于注释移动后的重锚。
- `contextBefore` / `contextAfter`：用于必要时增强重锚。
- `nameLocked`：用户是否显式改名。

可以将 `EditorBookmark::text` 移除或保留为迁移兼容字段但不再展示。若保留，只作为旧 JSON 导入时的废弃字段读取，不写入新 simai 数据。

## simai 内嵌存储方案

使用单个受管字段：

```txt
&miacode_bookmarks={"schema":"miacode_bookmarks_v2","items":[{"d":5,"l":8,"n":"Intro","s":8.796,"src":"comment","fp":"9e6a1d..."}]}
```

字段选择：

- 使用 `miacode_bookmarks`，避免与普通用户字段或第三方 `&bookmark=` 约定冲突。
- 单行 compact JSON，禁止真实换行。
- `d` = difficultyId，`l` = line，`n` = name，`s` = second。
- `src`、`fp`、`cb`、`ca`、`locked` 作为可选定位辅助字段。
- JSON parse 失败不能阻塞谱面打开，只报告书签导入问题并忽略该字段。

`SimaiDocument` 改造建议：

- 增加 `QVector<SimaiBookmarkData> bookmarks` 或等价字段。
- `fromText()` 遇到 key `miacode_bookmarks` 时解析到 `bookmarks`，不放入 `extraFields`。
- `toText()` 在通用 extra fields 后、difficulty fields 前写出 `&miacode_bookmarks=`。
- `parseUnmanagedFields()` 把 `miacode_bookmarks` 视为 reserved key，确保元数据页“Other &xx Fields”不显示它。
- 如果 bookmarks 为空，不写出该字段。

编码兼容：

- 第一版直接 compact JSON，便于人工识别和迁移。
- 如果后续发现外部编辑器损坏 JSON 标点或非 ASCII，可升级为 `&miacode_bookmarks=miacode:v2:<base64url-json>`。

## 旧 JSON 迁移

读取优先级：

1. simai 文件里的 `&miacode_bookmarks=`。
2. 如果 simai 字段不存在，再读取旧 `.miacode/miacode_settings.json` 的 `editor_bookmarks`。

迁移策略：

- 旧 JSON 只作为回退来源，不再作为主存储。
- 从旧 JSON 迁移到内存后，状态栏提示“已从旧项目状态载入书签，将在下次保存时写入谱面文件”。
- 不建议立刻标记整份文档 dirty；用户下一次保存或修改书签时写入 simai。
- simai 写入成功后，可以从项目 JSON 删除 `editor_bookmarks`，但保留 `last_opened_difficulty` 等项目状态。
- 至少一个版本周期内保留旧 JSON 读取能力，避免用户升级后丢失书签。

工具菜单中的 JSON 导入/导出：

- 可保留为兼容工具。
- 导入 JSON 后写入内存 bookmarks，并标记文档 dirty，以便保存进入 simai。
- 导出 JSON 应导出当前内存 bookmarks，格式可仍为旧 `miacode_bookmarks_v1` 或新增 v2。

## 实施步骤

1. 在 `SimaiDocument` 增加隐藏书签字段解析、序列化和 `parseUnmanagedFields()` 过滤。
2. 增加 `simai_document_spec` 覆盖：
   - `&miacode_bookmarks=` 可 round-trip。
   - 字段不进入 `extraFields`。
   - 字段不出现在 `parseUnmanagedFields()` 结果。
   - 坏 JSON 不阻塞其它字段解析。
3. 将 `EditorBookmark` 用户可见面收敛为 `name`，保留必要定位辅助字段。
4. 将 `saveProjectRenderState()` 的书签主写入迁到 simai 文档保存路径；旧 JSON 只保留 fallback/migration。
5. 重做 `rebuildFieldSidebar()`，加入 `bookmark_group` 和二级书签 item。
6. 给侧边栏增加折叠状态、内联重命名、右键菜单和滚动位置保留。
7. 移除或降级 `showCreateBookmarkDialogForLine()`、`openBookmarkAtLine()`、`showBookmarkManager()` 的弹窗主流程。
8. 在 `PlainCodeEditor` 增加行右键书签信号，并由 MainWindow 承接菜单动作。
9. 调整默认命名逻辑，保证只在首次创建时生成名称。
10. 更新仓库指南中失效的 `SimaiDocument` 路径。

## 验证清单

自动测试：

- `simai_document_spec`
- `plain_code_editor_spec`

手动 UI 验证：

- 窄侧边栏下长书签名省略且 tooltip 正确。
- 一个难度下超过 20 个书签时滚动正常。
- 展开/收起状态在重建侧栏、切换难度后保持。
- 双击书签可内联重命名。
- 右键书签可重命名、删除、跳转。
- 右键代码行可插入/删除/重命名书签。
- 注释首次生成默认名后，后续修改注释不覆盖用户改名。
- 保存后 `maidata.txt` 出现单个 `&miacode_bookmarks=`。
- 元数据页“Other &xx Fields”不显示 `miacode_bookmarks`。
- 从旧 `.miacode/miacode_settings.json` 打开能迁移书签。
- 删除所有书签后保存，`&miacode_bookmarks=` 被移除。

## 风险与开放问题

- `QListWidget` 可以支撑第一版，但层级、折叠和内联编辑会让委托逻辑变重；如果后续继续扩展，应该考虑迁到 `QTreeView` + model。
- 书签随文本编辑的重锚仍依赖 line delta、comment fingerprint 和 timeline second，多次大规模重排后仍可能需要人工修正。
- 把书签写入 simai 会增加文件 diff；使用单行 compact JSON 可以降低视觉干扰，但会让 diff 中单项变化不够友好。
- 是否保留 JSON 导入/导出，需要根据用户迁移需求决定。主路径不应再依赖它。

---

## 详细设计（实施版，2026-07-06）

以下为实际落地的设计决策与代码位置。与上文建议不一致处均已标注理由。

### D1. 数据模型

核心层（`src/core/chart/document/SimaiDocument.{h,cpp}`）：

```cpp
struct SimaiBookmarkData {
    int difficultyId;            // "d"（1..7，非法项在解析时逐条丢弃）
    int line;                    // "l"（钳到 >= 1）
    QString name;                // "n" — 唯一用户可见文本
    double second;               // "s" — 时间轴锚点，< 0 = 未知（写出时省略）
    QString source;              // "src" — "manual" / "comment"（空省略）
    QString commentFingerprint;  // "fp"（空省略）
    QString contextBefore;       // "cb"（空省略）
    QString contextAfter;        // "ca"（空省略）
    bool nameLocked;             // "locked" — 用户显式改名（false 省略）
};
```

- `SimaiDocument` 增加公开成员 `QVector<SimaiBookmarkData> bookmarks` 与
  `bool bookmarksParseError`。
- `fromText()`：`miacode_bookmarks` key 直接路由进 `bookmarks`，**永不进入
  `extraFields`**；JSON 解析失败 → 忽略字段并置 `bookmarksParseError`（谱面加载
  绝不因书签元数据受阻）；重复出现该字段时第一条可解析者生效；裸
  `&miacode_bookmarks=`（空值）视为"无书签"而非错误。
- `toText()`：在通用 extra fields 之后、难度三元组之前写出**单行 compact JSON**
  （`QJsonDocument::Compact`，天然无换行；JSON 转义保证引号/非 ASCII 安全）；
  空列表不写字段（删光书签 = 该行从文件消失）。写出前按 (d, l, n) 排序保证
  diff 稳定。
- `parseUnmanagedFields()` 的 `isReservedMetadataKey` 增加 `miacode_bookmarks`，
  元数据页「Other &xx Fields」编辑器彻底看不到该字段（也因此无法误改）。
- 未做 base64url 编码升级（保留为后备方案）。

应用层（`MainWindow::EditorBookmark`，`src/app/mainwindow/MainWindow.h`）：

- 保留原成员名 `title`（即 spec 中的"name"，避免全库改名噪音），新增
  `bool nameLocked`。
- `text`（旧"详情文本"）降级为**仅兼容旧 JSON 导入**的字段：不再展示、不写入
  simai payload、不写入新导出。`commentText` 保留为重锚辅助（内部）。

### D2. 存储与迁移

采纳顺序上的关键事实：打开流程是 `setCurrentFilePath`（触发
`loadProjectRenderState`）**先于** `loadDocument`（写入 `state_.document_`），而
新建文件流程两者顺序相反。因此：

- `EditorSection::loadProjectRenderState()` 只把旧 JSON 的 `editor_bookmarks`
  读进**暂存区** `state_.legacyJsonEditorBookmarks_`，不再直接写
  `editorBookmarks_`。
- `DocumentSection::loadDocument()` 在赋值 `state_.document_` 后立即调用
  `EditorSection::adoptBookmarksForLoadedDocument()`（必须在
  `activateInitialField()` 之前，因为难度切换会触发注释同步）：
  1. simai `bookmarks` 非空 → 采纳之，`editorBookmarksInSimai_ = true`；
  2. 否则采纳暂存的旧 JSON 书签（非空时状态栏提示"已从旧项目状态载入书签，
     将在下次保存时写入谱面文件"，经 `QTimer::singleShot(0)` 延迟以免被
     "Opened: …" 覆盖），`editorBookmarksInSimai_ = false`；
  3. `bookmarksParseError` → 状态栏提示"谱面内嵌书签数据无法解析，已忽略"。
- `DocumentSection::saveToPath()`：序列化前调用
  `EditorSection::syncBookmarksIntoDocument(&document_)`；写盘成功后置
  `editorBookmarksInSimai_ = true` 并顺手调 `saveProjectRenderState()` 清理旧键。
- `saveProjectRenderState()`：仅当 `editorBookmarksInSimai_ == false` 时继续写
  `editor_bookmarks`（**迁移期 crash 保险**——首次保存前书签仍有旧 JSON 镜像；
  旧版本 MiaCode 也还能读）；为 true 后该键被省略 = 从项目 JSON 删除。
  `last_opened_difficulty` 一律保留。
- 未标记整份文档 dirty；改为**用户主动书签操作**（创建/重命名/删除/拖移/导入）
  调用 `markBookmarksMutatedByUser()` 置 `documentDirty_`，注释自动同步产生的
  变化不置 dirty（否则打开带注释的旧谱面即刻变脏）。
- autosave 快照未混入内存书签（避免书签自动同步造成快照签名抖动）；崩溃时
  未保存的书签编辑丢失，与文本编辑同级风险，可接受。

### D3. 默认命名

`MainWindow.EditorDisplay.cpp` 匿名命名空间：

- `defaultBookmarkNameFromComment()`：normalize（trim + 连续空白压一格）→
  控制类注释（`3/4` 型）返回空 → 按分隔符
  `[\s , ， 、 ; ； : ： / ／]+` 取第一个 token，超长截到 **16** 字符 →
  无可用 token 时取前 **20** 字符原文。中英文均保留原文。
- `fallbackBookmarkNameForLine()`：中文 UI `第 N 行`，英文 `LN`。
- `syncBookmarksFromEditorText()` 的"同行注释重绑"分支**只更新**
  line/second/fingerprint/context/commentText，不再改写 `title`/`text` ——
  默认名只在创建时生成一次，之后任何自动逻辑不得改名（比 nameLocked 更强；
  nameLocked 仅作为对未来自动命名逻辑的诚实标记，在用户显式重命名时置位并
  随 payload 持久化）。

### D4. 侧边栏

> **实现状态（2026-09-05）**：本节保留的是旧 QWidget 侧栏的设计基线；v2 当前侧栏由
> `src/app/qml_ui/sidebar/DifficultyList.qml` 与 `src/app/qml_ui/ViewState.qml` 持有，
> runtime 已删除 `rebuildFieldSidebar()`、书签组折叠旧状态和 `revealBookmarkInSidebar()`
> 等空 Widget 桥。以下 `QListWidget` / `QWidget` 细节不再是现行实现契约。

`QListWidget` 保留；角色常量提升到 `MainWindowShared.h`
（`kOutlineItemKindRole/DifficultyRole/LineRole/SecondRole/ExpandedRole/ActiveRole`）。

- item kinds：新增 `bookmark_group`（难度下有书签才出现，文本"书签 N"，
  委托画 ▸/▾ 折叠箭头，小一号次级色字体）与真二级 `bookmark`。
- **书签项 text = 纯名称**（利用 `QListWidgetItem` EditRole 与 DisplayRole 同源，
  内联编辑器编辑的正是名称）；缩进 + 行号徽章（accent 低透明度圆角块）由
  `OutlineItemDelegate` 自绘；名称超宽 elide，完整名称+行号进 tooltip；
  窄栏 icon-only 模式只画徽章。
- 折叠状态：`QHash<int,bool> outlineBookmarkGroupExpanded_` 只记录**显式切换**；
  未触碰的难度默认"active 难度展开、其它收起"（`isBookmarkGroupExpanded`）。
  点击组头只切折叠不切难度。
- 重建保留：折叠状态（hash 天然保留）、当前书签选中项（按 kind+difficulty+line
  重新定位）、滚动位置（重建前后 vbar value）。
- 活动书签：`activeBookmark{DifficultyId,Line}_`，委托在缩进槽画 3px accent
  竖条；单击书签跳行并原位更新两个 item 的 ActiveRole（`QSignalBlocker` 包住，
  避免触发重命名 handler）。
- 交互：单击=跳行；双击=`editItem()` 内联重命名（`editTriggers` 显式设为
  `NoEditTriggers`，编辑一律程序化启动）；`itemChanged` →
  `EditorSection::renameBookmark()`（trim、空名/同名拒绝、置 nameLocked、置
  dirty），随后 **queued** `rebuildFieldSidebar()`（编辑器可能还挂在 item 上，
  不能同步重建）恢复规范显示/还原空名。
- 右键菜单：书签项=重命名/删除/跳到时间轴位置（旧双击跳时间轴挪到这里）；
  书签组与难度项=插入书签（默认当前编辑器光标行，必要时先切难度）。
  「详情」入口删除。
- 滚动条：显式 `Qt::ScrollBarAsNeeded` + `UiTheme::scrollBarStyleSheet()`。
- `revealBookmarkInSidebar(difficultyId, line, beginRename)`：展开组 → 重建 →
  选中 + `scrollToItem(PositionAtCenter)` → 可选 `editItem`。
- 右键菜单内嵌 `QWidgetAction` 输入框方案（上文"第二阶段"）未做。

### D5. 编辑器入口

`PlainCodeEditor` 只发意图信号（沿用既有 `lineNumberBookmark*` 命名族，未采用
spec 建议的短名）：

- 新增 `lineNumberBookmarkRenameRequested(int)`、
  `lineNumberBookmarkDeleteRequested(int)`、
  `lineNumberBookmarkContextMenuRequested(int, QPoint)`；
  「在侧边栏显示」复用既有 `lineNumberBookmarkActivated`。
- 正文右键菜单顶部：无书签行=插入书签；有书签行=重命名/删除/在侧边栏显示
  （`contextMenuEvent`，行号取 `cursorForPosition`）。
- 行号区右键：`LineNumberArea::contextMenuEvent` 发
  `...ContextMenuRequested(line, globalPos)`，菜单由 MainWindow 组装（动作集
  与正文菜单书签段一致）。
- 行号区双击创建：`lineNumberBookmarkCreateRequested` 改接
  `EditorSection::createBookmarkAtLine(line, /*beginRenameInSidebar=*/true)`
  —— 无弹窗，默认名即刻生效，侧边栏随即进入内联重命名。
- 既有能力保留：双击已有书签行激活（现=跳行+侧栏定位）、行号区拖拽移动书签
  （`replaceBookmarkLine`，现在会置 dirty）。

### D6. 弹窗降级与工具箱

- `showCreateBookmarkDialog(ForLine)` / `openBookmarkAtLine`（详情弹窗）/
  `showBookmarkManager` 及整套 `BookmarkDialog*` 壳类**全部删除**（约 -600 行）；
  `openBookmarkAtLine` 语义由 `activateBookmarkAtLine`（跳行+侧栏 reveal+accent）
  接替。
- 工具箱「书签」子菜单只留 JSON 导入/导出兼容工具（导入成功置 dirty，导出含
  `name_locked`，schema 仍为 `miacode_bookmarks_v1`）；创建/管理入口删除，
  对应 `QAction` 成员一并移除。

### D7. 测试与验证

- `simai_document_spec` 新增 24 断言：round-trip（含非 ASCII、引号转义）、
  单行/单字段、不进 extraFields、`parseUnmanagedFields` 过滤、坏 JSON 不阻塞
  其它字段 + 置 error flag、裸空值非错误、空列表删字段、非法难度项逐条丢弃、
  locked 默认/往返。
- 全量 CTest 25/26（`plain_code_editor_spec` 的 completion-popup focus-out
  用例为实施前已存在失败，与本改动无关）。
- 手动 UI 验证清单见上文（待 GUI 验收）。
