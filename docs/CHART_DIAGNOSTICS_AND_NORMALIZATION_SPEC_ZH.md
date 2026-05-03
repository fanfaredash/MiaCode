# 谱面诊断与规范化规格

本文档梳理三个相关但独立的子系统，它们都作用于 simai 谱面文本，并把结果
呈现在编辑器底部 tabs 面板中：

1. **语法 / Grammar 检测** —— 由 "语法检查 / Syntax Check" tab 呈现，
   抓出 simai 结构异常。
2. **无理 / Irrational 模式检测** —— 由 "无理检查 / Muri Diagnostics" tab
   呈现，抓出手不可达 / 不可奏的模式。
3. **谱面规范化（精简与归约）** —— 通过 "调整 / Modify" 菜单触发，
   把谱面文本重写为标准形式。

把本文档作为更详细规格的导航索引。当某个子系统已有深度规格时，行内会列出
引用；本文档的内容是**接口契约**加上**贯穿三者的设计决策**。

## 0. 共同基础

三个子系统都作用于编辑器持有的 **同一份 simai 文本**。它们在 parse 时的
严格程度不同：

| 子系统 | parser pass | 遇到语法错误时的行为 |
|---|---|---|
| 语法检测 | strict | 报告错误；尽力对剩余部分继续 parse |
| 无理检测 | lenient | 跳过坏掉的段落，分析能 parse 的部分 |
| 规范化 | lenient | 如果结构上无法 parse，返回 `errorMessage` 并放弃 |

共享的 simai parser 位于
`src/core/chart/parser/SimaiNativeParser.{h,cpp}`，按特性拆成 tap、touch、
slide、hold 和 strict-format check 等多个实现文件。诊断信息发射进
公共结构体 `SimaiNativeMessage`（line, col, endCol, message），三个子系统
都消费它。

底部 tabs 的 UI 宿主是 `BottomTabsQuickHost.qml`；每个 tab 的可见性由
`QuickShellController` 暴露的 `controller.<tabName>TabVisible` 属性决定。

---

## 1. 语法检测 —— "语法检查 / Syntax Check"

纯文本形态校验。每次编辑时都会跑；结果以一列错误 / 警告行渲染在 timeline
下方面板。

### 1.1 两种 parse 模式

Simai parser 暴露的是单一 `parseInternal()` 内核，通过 `strictMode`
布尔开关产生两种语义不同的 pass：

| 模式 | 公共 API | `strictMode` | 用途 |
|---|---|---|---|
| **Lenient** | `SimaiNativeParser::parseForTimeline()`（`SimaiNativeParser.h:59`） | `false` | 实时 timeline marker 抽取；每次编辑器编辑都会跑 |
| **Strict** | `SimaiNativeParser::validateSyntax()`（`:62`） | `true` | "语法检查" tab 列表；通过 `buildValidationReport()` 触发 |

两种 pass 共用同一份 parser 代码，都产生
`SimaiNativeParseResult { ok, errors, warnings, measureLineSeconds,
beatMarkers, noteMarkers, durationSeconds }`。差别在于
*哪些* 检查会被激活：

#### Lenient pass（`strictMode = false`）

目标：**无论文本是否完美，总是抽取一组可用的 marker。** 仅当一个 token
无法产出 marker 时才报错。

Lenient 的错误 / 失败点：

- 阻碍后续 parse 的括号 / 结构错误
  （`[ ]`, `( )`, `{ }` 不匹配；未终止的 `(BPM)` / `{N}` / `[HS*]` 块
  —— `SimaiNativeParser.Driver.cpp` 第 521、543、584 行）。
- 非法的 lane / ring / pad token（被
  `MuriConfig.h::padTokenIsValid()` 拒绝 —— 环 `A..E`、lane `1..8`、
  中心 `C`）。
- 异常的 slide / wifi 路径或 `[N:M]` 时间分数
  （`SimaiNativeParser.Slide.cpp`）。
- 异常的 tap / touch / hold token
  （`SimaiNativeParser.TouchTap.cpp`）。
- 非法的 `(BPM)` / `{N}` 数值（如 0、负数、NaN）。

Lenient *不* 关心：modifier 排序、`{N}` 与 384 的整除性、重复分隔符、
time-signature 摆放风格。这些都是 strict-only。Lenient pass 在
`errors == 0` 时意味着「谱面文本结构上足够规整，可以构建 timeline」。

#### Strict pass（`strictMode = true`）

目标：**捕获*所有*偏离 simai 典范形式的写法**，包括 lenient 会默默接受的
那些。在 lenient 的全部检查之上再叠加：

| 仅 strict 的检查 | 来源 | 例子 |
|---|---|---|
| `{N}` 必须整除 384 | `SimaiNativeParser.Driver.cpp:565` `(384 % beats) != 0` | `{7}` 拒绝（7 ∤ 384）；`{16}` 接受 |
| Beat 数值 clamp 警告 | `:561` `formatBeatValueClamped(beats)` | `{1024}` 被 clamp 到可表示的最大值 |
| 重复 `/` 分隔符 | `:593-595` `kRepeatedSlashSeparator()` | `1//5` 被标记 |
| 重复 `` ` `` 分隔符 | `:604-606` `kRepeatedBacktickSeparator()` | `` 1``5 `` 被标记 |
| Modifier 典范顺序 `b x h f` | `SimaiNativeParser.StrictChecks.cpp`（`kInvalidHoldModifierSequencePrefix`、`kNonCanonicalHoldModifierPlacementPrefix`） | `1xb` 而不是 `1bx` |
| Break-slide `b` modifier 位置 | `kInvalidBreakSlideModifierPositionPrefix` | slide 头/体上 `b` 错位 |
| Slide / hold 时值块位置 | `kInvalidSlideDurationPlacementPrefix`、`kInvalidSlideDurationPrefix`、`kInvalidHoldDurationPrefix` | 时值块位置错误 |
| Touch 时值需要 `h` 修饰符 | `kTouchDurationRequiresHPrefix` | touch token 上有时值但没 `h` |
| Time-signature `||x/y` 摆放 | `SimaiNativeParser.StrictChecks.cpp:105` | inline TS 出现在意外位置；纯注释 `||` 行被跳过 |
| 文件结尾 strict flush | `SimaiNativeParser.Driver.cpp:652` | 末尾 token 状态校验 |

Strict 还额外调用 `StrictChecks.cpp` 中的全套校验器，通过
`SimaiNativeParser.cpp:1487` 末尾的
`#include "SimaiNativeParser.StrictChecks.cpp"` 引入。

### 1.2 合并步骤 —— `buildValidationReport()`

「语法检查」tab 并不直接展示 strict 错误。它会调用
`buildValidationReport()`（`SimaiNativeParser.Driver.cpp:986-1068`），
该函数同时跑**两种 pass** 并产出合并报告。输出类型：

```cpp
struct SimaiNativeValidationReport {
    bool ok;
    int errorCount;
    int warningCount;
    int lenientNoteCount;
    int lenientErrorCount;
    int strictNoteCount;
    int strictErrorCount;
    QVector<SimaiNativeValidationIssue> issues;
};
```

合并逻辑（Driver.cpp:1023-1050）：

1. 取 *strict* 错误列表作为 issue 源。
2. 对每个 strict 错误计算稳定 key，看 lenient pass 是否在同一位置也报了
   同样的 key。
3. **如果 lenient 也在同一位置失败** → severity 保持 `Error`
   （这是真正的结构性问题）。
4. **如果只有 strict 报错** → severity 降级为 `Warning`
   （谱面能正常 parse，只是写法不典范）。
5. **例外列表 —— `shouldRemainValidationError()`**
   （Driver.cpp:235-241）即使只有 strict 报，也保持 `Error`，因为这些
   几乎一定会引起下游错误：
   - 重复 `/` 分隔符
   - 重复 `` ` `` 分隔符
   - 不匹配的右括号（任何种类）
   - 未闭合的左括号（任何种类）
6. Strict *警告*（在 strict pass 中本就是 `Warning`）原样保留。
7. 空谱面捷径：`text.trimmed().isEmpty()` 时跳过两次 pass，直接产出一条
   `kChartEmpty()` 原始 message 的 `Error` issue。

这就解释了报告里那四个计数的存在：`lenientNoteCount` 与 `strictNoteCount`
显示每种 pass 抽出多少 marker（通常一致，但 strict 拒绝某些 lenient
能接受的 token 时会发散）；`lenientErrorCount` / `strictErrorCount`
让你一眼看出 issue 列表里多少是「结构性」、多少是「风格性」。

#### 严重度降级示例

| 输入 | Lenient | Strict | UI 严重度 |
|---|---|---|---|
| `1[4:1`（未闭合括号） | Error | Error | **Error**（lenient 同意） |
| `1//5`（重复 `/`） | OK | Error | **Error**（在 `shouldRemainValidationError` 例外列表中） |
| `1xh`（modifier 非典范） | OK | Error | **Warning**（仅 strict，降级） |
| `{7}1,2,3,`（{7} 非 384 整除） | OK | Error | **Warning**（仅 strict） |

### 1.3 本地化展示

`buildValidationReport(text, locale, ...)` 接受
`SimaiNativeValidationLocale::English` 或 `Chinese`。parser 输出的原始
`message` 通过以下表映射：

- `ValidationMessage::zhExactMap()`（Driver.cpp:230） —— 完全匹配的中文
  翻译。
- `ValidationMessage::zhPrefixMap()`（Driver.cpp:243） —— 带可变后缀
  message 的前缀型中文翻译。

格式：`[ERROR] <已本地化的详情>` 或 `[WARNING] <已本地化的详情>`。

### 1.4 隐藏的运行时开关 —— invalid-star 预览

`SimaiNativeParser::setInvalidStarPreviewEnabled(bool)` /
`invalidStarPreviewEnabled()`（`SimaiNativeParser.h:65-66`，
Driver.cpp:976-984）控制一个仅 debug 用的预览路径，让用户在写谱时
看到「非法」星 slide 长什么样。仅 `parseForTimeline` 尊重该 flag
（通过 `parseInternal` 的第三个参数）；`validateSyntax` 始终传
`false`。这个 flag 是设置项驱动的、不属于公共规格的一部分，但在分析
两种模式下 marker 数量发散时会用到。

### 1.5 问题呈现

每个 issue 是 `SimaiNativeValidationIssue`（头文件 `:36-43`）：

```cpp
struct SimaiNativeValidationIssue {
    int line;
    int col;
    int endCol;
    SimaiNativeValidationSeverity severity;  // Error | Warning
    QString rawMessage;
    QString displayMessage;     // 已本地化，含 "[ERROR]"/"[WARNING]" 前缀
};
```

UI 上的呈现：

- **面板列表**：每个 issue 一行，`[Lline Ccol]` 前缀 + 显示文本。
- **编辑器 extra-selection**：在 `(line, col..endCol)` 上叠加红色波浪线 /
  琥珀色下划线（由 `refreshEditorExtraSelections` 驱动）。
- **header 级 ignore**：每条 issue 带一个 issue-type 字符串 key，谱面作者
  可以在文件顶部加 `||#ignore <key>` 行来项目级别禁用某个分类。

### 1.6 语法检测**不**做什么

- 不报无理 / 可奏性问题（那是子系统 2 的事）。
- 不自动修复；重写是子系统 3 的工作。
- 不阻塞 parse —— `parseForTimeline()` 无论 strict 错误是否存在都会跑，
  让用户能边编辑边看到面板里的 strict 警告。

---

## 2. 无理模式检测 —— "无理检查 / Muri Diagnostics"

检测 **手不可达** 模式。作用于已经 parse 出的 timeline marker，不是原始
文本。两种分析模式 —— 运行时（180-TPS 手势模拟）和静态（几何 + 时间阈值）
—— 合并到同一个面板。

### 入口

| 表面 | 文件 / 符号 |
|---|---|
| Runtime analyzer | `MuriAnalyzer::analyze()` —— `src/tools/muri/MuriAnalyzer.h` |
| Static analyzer | `miacode::muri::buildStaticMuriReferences()` —— `src/tools/muri/MuriStaticChecker.h` |
| 面板合并 | `miacode::muri::buildVisibleMuriPanelEntries()` —— `src/tools/muri/MuriPanelEntries.h` |
| 检测枚举 | `MuriKind` —— `src/common/MuriTypes.h:16-22` |
| 渲染期选项 | `MuriRenderOptions` —— `src/common/MuriRenderOptions.h` |
| 静态阈值配置 | `kStaticTapOnSlideThresholdDefaultMs` —— `src/common/MuriConfig.h` |
| UI 宿主 | `MainWindow::ValidationSection`（与语法共用） —— 同一份 `.Validation*.cpp` |
| 底部 tab 门控 | `controller.muriTabVisible` |

### 检测分类（`MuriKind`）

| Kind | 中文 | 检测条件 |
|---|---|---|
| `SlideTooFast` | — | slide / wifi 最终判定落在判定窗外 |
| `SlideHeadTap` | 外无 | slide / wifi 头预判后续 tap / hold / star |
| `TapOnSlide` | 撞尾 | slide / wifi 尾或路径与后续 tap / hold / star 冲撞 |
| `Overlap` | 叠键 | 同一 pad 上的两个同时按 |
| `MultiTouch` | 多押 | 所需手数超过 2（仅 runtime 模式） |

每个 kind 对应一个 `MuriAlertLevel`（`Muri` 硬错 / `Warning` 软警告）。
映射规则 —— 共六条 —— 收录在已有的 `docs/MURI_DETECTION_SPEC.md` 里，
连同共享的 180-TPS 常量、面板合并 / 去重规则。

### 静态 vs 运行时拆分

| 方面 | 运行时（`MuriAnalyzer::analyze`） | 静态（`buildStaticMuriReferences`） |
|---|---|---|
| 输入 | 实时 markers + 真实判定时间 | markers + 时间阈值 |
| 检测 `MultiTouch` 吗？ | 是 | 否 |
| 可配置阈值 | n/a | `staticTapOnSlideThresholdMs`（150–250 ms，默认 200） |
| Slide-head extra-pad-down 窗口 | `slideTraceSecond + min(50ms, first-area-enter)` | 同一常量 |
| 输出 | `MuriPanelEntry` 列表，附 markers 交叉引用 | 静态参考列表，并入运行时面板 |

### 影响检测 / 显示的渲染期选项

`MuriRenderOptions`：

- `renderMode` —— `Native` vs `MaimuriDxStyle`（视觉变体）
- `showSlideTracks`、`showJudgeMarkers`、`showTouchTrail` —— 显示开关
- `wifiNeedC` —— wifi 几何规则是否需要中心 C
- `excludeTouchFromMultiTouch` —— 把 touch 从 hand-count 中排除

用户可配置的静态阈值通过 Validation 菜单 →
`onEditStaticTapOnSlideThreshold()`（`MainWindow.ValidationSection.h:63`）触发，
范围 `[kStaticTapOnSlideThresholdMinMs, kStaticTapOnSlideThresholdMaxMs]`。

### 已有详细规格

`docs/MURI_DETECTION_SPEC.md` 是该子系统的权威参考（覆盖所有六条 alert-level
规则、180-TPS 基准、面板合并 / 去重、跳转语义）。测试覆盖：
`docs/MURI_DETECTION_TEST_CHECKLIST.md`。

---

## 3. 谱面规范化 —— "调整 / Modify" 菜单

把谱面文本重写为标准形式。**绝不会被隐式触发** —— 始终由用户从菜单触发，
通过单个 undoable 编辑块完成。两个独立选项可以叠加。

### 入口

| 表面 | 文件 / 符号 |
|---|---|
| 公共 API | `normalizeChartText()`、`normalizeChartSelectionText()` —— `src/core/chart/transform/ChartNormalization.h:36,41` |
| 选项结构体 | `ChartNormalizationOptions` —— `:10-13` |
| 结果结构体 | `ChartNormalizationResult`（`ok`, `text`, `errorMessage`, `changedCount`, `measureLineCount`） |
| 菜单入口 | `MainWindow::DocumentSection::onNormalizeWholeChart()` —— `src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp:311+` |
| 批量 transform 工具 | `src/tools/chart_transform/ChartBatchTransformSpec.cpp` |
| 偏好设置 | `chartNormalizeStartAtNewMeasurePreferenceKey`、`chartNormalizeReduceTo384GridPreferenceKey` |

### 策略选项

| 选项 | 默认 | 效果 |
|---|---|---|
| `startAtNewMeasure` | true | 把选区起点视为小节边界；重写之前的部分小节上下文 |
| `reduceTo384Grid` | true | 把非 384 的细分量化到最近的 384 网格 |

对话框（`MainWindow.DocumentTransforms.cpp:72-86`）显示两个 checkbox：

- "选区起点视作小节线开始 / Treat selection start as measure boundary"
- "统一近似至384分音 / Snap approximately to 384 grid"

选择项通过编辑器偏好 JSON 跨会话持久化。

### 算法特性

- **无 parser 的 token 级 transform。** 作用于谱面文本而不是 parse 出的
  AST，从而保留注释、空白、谱面作者的书写风格仍然附着在正确的 token 上。
- **每行一小节。** 输出把每个小节重新格式化到独立行，前缀 `{N}` 细分声明。
- **time-signature 感知。** 读 `SimaiTimingMetadata` 获取整谱 time-signature；
  尊重 inline `|| x/y` overrides，它们会截断当前小节并重启 grid 基准。
- **BPM 触发 grid 重启。** 一行 `(BPM)` 在那一点重启细分基准。
- **Modifier 顺序典范化。** 单个 note 内部的 modifier 重排为典范的
  `b x h f`，依据
  `SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md` 的决定。

### 错误 / 中止行为

如果输入文本无法重新 tokenize（例如一个跨多个小节的未终止括号组），
`normalizeChartText()` 返回 `{ ok: false, errorMessage: "..." }`，编辑器
保持文档不动。绝不会提交部分重写。

### 已有详细规格

`docs/SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md` 承载完整的设计依据：
10 条最终决策、9 项架构发现、前置条件、输出规则、空行策略、modifier 顺序
规范、5 阶段实现拆分。

---

## 4. 横切策略

### 各子系统的读取来源

```
                     ┌────────────────────────┐
                     │  编辑器文本（实时）     │
                     └───────────┬────────────┘
                                 │
                ┌────────────────┼────────────────────┐
                │                │                    │
        ┌───────▼─────┐  ┌───────▼──────┐    ┌────────▼────────┐
        │ 语法 strict │  │ Lenient      │    │ Token-级重写    │
        │ pass        │  │ parser →     │    │ （仅 Modify     │
        │ → 列表      │  │ markers →    │    │  菜单触发）     │
        │             │  │ Muri 分析    │    │                 │
        │             │  │ → 列表       │    │                 │
        └─────────────┘  └──────────────┘    └─────────────────┘
                │                │                    │
                ▼                ▼                    ▼
        语法检查 tab      无理检查 tab           以单个 undo block
        + extra-sel       + 跳转 marker          写回编辑器
```

### Issue-key 约定

语法和无理两个列表都给每种 issue 类型一个稳定的字符串 `key`，让用户可以：
1. 通过 header 行 `||#ignore <key>` 屏蔽某一类 issue。
2. 点击面板行让编辑器光标跳转到 `(line, col)`。

### 阈值 / 配置触点

如果将来在三个子系统中任何一个新增了一种 kind，必须同步更新以下各处：

| 层 | 要新增什么 |
|---|---|
| 检测枚举 | 新的 `MuriKind` / 新的 validation 规则分支 |
| 显示字符串 | `displayMessage` 本地化（zh + en） |
| Issue-key | 一个唯一稳定的字符串供 `||#ignore` 使用 |
| 规格文档 | 追加到 `MURI_DETECTION_SPEC.md`、本索引、或者一份新的 SPEC（如果它本身是新子系统） |
| 测试清单 | 在 `MURI_DETECTION_TEST_CHECKLIST.md` 加一行覆盖 |

### 本索引文档**不**覆盖什么

- simai parser 内部 —— 见
  `src/core/chart/parser/SimaiNativeParser.h` 和按特性拆开的 `.cpp` 实现
  级文档。
- 无理检测的 alert-level 规则 —— 见 `MURI_DETECTION_SPEC.md`。
- 规范化的设计依据 —— 见
  `SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md`。

本文档是导航索引。详细规格才是唯一真相。
