# 代码审计：多语言处理分布 & UI 组件复用（2026-07-07）

> 审计范围：`src/` 全量。两个问题：
> 1. 多语言处理散落在何处，为何引入新语言经常出现 fallback；
> 2. UI 组件是否有统一模板，还是每个功能各画一种。
>
> 本文档同时记录整改方案（§3）。整改原则（用户拍板）：**简体中文是最全的基准语言；
> 其他语言缺失时从中文翻译过去。**

---

## 1. 多语言处理审计

全仓**没有**使用 Qt Linguist（`tr()`/`.ts`/`.qm` 全链路为零，仅 QuickShellMain.qml 有一处
无效 `qsTr`）。语言解析入口是集中的（`UiText.cpp` `resolvedLanguagePreference()`：
`MIACODE_LANG` 环境变量 > 存储偏好 > 系统 locale > 默认英文；语言切换需重启生效），
但**文案取词并存 7 套机制**：

### 机制 1 — 中央键值表 `UiText::text(key)`（"正统"路径）

- `src/app/ui/UiText.cpp` 内硬编码两张 QHash：`zhMap` 364 键、`jaMap` 412 键。
  查不到返回空串，由调用点的英文 fallback 参数兜底。
- **英文没有自己的表**，只以散落在 ~359 个调用点的字面量形式存在。同一个键在不同调用点
  可以有不同英文，覆盖率不可审计。
- **已发生键集漂移**：审计时 jaMap 有 48 个键 zhMap 没有（extensions 首选项、批量导出
  错误、`about.platform` 等）。实证：`MainWindow.ExportWorker.cpp:804`
  `uiText("dialog.batch_export.error.export_failed", "Export failed.")` —— 中文用户在
  这 48 处看到英文。这是"引入新语言经常出现 fallback"的机制性根源：没有单一键清单，
  加语言时各表各自演化。

### 机制 2 — 二元三元 `UiText::isChineseUi() ? 中文 : English`

- 249 处、51 个文件（重灾区：`MainWindow.Dialogs.MediaTools.cpp` 45 处、
  `MainWindow.FrameBootstrap.cpp` 37 处、`BootstrapAndMenus` 20、`DocumentUi` 15、
  `TrackMetadata` 14、`ValidationRuntime` 12）。
- 结构上就是 zh/en 二选一——日语以及未来任何新语言在这些点**永远**落到英文。

### 机制 3 — 五种同功能本地 helper，约 14 份拷贝

| Helper | 签名 | 位置 |
|---|---|---|
| `UiDialogs::text` | `(key, en_fallback)` | `DialogLocalization.h:390` |
| `uiText` | `(key, en_fallback)` | `MainWindowShared.cpp:240`、`VideoExportDialogInternal.h:40`（重复实现） |
| `l10n` | `(en, zh)` | `VideoExportDialogInternal.h:46` + cover_export 下 7 个文件各一份 |
| `trText` | `(zh, en)` —— 参数序与 l10n **相反** | `NetBatchDownloadDialog.cpp`、`NetBatchDownloadWorker.cpp` |
| `localizedText` | `(zh, en)` 成员函数 | `ExportLauncherPage`、`LatencyDetectionPage` |

后四种全部 zh/en 二元；`l10n(en,zh)` 与 `trText(zh,en)` 参数序相反是现成的写反隐患。

### 机制 4 — 解析器自带一套（core 层）

`SimaiNativeParser.Driver.cpp` `zhExactMap`/`zhPrefixMap`：以**英文报错原文为键**映射中文，
经 `localizeValidationDetail()` 按 `SimaiNativeValidationLocale`（原本只有 English/Chinese
两值）翻译。用消息原文当键极脆——改一个英文措辞，中文映射就静默失联。

### 机制 5 — Muri 层传 `bool chineseUi` 参数

`common/MuriTypes.cpp`：函数签名直接收 bool，又一处二元结构。

### 机制 6 — QML 侧只拿到一个布尔

`QuickShellStyleBridge.cpp` 往 paletteMap 塞 `isChineseUi` 布尔；`BottomTabsQuickHost.qml`、
`TimelineTabSurface.qml` 在 QML 里写 zh/en 三元。桥只导出布尔，QML 结构上不可能显示日语。
另有部分标签由 `QuickShellController.cpp` 预翻译后推入（同样是三元），且注释明确假定
"语言每会话恒定"。

### 机制 7 — 字体选择也用 `isChineseUi()` 分叉

`MainWindowShared.cpp` `uiFont`/`uiAccentFont`：中文 → 微软雅黑等候选；否则直接
Segoe UI。日语 UI 的假名/汉字走系统随机 fallback 字体，观感劣化。

### 附带发现

- **乱码字面量 6 处**（UTF-8 → GBK 双重编码损坏，`/utf-8` 编译下按乱码原样显示给用户）：
  `VideoExportDialog.cpp` 5 处（如 `"杈撳嚭"` 应为"输出"）+ `VideoExportDialog.ExportFlow.cpp` 1 处。
- `UiText` 同时还是偏好持久化（`loadPreferencesObject`）与主题偏好的宿主，被 HUD 字体、
  解码偏好等无关系统引用。i18n 整理时不动它，但列为后续拆分候选。
- `intro/qml` 的中文全在注释里，不涉及 UI 文案。

### Fallback 故障模式总结

1. 新语言（ja）加入 → 249 处三元 + 4 种二元 helper + Muri + 解析器 + QML 全部保持
   zh/en 二元 → 日语大面积回落英文。
2. jaMap 添键没同步 zhMap → 中文在 48 个键上回落英文（最全语言反而缺）。
3. 英文只存在于调用点 fallback → 无法审计、无法保证同键同文。

---

## 2. UI 组件复用评估

**结论：复用做到了原子层（色板/样式表工厂/3 个小控件），组合层（卡片、表单行、
工具栏按钮、设置对话框骨架）每个功能手搓一遍。**

### 已统一的部分

- 中央主题：`UiTheme.h` `Colors` 调色板（~50 个语义色）+ 明暗主题解析；QML 经
  `QuickShellStyleBridge` paletteMap 拿同一套色。
- 对话框脚手架：`DialogLocalization.h` `prepareDialogWindow()`（原生标题栏主题 +
  预览快捷键策略 + 锚定居中 + 激活）12 文件 18 处一致使用；`localizeButtonBox`/
  `showMessageBox` 统一标准按钮。
- 共享原子控件有真实复用：`BusySpinner`、`EditableValueLabel`、`FlowLayout`（28 文件）。
- 对话框控件样式表共享：`dialogSliderStyleSheet`/`dialogComboBoxStyleSheet`/
  `dialogPushButtonStyleSheet`/`dialogTabStripStyleSheet` 等。

### 未统一的部分（增量成本所在）

1. UiTheme 按"每功能一个样式表函数"线性膨胀（~45 个，`UiTheme.cpp` 57 KB）：
   `latencyDetectionPageStyleSheet`、`exportLauncherPageStyleSheet`、`metadataPageStyleSheet`、
   `preferencesDialogStyleSheet`、`settingsDialogStyleSheet`、`aboutDialogStyleSheet`…
2. 组合级控件工厂重复手搓：`LatencyDetectionPage` `makeCard`/`makeRowLabel`；
   `CoverStudioWindow` `makeToolbarButton`；`CoverFramePickerPanel` `makeTransportButton`；
   `CoverInspectorPanel` `makeSliderValueRow`；音频设置对话框内联搭同类行。
3. 203 处裸 `setStyleSheet` 散布 34 文件（WindowShell 26、FrameBootstrap 22、
   AudioSettings 18、VideoExportDialog 18…）——主题改动时的回归面。
4. 对话框构建双轨制：4 个 QDialog 子类（tools/）vs ~13 个 MainWindow section 内联
   巨型函数对话框。没有共享"带标签页的设置对话框骨架"。
5. QML 零组件库：12 个 QML 文件三处目录，无共享组件；各文件内联定义按钮/开关样式。

### 建议（未在本次整改范围内）

- 保留色板与对话框脚手架；新增一层组合组件（Card/FormRow/SliderValueRow/工具栏按钮
  工厂/TabbedSettingsDialog 基类）收编各处 `make*`；规则改为"新页面禁止新增 per-page
  样式表函数与裸 setStyleSheet"。
- 与多语言整改联动：三元大量长在手搓 UI 构建代码里，组件收编时同批消灭残余。

---

## 3. 多语言整改方案（本次实施）

目标架构（两条取词路径，各一条清晰规则）：

1. **键值路径**（已有键的字符串）：`UiText::text(key)` 不变；zhMap 补齐 48 缺键，
   使 zh/ja 键集完全相等；新增 spec 断言键集相等，杜绝再漂移。
2. **内联路径**（功能代码里的字面量）：唯一入口
   `UiText::localized(en, zh, ja = QString())`。
   - Chinese → zh；English → en；Japanese → 显式 ja 参数，缺省时查**中央 zh→ja 词典**
     （`UiText::japaneseByChineseText()`，独立 TU `UiTextJaDictionary.cpp`），
     词典也没有 → en。
   - "从中文翻译过去"落为工程事实：加语言 = 填一张以中文原文为键的词典，不再改 N 个
     调用点。词典键 = 中文原文（改中文文案时须同步词典，spec 扫源码兜底）。
3. **收编散落机制**：
   - 249 处 `isChineseUi() ? zh : en` 三元 → 脚本迁移为 `UiText::localized(en, zh)`。
   - `l10n`/`trText`/`localizedText` 本地 helper → 改为转发 `UiText::localized`
     （参数序统一为 en, zh）。
   - 解析器 `SimaiNativeValidationLocale` 增加 `Japanese` + `jaExactMap`/`jaPrefixMap`
     （从中文翻译）；locale 从 `UiText` 解析结果派生。
   - Muri 标签 `bool chineseUi` → 三值 locale。
   - QML 桥补导出 `uiLanguage`（"en"/"zh"/"ja"），QML 三元升三路。
   - 字体：`uiFont`/`uiAccentFont` 增加日语候选（Yu Gothic UI / Meiryo / Noto Sans CJK JP）。
4. **修复**：zhMap 48 缺键（从 ja/en 译回中文）；6 处乱码字面量还原为正确中文。
5. **防回归 spec**（`miacode_add_dev_tool(NAME TEST)`，仿 `debug_flag_index_spec` 的
   源码扫描模式）：
   - zhMap/jaMap 键集相等；
   - 源码中所有 `UiText::localized(en, zh)` 两参调用的中文原文必须在 zh→ja 词典中有词条。

不在本次范围：英文独立成表（键值路径的英文仍以调用点 fallback 存在）、`UiText` 与
偏好持久化的拆分、UI 组合组件库。
