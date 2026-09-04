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
- 每条历史设上限。**步的存储形式决定了这个上限该是多少**：最初每步存改动前后**两份全文**，
  于是敲一个字符就要两倍谱面大小，几个难度开着就是几十 MB，上限只能压到 200。
  改成只存**这一次改动本身**（`start` + 被替换文本 + 新文本）后，一步的代价等于这次编辑的大小，
  上限因此可以是一个「谁会往回翻这么多」的数字而不是内存天花板——现在是 **5000**。
  撤销本来就在应用时把那一对全文归约成同一个 span，直接记录它并没有丢任何信息。

这去掉了 2.3 的整类问题：不再有"身份变了要记得清"的时序依赖，
**因为再也不需要清**——取历史时就是按当前视图的 key 取。

**已完成（2026-08-30）**：`QmlEditorController` 的 `qmlUndo_/qmlRedo_` 换成
`QHash<QString, QmlHistory>` + `historyScopeId_`；`setHistoryScope` / `clearAllHistory` /
`dropHistoryScope` 三个入口。`SourceEditor.syncTextFromController()` 在**动文本之前**
直接从会话读出 scope 并命名（不等信号，避免顺序依赖）；`onDocumentReplaced` 全清；
`ViewState.closeEditor` 新增 `editorClosed(key)` 信号，编辑器据此丢弃那一份历史。
每份历史上限 200 步（每步存改动前后两份全文，无上限即无上限增长）。
`qml_editor_controller_spec` 新增三条断言，其中「切走再切回，那个难度的历史仍在」
正是所有者报告的那一条。

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
2. ~~**每视图撤销历史**（3.3）~~ —— **已完成（2026-08-30）**。
3. ~~**section 级脏**（3.1）~~ —— **已完成（2026-08-30）**。
4. ~~**关闭标签的守卫**（3.2）~~ —— **已完成（2026-08-30）**。
5. ~~**打开最近 / 关闭文档**~~ —— **已完成（2026-08-30）**。**新建仍缺失**：
   v1 的 `onNewFile` 要先选目录再建文件，是一条独立的流程，且所有者未报告，另行安排。
6.5. **打开最近的标签**（2026-08-30 修复）。原先整条路径当菜单文字，宽度不够就显示不全；
   改为 v1 的规则——**显示所在文件夹名**（对谱面而言就是曲名，因为文件永远叫 maidata.txt），
   完整路径移到 tooltip。`AppMenuItem` 因此新增 `tooltip` 槽位，恢复备份沿用同一形态。

6. ~~**两套三选一收敛成一套**~~ —— **已完成（2026-08-30）**。`MainView` 那份
   `unsavedChangesDialog` 已删除，打开 / 打开最近 / 关闭文档 / 关窗 / 音频拖放建谱
   全部走 `MainWindow::requestLeaveDocument`。

### 3.3.1 已修：scope 不能来自绑定（2026-08-30 用户报告）

每视图历史落地后仍出现「在某难度的修改能在另一个难度 Ctrl+Z」。原因是命名 scope 的方式：

`QmlDocumentModel::emitDocumentStateChanged()` 先更新内部状态，然后**依次**发出
`chartTextChanged` → `currentDifficultyChanged`。SourceEditor 原本用一个绑定属性
（`readonly property string historyScopeId: ... currentDifficultyId`）来命名 scope，
而绑定要等 `currentDifficultyChanged` 才更新——所以**新难度的正文到达时，scope 名还是旧难度**，
之后每一次输入都记进了被离开的那个难度的历史里，Ctrl+Z 于是重放了另一个难度的编辑。

改为在需要时**直接读属性**（`currentHistoryScopeId()`）：getter 读的是已经更新过的状态，
不受信号顺序影响。另外 `onDocumentStateChanged` 也会重申一次 scope，覆盖那些
「切了难度但没有文本变化」的路径。

**测试覆盖的诚实说明**：spec 里的假会话是普通 QML 属性，赋值即通知，
无法复现「状态已新、通知未到」这个真实顺序——只有真正的 `QmlDocumentModel` 会那样发信号。
因此这一条钉的是**调用形式**（必须是函数读取，不得是绑定属性），不是竞态本身。

## 7. 本轮新发现，尚未处理

- [x] **未命名文档选「保存」没有路径可写**（2026-08-30 修复）。原记为"掉进 Widgets 的
      `QFileDialog`"；Save 改为逐 section 之后，v2 的离开流程已不再走 `onSaveFile()`，
      所以实际症状变成了**更糟的一种**：`fileService_->save()` 以空路径被拒，
      提示直接消失，什么都没写、也什么都没说——一个死按钮。
      现在保存在没有路径时先**要一个路径**：走 `UiRequestService::requestFile`
      （QML 的 FileDialog，不是 Widgets），拿到路径后以**整份文档**写入
      （新文件没有"其他难度的旧内容"可保留），再继续原来的续延；
      取消选择＝取消保存＝取消这一步所属的整个动作，因为什么都没写进去。
      关标签的保存同样改为异步（`requestSaveDifficultySection` + `sectionSaveFinished`），
      **只有真的写进去了才关标签**。
      漂移守卫：`qml_document_lifecycle_contract_spec`。
- [x] **新建文档**（2026-08-30 补完）。`文件 → 新建`（Ctrl+N），过同一条离开守卫，
      然后选文件夹（`UiRequestService` 的 folder 请求）→ 在其中写入 `maidata.txt` →
      **按打开一份普通谱面的方式打开它**，所以新文档从一个真实的 save point 开始，
      而不是一到手就是脏的。已存在时先确认覆盖。写入落在
      `ChartWorkspaceFileService::createEmptyDocument()`——文件边界的活，不该由 QML 层做。
      **2026-09-05 入口收口**：旧 `Session` 的新建、打开、最近文件及保存转发已删除；
      `DocumentFileFlow.cpp` 中只服务于旧菜单的 native 新建/打开文件对话框也已删除。QML
      新建链路由 `QmlDocumentModel` 负责音频选择、文件服务写入和普通打开，
      `onSaveFileAs()` 仍仅作为无当前路径时的 native 保存 fallback 保留。
- [x] **自动保存的「恢复备份」在 v2 没有入口**（2026-08-30 查证并补完）。
      `文件 → 恢复备份` 列出快照（时间戳标签，重名时附文件名，与 Widgets 菜单同规则），
      选中后走既有的 `restoreBackupFilePath()` 确认流程；因为恢复会替换文档，
      入口本身也过离开守卫。原查证结论（自动保存本身在跑）见下：自动保存本身**是在跑的**：
      `updateDirtyState()` 在每次 v2 提交经 `applyCommittedQmlDocument` 时启动计时器，
      快照文本取自镜像文档（`editorText()` 现在读的是文档而不是已删除的隐藏编辑器），
      所以内容正确。异常退出后的自动询问也已于 2026-08-29 改走 `UiRequestService`。
      **更正（2026-08-30，所有者报告后复核）**：这条只对了一半。2 分钟的例行快照确实在跑，
      但 **2 秒防抖的 `autosaveIdleTimer_` 与崩溃快照投递都挂在 `markCurrentFieldDirty()` 上，
      而它的调用方全是隐藏 v1 输入框的 `textChanged`，v2 从不触发**——
      所以"每次编辑的安全网"在 v2 上不存在。详见 TODO §7.-2。
      **缺的还有手动那一半**：v1 的 `refreshRestoreBackupMenu()` 是挂在隐藏 `MainWindow` 上的
      `QMenu`，v2 没有任何地方能列出并选择历史备份。

- [x] **空的文件标签刷新桥**（2026-09-05，已清理）。`PlaybackCoordinator::updateCurrentFileLabel()`
      没有实现内容，且唯一调用只发生在设置当前文件路径时；已删除其 coordinator/Session 声明、
      转发、空实现和调用。文件路径变化后的真实窗口/QML 标题更新仍由 `updateWindowTitle()` 负责。
- [x] **runtime 的空 UI 桥**（2026-09-05，已清理）。旧媒体工具/关于/设置槽、文档侧栏与页面
      填充方法、按难度设计师空对话框入口和 `noteStatus` 均没有可见实现或生产消费方；已删除
      对应转发、调用点和仅服务于旧 Widget 侧栏的折叠状态。QML 书签状态继续由 `ViewState.qml`
      维护，实际文档、媒体工具和页面切换流程未删除。Release 构建通过，相关测试 8/8 通过，
      全量 CTest 仍为 105/108，失败项与既有基线一致。
- [x] **文档错误/确认回退**（2026-09-05，已收口）。打开失败改走
      `UiRequestService::postNotice`，删除难度的确认由 QML `DifficultyList` 完成后再调用文档
      变更；因此 `DocumentFileFlow.cpp` 与 `DocumentPages.cpp` 不再依赖 `QMessageBox`。
      同步未保存确认仍是有实际行为的 native fallback，暂不删除。

## 6. 所有者已拍板（2026-08-30）

**A. 关闭一个脏标签时，"保存"和"放弃"分别意味着什么？**

- ~~A1~~ **已被所有者改判（2026-08-30）**：**所有 Save 都只存当前难度**，不只是关标签那一个。
  写入文件 = 上次 save point 的内容 + 这一个难度的最新改动，其他难度在磁盘上保持原样。
  Ctrl+S / 菜单保存 / 关标签 / 关窗全都如此；save point 因此变成**逐 section** 推进的。
  **例外：另存为写整份文档**——新文件没有"其他难度的旧内容"可以保留，只写一个 section
  等于把谱面其余部分丢在地上。
  放弃仍是只回滚这个 section。

  **连带的必然结果**：关窗时不能只问一次。Save 既然只存一个难度，一次提问最多救回一个，
  剩下的会随着关闭一起消失。所以关窗（以及任何"离开文档"）改为**逐个未保存难度提问**，
  提问前先把编辑器切到那个难度——这既是所有者要的「切换到第一个未保存窗口」，
  也是让提问指向一个看得见的东西。全部难度处理完后，如果文件仍然脏
  （元数据、增删难度），最后再以**文件**为对象问一次。
- A2：保存 = 存整份文件；放弃 = 只关标签、**不回滚**（改动仍在文档里，文件仍然脏）。
  这更像 VS Code 的"关闭但不保存"，但会留下一个看不见的脏改动。
- A3：关闭标签**永不询问**，只在文档级动作时问。（等于不做 3.2；所有者的第 1 条即被否决）

**B. 「关闭文档」之后落到哪里？**

- B1：空文档（`SimaiDocument::createEmpty()`），编辑器可直接开始写。
- **B2 ✅ 选定**：空状态，没有可编辑的正文。
  **补充决定（2026-08-30）：暂不做欢迎页，留空即可。** 关闭文档后没有打开的标签、没有正文；
  欢迎页作为独立的产品项另行安排。

**C. 每视图撤销历史在「打开别的文件」后是否保留？**

- **C1 ✅ 选定**：不保留，全清。换文件 = 换一份历史。
- C2：按文件路径保留。未采纳。
