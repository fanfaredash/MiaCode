# v2 文档机制审计与重新设计

> 起因：所有者 2026-08-30 报告 7 项缺陷（关闭/切换未询问保存 ×4、难度切换后置脏与撤销 ×2、
> Esc 输入特殊字符 ×1），并判定「文档机制需要一个全盘审计和重新设计（和 v1 不完全相同）」。
>
> 本文分两部分：**第 1–2 章是审计**（每条结论都标注是「读码确认」还是「待运行复现」）；
> **第 3–6 章是重新设计**，含需要所有者拍板的三个产品决定。
>
> 基线：`ab5e5715`（2026-08-30）。

## 1. 现状：三个单位，各自为政

v2 文档域现在有三个互不对齐的"单位"：

| 单位 | 谁拥有 | 粒度 |
|---|---|---|
| **持久化单位** | `ChartWorkspaceFileService` | 一个 `maidata.txt` = 元数据 + 全部难度 |
| **编辑视图单位** | `ViewState.openEditorTabs` | 每个难度一个标签 + 一个元数据标签 |
| **脏 / 撤销单位** | `ChartWorkspace.savedSourceText_` / `QmlEditorController.qmlUndo_` | **整份文件**一个脏标志；**全局一个**撤销栈 |

所有 7 项缺陷都落在这三行的错位上。v1 没有这个问题，因为 v1 根本没有"标签"——
它一次只显示一个字段，并用 `currentFieldDirty_` 表示"编辑器里这一份还没提交回文档"。
v2 的每次编辑**立即提交进工作区**，所以 `currentFieldDirty_` 在 v2 没有意义
（`applyCommittedQmlDocument` 里它被无条件写成 `false`），但**没有任何东西接替它的位置**。

## 2. 逐条定位

### 2.1 Esc 在正文里输入一个特殊字符 —— 读码确认，根因精确

`SimaiTextEditPolicy::process()` 的最后一段是无条件兜底：

```cpp
// A policy edit only covers text that Qt would otherwise insert directly.
replaceSelection(&result, input);
result.consumed = true;
```

它唯一的门是 `canHandleInput = !input.isEmpty() && !hasCommandModifier(...)`。
**Esc 的 `event.text()` 是 `U+001B`，非空、无修饰键**，于是一路落到这里，被当成正文插入。

注释里那句假设是错的：Qt **不会**插入 Esc——`QInputControl::isAcceptableInput` 会因
`QChar::Other_Control` 且不可打印而拒绝。策略比 Qt 更宽松，所以策略自己制造了这个字符。
同类还有任何"`text()` 非空、不可打印、无修饰"的键。

Esc 在树上唯一的处置只有一处：`QmlEditorController::processKey` 里
**补全弹窗打开时**关闭补全。弹窗没开时，Esc 无人认领。

### 2.2 难度切换后的置脏 —— 读码确认

`QmlDocumentProjection.cpp:81-85`：

```cpp
if (input.dirty) {
    state.dirtyEditorKeys = input.activeDifficultyId > 0
        ? QStringList{QStringLiteral("difficulty:%1").arg(input.activeDifficultyId)}
        : QStringList{QStringLiteral("metadata")};
}
```

工作区只有**整份文件**一个 `dirty`，而标签栏要画**每个标签**的脏点。
于是这里把整份文件的脏"记在"当前活动标签头上。后果：

- 改 Expert → 切到 Master：Expert 的脏点消失，Master 凭空多出一个脏点。
- 只改元数据：切到任一难度标签，脏点跑到那个难度上。
- 关掉当前标签：脏点转移到下一个被激活的标签。

**脏点指的从来不是标签，是文件。** 这不是显示 bug，是缺一个数据。

### 2.3 难度切换后的 Ctrl+Z —— 读码确认结构，失败模式待运行复现

撤销是 `QmlEditorController` 里**一个**栈（`qmlUndo_` / `qmlRedo_`），
`SourceEditor.syncTextFromController()` 在"身份变了"（难度 id 或元数据模式变化）时调用
`resetQmlHistory()` **清空**它。

所以按设计：**切换难度 = 撤销历史被丢弃**，切回来也不恢复。这本身就不是所有者期望的行为。

失败模式还有第二种可能，需要运行复现来区分：`identityChanged` 的判断依赖
`documentSession.currentDifficultyId` 在 `chartTextChanged` 触发时**已经**是新值。
读码看是成立的（`emitDocumentStateChanged()` 先跑 `refreshDocumentState()` 再发信号），
但只要哪条路径颠倒了这个顺序，历史就不会被清——于是**上一个难度的撤销记录会被应用到当前难度的正文上**，
那是比"撤销没了"严重得多的数据损坏。**复现要点**：改 A → 切 B → Ctrl+Z，
看 B 的正文是否被 A 的历史改动。

### 2.4 子窗口（标签）关闭不询问保存 —— 读码确认

`ViewState.closeEditor(key)` 只做三件事：从 `openEditorTabs` 移除、从历史移除、选下一个活动标签。
没有任何询问，**而且以现在的数据也问不出来**：没有"这个难度脏不脏"这个事实（见 2.2）。

### 2.5 切换文档不询问保存 —— 部分成立

`MainView.requestOpenFile()` **会**问（`documentSession.dirty` → `unsavedChangesDialog`）。
但这是 v2 里**唯一**的文档切换入口。见 2.6。

### 2.6 缺失的入口 —— 读码确认

v2 的 文件 菜单只有 打开 / 保存 / 另存为 / 退出。**没有 新建、没有 打开最近、没有 关闭文档。**

- `MainWindow` 里 `recentFilePaths_`、`refreshRecentFilesMenu()`、`openRecentFile()` 都还在，
  且 `openRecentFile()` 本来就调用 `maybeSaveBeforeContinue()`——但它只挂在隐藏 `MainWindow`
  的 `QAction` 上，**QML 外壳没有任何路径能走到**。
- 「关闭文档」在 v2 里**根本不存在**（v1 的对应物是 `switchToWelcomePage()`，同样不可达）。

所以所有者说的第 4 条不只是"没询问保存"，是**功能整体缺失**。

### 2.7 关闭窗口询问保存 —— 已于 `ab5e5715` 修复

两段式关闭 + `UiRequestService::requestChoice`。本轮不再重复。

## 3. 重新设计：脏是两级的

**一句话**：文件是持久化单位，节 (section) 是编辑与脏的单位；
撤销跟着视图走，不跟着文件走。

### 3.1 节 (section)

一个 section = 一个难度的正文，或者元数据。它正好对应一个编辑器标签，
也正好对应 `SimaiDocument` 里可独立比较的一块。

`ChartWorkspace` 在每个 save point（打开、保存）除了记 `savedSourceText_`，
再记一份 **`savedDocument_`（解析后的 `SimaiDocument`）**。于是：

- `sectionDirty(sectionId)` = 当前 document 的该 section 文本 ≠ `savedDocument_` 的同一 section
- `dirty`（整份） = 任意 section 脏，或结构变化（增删难度、元数据字段）

代价：每个 save point 多解析一次（打开/保存时各一次，不在编辑热路径上）；
每次查询是一次 `QString` 比较。

**这同时修掉 2.2**：`dirtyEditorKeys` 不再是"猜"，而是真的每个标签一条。

### 3.2 关闭一个标签

标签脏 → 弹三选一。这里有一个**必须由所有者拍板**的语义：

> 文件是持久化单位，所以"保存"必然保存**整份文件**（包括其他 section 的改动）。
> 而"放弃"可以只回滚**这一个 section**。

三个候选语义见第 6 章问题 A。

### 3.3 撤销：每个视图一条历史

`QmlEditorController` 把 `qmlUndo_` / `qmlRedo_` 换成 `QHash<QString, History>`，
键就是编辑器标签的 key（`difficulty:3` / `metadata`）。

- 切换难度：**不再清空**，只是换一条历史。切回来撤销仍然可用。
- 文档被替换（打开别的文件、恢复备份）：**全部清空**——那是另一份文档的历史。
- 关闭标签：连同它的历史一起丢弃。
- 每条历史设上限（建议 200 步），避免长会话无限增长。

这去掉了 2.3 的整类问题：不再有"身份变了要记得清"的时序依赖，
**因为再也不需要清**——取历史时就是按当前视图的 key 取。

### 3.4 Esc 与所有非文本键

策略的兜底改成只覆盖**Qt 真的会插入的文本**，与 `QInputControl::isAcceptableInput` 对齐：

```cpp
bool isInsertableText(const QString& input);  // 可打印 / Other_Format / '\t'
```

`canHandleInput` 加上这一条。非文本键一律 `consumed = false` 落回 Qt，Qt 自己会拒绝。

Esc 的归属链就此明确：补全弹窗关闭它自己 → 查找栏关闭它自己 → 编辑器里什么都不做。
**永远不进正文。**「收起选区」这类新行为不在本次范围内，需要时另作产品决定。

**已完成（2026-08-30）**：`isInsertableText()` 与 `QInputControl::isAcceptableInput` 对齐，
`canHandleInput` 与 `wouldInsertLiteralCommandText` 都改用它。
`simai_text_edit_policy_spec` 新增七例：Esc / Backspace / Delete / Cancel 不输入任何东西，
字母 / 制表符 / 零宽连接符照常输入。

### 3.5 文档级动作与它们的守卫

统一成一条：**任何会丢弃当前文档内容的动作，必须先过 `requestLeaveDocument`。**

| 动作 | 现状 | 设计后 |
|---|---|---|
| 打开 | 已守卫（QML 侧） | 走同一条 `requestLeaveDocument` |
| **新建** | **缺失** | 新增，守卫 |
| **打开最近** | **缺失** | 新增（`recentFilePaths_` 已在），守卫 |
| **关闭文档** | **缺失** | 新增，守卫，落到空/欢迎态 |
| 关闭窗口 | 已守卫（`ab5e5715`） | 不变 |
| 音频拖放建谱 | 已守卫（`ab5e5715`） | 不变 |
| 关闭标签 | 无 | 新增 **section 级**守卫（3.2） |

QML 侧那份 `unsavedChangesDialog` 与 C++ 侧的 `requestChoice` 目前是**两套**同样的三选一。
设计后收敛成一套：QML 只负责画，决定权在 `requestLeaveDocument` / 新增的
`requestLeaveSection`，两者都返回续延。

## 4. 与 v1 的差异（有意为之）

| | v1 | v2 设计 |
|---|---|---|
| 编辑器持有的未提交文本 | 有（`currentFieldDirty_`） | **没有**。编辑即提交进工作区，"脏"只相对于 save point |
| 脏的粒度 | 字段级 + 文档级，两者语义不同 | section 级 + 文档级，**后者是前者的或** |
| 撤销 | 一个 `QTextDocument` 的原生栈，切字段即失效 | 每视图一条，切换保留 |
| 关闭字段 | 无"标签"概念，切字段即提交或询问 | 关闭标签是**视图**动作，只在该 section 脏时才问 |

## 5. 落地顺序（建议，每项一个提交）

1. ~~**Esc / 非文本键**（2.1）~~ —— **已完成（2026-08-30）**。
2. **每视图撤销历史**（3.3）——独立于脏的改造。
3. **section 级脏**（3.1）——`ChartWorkspace` + 投影 + 标签栏脏点。
4. **关闭标签的守卫**（3.2）——依赖 3。
5. **新建 / 打开最近 / 关闭文档**（3.5）——依赖统一的 `requestLeaveDocument`。
6. **两套三选一收敛成一套**。

## 6. 所有者已拍板（2026-08-30）

**A. 关闭一个脏标签时，"保存"和"放弃"分别意味着什么？**

- **A1 ✅ 选定**：保存 = 存整份文件；放弃 = 只回滚这个 section，其他 section 不动。
- A2：保存 = 存整份文件；放弃 = 只关标签、**不回滚**（改动仍在文档里，文件仍然脏）。
  这更像 VS Code 的"关闭但不保存"，但会留下一个看不见的脏改动。
- A3：关闭标签**永不询问**，只在文档级动作时问。（等于不做 3.2；所有者的第 1 条即被否决）

**B. 「关闭文档」之后落到哪里？**

- B1：空文档（`SimaiDocument::createEmpty()`），编辑器可直接开始写。
- **B2 ✅ 选定**：欢迎页 / 空状态，没有可编辑的正文。v2 目前**没有**这个页面，需要一并补。

**C. 每视图撤销历史在「打开别的文件」后是否保留？**

- **C1 ✅ 选定**：不保留，全清。换文件 = 换一份历史。
- C2：按文件路径保留。未采纳。
