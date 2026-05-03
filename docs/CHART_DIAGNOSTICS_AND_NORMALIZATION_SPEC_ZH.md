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

仅 strict 模式触发的检查（已逐项对照源码核对）。strict pass 的输出就是
最终展示给用户的严重度 —— 不再有合并步骤，所以下表 Severity 列即 UI
显示的级别。

| # | 仅 strict 的检查 | 严重度 | 来源 | 例子 |
|---|---|---|---|---|
| 1 | `{N}` 必须整除 384 | Warning | `Driver.cpp:557-565` `(384 % beats) != 0`（仅当 `beats <= 384`） | `{7}` —— 7 ∤ 384，能 parse 但被标注 |
| 2 | `{N}` 数值 clamp 警告 | Warning | `Driver.cpp:550-564` `formatBeatValueClamped(beats)`（仅当 `beats > 384`） | `{1024}` 被 clamp，emit 警告 |
| 3 | 未闭合 `[HS*…>` 块 | Error | `Driver.cpp:582-587` `kUnterminatedHsBlock()` | `[HS*x` 没有闭合 `>`。**lenient 模式直接 silent break**，不报错。 |
| 4 | 重复 `/` 分隔符 | Error | `Driver.cpp:593-597` `kRepeatedSlashSeparator()` | `1//5` |
| 5 | 重复 `` ` `` 分隔符 | Error | `Driver.cpp:604-611` `kRepeatedBacktickSeparator()` | `` 1``5 `` |
| 6 | Touch_hold 修饰符位置非典范 | Error | `TouchTap.cpp:59-66` `kNonCanonicalHoldModifierPlacementPrefix` | `Ah[4:1]b` —— touch 同时有 `[duration]` 和 `]` 之后的非空后缀修饰符 |
| 7 | Tap/hold 修饰符位置非典范 | Error | `TouchTap.cpp:157-164` `kNonCanonicalHoldModifierPlacementPrefix` | `1h[4:1]x` —— hold 同时有 `[duration]` 和 `]` 之后的非空后缀修饰符 |
| 8 | Break-slide `b` 修饰符位置无效 | Error | `Slide.cpp:69-77` `kInvalidBreakSlideModifierPositionPrefix` | `b` 不是紧挨在 slide token 第一个 `[` 之前 |
| 9 | Slide 时值块位置无效 | Error | `Slide.cpp:113-115` `kInvalidSlideDurationPlacementPrefix` | Slide token 包含多于一个 `[…]` 块 |
| 10 | 多段 slide 带每段 wait+duration | Error（通过 parse 失败 → `classifyInvalidNoteMessage`） | `SimaiNativeParser.cpp:1206` strict 时 `parseStandardSlideChain` 返回 `false` | 多形 chain 每形都带 `[wait#duration]`，strict 拒绝；lenient 按形状长度比例分配总时值 |
| 11 | 缺失 beat 分隔符 `,` | Error | `StrictChecks.cpp:119-123` `runStrictFormatChecks` | 含有 note 字符且非终止 `E` 的行，整行没有 `,` |
| 12 | 不匹配的右括号 | Error | `StrictChecks.cpp:131-137`（在 `runStrictFormatChecks` 中） | `]` 没有对应的 `[`；`}` 没有 `{`；`)` 没有 `(` |
| 13 | 未闭合的左括号 | Error | `StrictChecks.cpp:142-145`（在 `runStrictFormatChecks` 中） | `[` / `{` / `(` 在文件结尾仍残留在 stack 中 |

`runStrictFormatChecks(state, lines)` 在 `Driver.cpp:652` 即主 parse loop
之**后**才跑。它会先剥掉控制块 `( … )`、`{ … }`、`<HS* … >`、以及整行
`||`-前缀注释（`StrictChecks.cpp:101-115`），然后再做检查 #11-#13；
所以注释掉的内容**不会**触发这三项 strict-only 检查。

**仅 lenient 的行为**（strict 不会做的宽松处理）：

| Lenient 宽松处理 | 来源 | 行为 |
|---|---|---|
| Touch `C1` / `C2` → `C` 归一化 | `TouchTap.cpp:13-18` | Lenient 在校验前静默把 `C1`/`C2` 改写为 `C`。Strict 跳过这一步，token 原样进入 `parseTouchSuffix`，几乎一定产生 `Invalid touch token` 错误。 |
| 多段 slide chain 带每段 wait+duration | `SimaiNativeParser.cpp:1209-1213` | Lenient 按形状长度比例把总时值分配到各段；strict 拒绝（即上表 #10）。 |
| 未闭合 `[HS*` 块 | `Driver.cpp:582-587` | Lenient 直接 silent break；strict 报上表 #3。 |

### 修饰符字母顺序 —— 明确**不是** strict 检查

Parser **不强制 `b / x / h / f` 的典范字母顺序**。
`parseTapModifierSequence`（`SimaiNativeParser.cpp:90+`）按字符迭代，
**仅在出现重复时拒绝**。也就是说 `1bx`、`1xb`、`1xh`、`1hx`、`1bhxf`、
`1fxhb` parse 出来都一样。

中文显示串「Hold 修饰符顺序无效」历史上读起来像「顺序错」，但底层
英文 `Invalid hold modifier sequence: <token>` 实际上对应两种条件，
**两种都与模式无关（lenient 和 strict 都报）**：

| 条件 | 来源 | 例子 |
|---|---|---|
| 前缀或后缀里出现重复修饰符 | `parseTapModifierSequence` 返回 `false` → `TouchTap.cpp:126` | `1bb`、`1xx`、`1hh` |
| `]` 之后的后缀里出现 `h` 修饰符 | `TouchTap.cpp:119-122` | `1[4:1]h`、`1x[4:1]h` |

仅 strict 的 Warning `kNonCanonicalHoldModifierPlacementPrefix`（即上表
#6 / #7）是**另一回事**：它谈的是修饰符相对 `[duration]` 块的*位置*，
不是修饰符*顺序*。仅当 `[duration]` 块存在 AND `]` 之后还有非空后缀
修饰符时才触发。

### 1.2 验证报告 —— `buildValidationReport()`

`buildValidationReport()`（`SimaiNativeParser.Driver.cpp:971-1042`）
产出「语法检查」tab 消费的报告。输出类型：

```cpp
struct SimaiNativeValidationReport {
    bool ok;
    int errorCount;
    int warningCount;
    int lenientNoteCount;       // 历史字段；新流水线下恒为 0
    int lenientErrorCount;      // 历史字段；新流水线下恒为 0
    int strictNoteCount;
    int strictErrorCount;
    QVector<SimaiNativeValidationIssue> issues;
};
```

构建逻辑现在是「仅 strict」：

1. **空谱面捷径。** `text.trimmed().isEmpty()` 时直接产出一条
   `kChartEmpty()` 原始 message 的 `Error` issue 并返回。
2. **strict pass。** 调用 `validateSyntax()`（即
   `parseInternal(strictMode = true)`）。
3. **issue 直通。** 每条 strict *错误* 变成 UI `Error` issue，每条
   strict *警告* 变成 UI `Warning` issue。**不再合并、不再降级、
   不再有例外列表** —— strict pass 的严重度即最终严重度。
4. `report.ok = (report.errorCount == 0)`。

#### 与 lenient pass 解耦

构建验证报告时**不再咨询** lenient pass（`parseForTimeline`）。
Lenient 的存在意义现在只剩一件事：抽取一组可用的 timeline marker
集合，给谱面预览用。语法诊断完全不看它。

这种解耦也体现在报告结构体里：

- `lenientNoteCount` / `lenientErrorCount` 仍保留（原 `MainWindow::ValidationEntry`
  下游消费方引用这两个字段，是 ABI 兼容性需要），但永远填 `0`，已不再
  携带含义。
- `buildValidationReport()` 签名仍保留 `lenientResult` 形参，是为了源码
  级兼容那些缓存了 lenient 结果的调用点（`ValidationRuntime.cpp` 会传入
  缓存好的 lenient 结果，避免重复 parse）。实现里被标记为 `Q_UNUSED`。
- 之前文档中提到的 `shouldRemainValidationError()` 例外列表与
  `makeValidationMessageKey()` 辅助函数已被**直接删除** —— 它们只在
  服务于合并步骤的语境下才有意义。

实际效果：

- 「编辑器在 timeline 上画什么？」 → 仅由 lenient pass 回答。
- 「这是不是典范的 simai？」 → 仅由 strict pass 回答。
- 两个问题之间互不干涉。

#### 严重度示例（解耦后）

「Lenient 原始」列已删除，因为它不再参与。strict 严重度即 UI 严重度。

| 输入 | UI 严重度 | strict pass 来源 |
|---|---|---|
| `1[4:1`（未闭合 `[`） | **Error** | `runStrictFormatChecks` 的 `kUnclosedBracketPrefix` |
| `]`（不匹配右括号） | **Error** | `runStrictFormatChecks` 的 `kUnmatchedClosingBracketPrefix` |
| `1//5`（重复 `/`） | **Error** | `kRepeatedSlashSeparator` |
| `` 1``5 ``（重复 `` ` ``） | **Error** | `kRepeatedBacktickSeparator` |
| `1bb`（重复 `b` 修饰符） | **Error** | `parseTapModifierSequence` 返回 `false`（与模式无关） |
| `1[4:1]h`（`h` 在 `]` 之后） | **Error** | `TouchTap.cpp:119-122`（与模式无关） |
| `1h[4:1]x`（tap/hold 修饰符位置非典范） | **Error** *（原 Warning，已翻牌）* | `kNonCanonicalHoldModifierPlacementPrefix`（`TouchTap.cpp:157-164`） |
| `Ah[4:1]b`（touch_hold 修饰符位置非典范） | **Error** *（原 Warning，已翻牌）* | `kNonCanonicalHoldModifierPlacementPrefix`（`TouchTap.cpp:59-66`） |
| `{7}1,2,3,`（7 ∤ 384） | **Warning** *（原 Error，已翻牌）* | `formatStrictBeatValue`（`Driver.cpp:557-565`） |
| `{1024}1,2,…`（beats 被 clamp） | **Warning** | `formatBeatValueClamped` |
| `[HS*xyz`（无闭合 `>`） | **Error** | `kUnterminatedHsBlock` |
| `1abc,2,3,`（note 行某处缺逗号） | **Error** | `runStrictFormatChecks` 的 `Missing beat separator ','` |
| `1xh` / `1hx` / `1bxhf` / `1fxhb` | **不报** | `parseTapModifierSequence` 不关心顺序 |
| Touch `C1` | **Error** | strict 跳过 lenient 的 `C1`/`C2` → `C` 改写，token 原样进入 `parseTouchSuffix`，被拒（`Invalid touch token: C1`）。lenient 流水线仍把它渲染为预览 timeline 上的 `C` touch —— 两条流水线在这里默默不同，是设计如此。 |

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
