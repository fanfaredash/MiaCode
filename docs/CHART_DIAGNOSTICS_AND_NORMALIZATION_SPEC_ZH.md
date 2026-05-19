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
| 6 | Touch_hold 修饰符位置非典范 | Warning | `TouchTap.cpp:71-80` `kNonCanonicalHoldModifierPlacementPrefix` | `Ah[4:1]b` —— touch 同时有 `[duration]` 和 `]` 之后的非空后缀修饰符 |
| 7 | Tap/hold 修饰符位置非典范 | Warning | `TouchTap.cpp:169-178` `kNonCanonicalHoldModifierPlacementPrefix` | `1h[4:1]x` —— hold 同时有 `[duration]` 和 `]` 之后的非空后缀修饰符 |
| 8 | Break-slide `b` 修饰符位置非典范 | Warning | `Slide.cpp:60-82` `kInvalidBreakSlideModifierPositionPrefix` | `b` 不是紧挨在 slide token 第一个 `[` 之前。Slide 仍然能 parse，`trackBreak` 被置位；`b` 字符在构建 `sanitizedCore` 时被剥离。 |
| 9 | Slide 时值块位置无效 | Error | `Slide.cpp:113-115` `kInvalidSlideDurationPlacementPrefix` | Slide token 包含多于一个 `[…]` 块 |
| 10 | 多段 slide 带每段 wait+duration | Error（通过 parse 失败 → `classifyInvalidNoteMessage`） | `SimaiNativeParser.cpp:1206` strict 时 `parseStandardSlideChain` 返回 `false` | 多形 chain 每形都带 `[wait#duration]`，strict 拒绝；lenient 按形状长度比例分配总时值 |
| 11 | 缺失 beat 分隔符 `,` | Error | `StrictChecks.cpp:119-123` `runStrictFormatChecks` | 含有 note 字符且非终止 `E` 的行，整行没有 `,` |
| 12 | 不匹配的右括号 | Error | `StrictChecks.cpp:131-137`（在 `runStrictFormatChecks` 中） | `]` 没有对应的 `[`；`}` 没有 `{`；`)` 没有 `(` |
| 13 | 未闭合的左括号 | Error | `StrictChecks.cpp:142-145`（在 `runStrictFormatChecks` 中） | `[` / `{` / `(` 在文件结尾仍残留在 stack 中 |
| 14 | 非典范的中心 Touch 音符（`C1`/`C2`） | Warning | `TouchTap.cpp:12-31` `kNonCanonicalCenterTouchPrefix` | `C1` —— lenient 与 strict 都把它归一化为 `C`，strict 额外标注非典范写法。 |
| 15 | Slide head 修饰符位置错误（`?` / `!` / `@`） | Error | `Driver.cpp` `detectMisplacedSlideHeadModifierMessage` / `kMisplacedSlideHeadModifierPrefix` | 这三个字符是仅 slide 才有的 head 修饰符。strict 要求每个 `?`/`!`/`@` 必须落在 token 中的 `[1, firstShapeIdx)` 范围内（`firstShapeIdx` 是 token 中第一个 slide shape 字符的下标，shape 字符集为 `-^v<>VpqszwW`）。它们与 `b`/`x` 的相对顺序没有限制。例如 `1!`、`1h@`、`1-5![8:1]`、`1-?5[8:1]` 都会被拒绝；`1!-5[8:1]`、`1x?-5[8:1]`、`1@bx-5[8:1]` 都通过。 |
| 16 | Tap-star 修饰符位置错误（`$` / `$$`） | Error | `Driver.cpp` `detectMisplacedTapStarModifierMessage` / `kMisplacedTapStarModifierPrefix` | `$` 是仅 tap 才有的修饰符（设置 `tapUsesStarMaterial`，`$$` 额外置 `tapStarDouble`）。strict 拒绝 `$` 出现在任何非纯 tap 的 token 中 —— 即非数字开头、含 slide shape 字符、含 `[…]`、或含 `h`（不区分大小写）的任意 token。与 `b`/`x` 的任意顺序组合都允许：`1$`、`1$$`、`1b$`、`1$x`、`1$bx`、`1bx$$` 都通过；`A1$`、`1-5$[8:1]`、`1h$`、`1$h[1:1]` 都拒绝。 |
| 17 | Slide 段链语法无效 | Error | `SimaiNativeParser.cpp` `isValidSlideChainStrict` / `kInvalidSlideChainPrefix`，由 `Slide.cpp::parseSlideToken` 调用 | 在 `*` 拆分 + head 还原 + 剥掉 `b` 之后，逐字符走过 slide core，确保每个字符都被一个合法构造消费。**Lane 数字。** head 必须是 1-8；每个 shape 的尾随数字也必须是 1-8。**Shape 格式。** `V` 必须紧跟两个 1-8 数字；`p` / `q` 后可跟一个数字（单环），或再跟一个相同字符再跟一个数字（双环 `pp` / `qq`）；`-^v<>szw` 中每个字符后必须恰好跟一个 1-8 数字。**无 orphan 字符。** 任何不是 shape / 不是 `[`、且未被任何 shape 的尾随数字槽消费的字符，都会被拒绝 —— 这正是用来抓 lenient chain parser 的 silent-skip：例如 `1v35-7[8:1]` 中的 `5` 会被 lenient 默默丢掉，parse 成 `[1v3, 3-7]`。**至少一个 shape。** 整段只有 bracket 的 token（如 `1[8:1]`）会被拒绝（不过这种 token 在上游 dispatch 时就走 hold 分支了，不会进到本检查）。**Chain 自动连接** —— 每个 shape 的 source 就是上一段的 destination，只要数字位数正确，chain 自然连得起来。**几何合法性**（start, V, via, end 是否真是合法的 V 组合）_不_ 在这里检查 —— 那是 `Slide.cpp:147` 已有的 slide-data 查表负责的，两种模式下都会对未知 shape key 报错（如 `1V32` 今天就已经会被拒绝）。 |

`runStrictFormatChecks(state, lines)` 在 `Driver.cpp:652` 即主 parse loop
之**后**才跑。它会先剥掉控制块 `( … )`、`{ … }`、`<HS* … >`、以及整行
`||`-前缀注释（`StrictChecks.cpp:101-115`），然后再做检查 #11-#13；
所以注释掉的内容**不会**触发这三项 strict-only 检查。

**仅 lenient 的行为**（strict 不会做的宽松处理）：

| Lenient 宽松处理 | 来源 | 行为 |
|---|---|---|
| 多段 slide chain 带每段 wait+duration | `SimaiNativeParser.cpp:1209-1213` | Lenient 按形状长度比例把总时值分配到各段；strict 拒绝（即上表 #10）。 |
| 未闭合 `[HS*` 块 | `Driver.cpp:582-587` | Lenient 直接 silent break；strict 报上表 #3。 |

> **关于 `C1`/`C2` 的说明。** 这一项以前是仅 lenient 的宽松处理（lenient
> 改写、strict 不改写）。现在两种 pass 都做归一化，所以解析出来的 marker
> 一致，仅 strict 额外发一条 Warning（即上表 #14）。两条流水线不再对
> 「这个 token 是否合法」有分歧。

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
| `1h[4:1]x`（tap/hold 修饰符位置非典范） | **Warning** | `kNonCanonicalHoldModifierPlacementPrefix`（`TouchTap.cpp:169-178`） |
| `Ah[4:1]b`（touch_hold 修饰符位置非典范） | **Warning** | `kNonCanonicalHoldModifierPlacementPrefix`（`TouchTap.cpp:71-80`） |
| `2bv-3[4:1]`（break-slide `b` 不紧挨 `[`） | **Warning** *（原 Error，已翻牌）* | `kInvalidBreakSlideModifierPositionPrefix`（`Slide.cpp:60-82`）。Slide 仍能 parse，`trackBreak` 被置位。 |
| `{7}1,2,3,`（7 ∤ 384） | **Warning** *（原 Error，已翻牌）* | `formatStrictBeatValue`（`Driver.cpp:557-565`） |
| `{1024}1,2,…`（beats 被 clamp） | **Warning** | `formatBeatValueClamped` |
| `[HS*xyz`（无闭合 `>`） | **Error** | `kUnterminatedHsBlock` |
| `1abc,2,3,`（note 行某处缺逗号） | **Error** | `runStrictFormatChecks` 的 `Missing beat separator ','` |
| `1xh` / `1hx` / `1bxhf` / `1fxhb` | **不报** | `parseTapModifierSequence` 不关心顺序 |
| Touch `C1` | **Warning** *（原 Error，已翻牌）* | `kNonCanonicalCenterTouchPrefix`（`TouchTap.cpp:12-31`）。lenient 与 strict 都把 `C1`/`C2` 归一化为 `C`，strict 额外标注非典范写法。两条流水线现在对解析出的 marker 一致。 |
| `1!` / `1h@`（仅 slide 才用的修饰符出现在非 slide token） | **Error** | `kMisplacedSlideHeadModifierPrefix`（`Driver.cpp` `detectMisplacedSlideHeadModifierMessage`） —— token 中没有 slide shape 字符。 |
| `1-5![8:1]` / `1-?5[8:1]`（仅 slide 才用的修饰符出现在 slide body） | **Error** | `kMisplacedSlideHeadModifierPrefix` —— `?`/`!`/`@` 落在第一个 slide shape 字符之后。 |
| `1!-5[8:1]` / `1x?-5[8:1]` / `1@bx-5[8:1]` | **不报** | 三种字符都落在 lane digit 与第一个 shape 字符之间，与 `b`/`x` 的顺序不受限。 |
| `A1$` / `1-5$[8:1]` / `1h$` / `1$h[1:1]`（仅 tap 才用的修饰符出现在非 tap） | **Error** | `kMisplacedTapStarModifierPrefix`（`Driver.cpp` `detectMisplacedTapStarModifierMessage`） —— token 分别是 touch / slide / hold / 带 `[…]` 的非 tap。 |
| `1$` / `1$$` / `1b$` / `1$x` / `1$bx` / `1bx$$` | **不报** | Token 是纯 tap；`$` / `$$` 与 `b` / `x` 任意顺序组合都可以。 |
| `1V35-7[8:1]`（V chain 接 `-`） | **不报** | `V35` 是两个数字（3、5），`-7` 是一个数字，chain 5→5 衔接。两个 shape 都能在 slide data 中查到。 |
| `1v35-7[8:1]`（小写 `v` 跟两个数字） | **Error** | `kInvalidSlideChainPrefix` —— 小写 `v` 后面只跟一个数字，所以 `1v3` 之后的 `5` 是 orphan。lenient 默默丢掉它，parse 成 `[1v3, 3-7]`。 |
| `1V32-7[8:1]`（V 组合无效） | **Error** | `Slide.cpp:147` `Invalid note: … unknown shape 1V32` —— 由已有的 slide-data 查表抓住，不是新的 strict 语法检查（后者只校验数字位数）。两种模式下都会报错；lenient 行为未变。 |
| `1-5p6[8:1]`（多 shape chain `-` + `p`） | **不报** | 两个 shape 各跟一个数字，chain 5→5 衔接。 |
| `1-5-p6[8:1]`（chain 中 orphan） | **Error** | `kInvalidSlideChainPrefix` —— 第二个 `-` 的尾随槽是 `p`，不是 1-8 数字；尾部的 `6` 同样是 orphan。 |
| `1-5[8:1]*-6[8:1]`（同 head `*` 拆分） | **不报** | `*` 拆分后两个分支是 `1-5[8:1]` 与 `1-6[8:1]`（head `1` 继承），都合法。 |
| `1-5[8:1]*-p6[8:1]`（第二个分支无效） | **Error** 出现在第二个分支 | 第一个分支合法；第二个分支重建为 `1-p6[8:1]`，被 strict chain 检查拒绝（`-` 后跟 `p`，不是数字）。 |
| `1-5[8:1]*-5p6[8:1]`（第二个分支是多 shape） | **不报** | 第二个分支重建为 `1-5p6[8:1]`，是合法的两段 chain。 |

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

> **审查状态。** 本节是从索引 / 设计意图层面描述该子系统。与 §1（语法
> 检测）不同，本次修订**没有**在代码层面逐行核对实现；这里描述的规则与
> 示例可能与当前 `MuriAnalyzer` / `MuriStaticChecker` 的实际行为存在
> 偏差。**仍需对代码做人工核对**，才能把本节当作权威参考。本子系统的
> 权威参考是 `docs/MURI_DETECTION_SPEC.md`，以及下方入口表中引用的
> 源文件。

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
通过单个 undoable 编辑块完成（`editCursor.beginEditBlock()` /
`endEditBlock()`，`MainWindow.DocumentTransforms.cpp:393-398`）。两个独立
checkbox 选项控制行为，跨会话持久化在编辑器偏好里。

### 入口

| 表面 | 文件 / 符号 |
|---|---|
| 公共 API（全文） | `normalizeChartText()` —— `src/core/chart/transform/ChartNormalization.h:36` |
| 公共 API（选区） | `normalizeChartSelectionText()` —— `:41` |
| 内部主流程 | `normalizeChartFragment()` —— `src/core/chart/transform/ChartNormalization.cpp:1190+` |
| 选项结构体 | `ChartNormalizationOptions` —— `:10-13` |
| 结果结构体 | `ChartNormalizationResult { ok, text, errorMessage, changedCount, measureLineCount }` —— `:15-21` |
| 菜单入口 | `MainWindow::DocumentSection::onNormalizeWholeChart()` —— `src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp:291` |
| 内联回归测试 | `runInlineSpecs()` —— `src/tools/chart_transform/ChartBatchTransformSpec.cpp:736+` |
| 偏好键名 | `kChartNormalizeStartAtNewMeasurePreferenceKey`、`kChartNormalizeReduceTo384GridPreferenceKey` —— `ChartNormalization.h:23-26` |

### 3.1 输入与门控

`normalizeChartFragment` 的第一步是
`SimaiNativeParser::buildValidationReport(input, English, nullptr, timingMetadata)`
（`ChartNormalization.cpp:1200-1208`）。按 §1.2 的拆解，`buildValidationReport`
现在是**仅 strict** —— 因此：

- 任何 strict pass 抓到的 **Error** 都会让 normalization 立刻返回
  `{ ok: false, errorMessage: <第一条 issue 的本地化消息> }`，编辑器保持
  文档不动。
- strict pass 抓到的 **Warning**（如 `{7}` 非 384 因数、touch_hold 修饰符
  位置非典范、`b` 不紧贴 slide `[` 等，详见 §1.1 表）**全部放行**。

> **关于 §0 标签的修正。** §0 表把规范化标为 "lenient"，但代码实际通过
> `buildValidationReport`（strict）做门控。具体后果：strict-only 的 Error
> —— `1//5` 重复分隔符、`[HS*xyz` 未闭合 HS 块、缺逗号、misplaced slide
> head modifier、misplaced tap-star modifier、invalid slide chain
> —— 也会阻挡 normalization，即便 lenient pass 能跳过坏段继续抽 marker。

### 3.2 两个选项的实际语义

对话框（`MainWindow.DocumentTransforms.cpp:73-87`）显示两个 checkbox，但
作用层级很不一样。

#### `startAtNewMeasure`（默认 true）

| 调用路径 | 实际影响 |
|---|---|
| `normalizeChartText`（全文 / 无选区） | **实质 no-op**。seed 来自 `seedFromTimingMetadata`，`startPhaseWhole` 总是 0；不管选项真假，第一个 measure 都从 phase 0 开始。 |
| `normalizeChartSelectionText`（有选区） | true：把选区起点视为小节边界，丢弃 `scanNormalizationSeed` 算出的相位偏移；如果原本相位非零，输出**会在最前面注入一行 `\|\| <meter>`**（`ChartNormalization.cpp:1561-1574`）。false：保留原相位，输出延续选区前一刻的 measure。 |

回归测试 `ChartBatchTransformSpec.cpp:1342-1361` 是 mid-measure 选区 +
`startAtNewMeasure=true` 注入 `\|\| 4/4` 的实证。

#### `reduceTo384Grid`（默认 true）

这个选项控制**对非 384 网格的 moment 如何处理**。`renderMeasureLine`
（`ChartNormalization.cpp:1061+`）是 dispatcher，按 measure 分流：

| 值 | 行为 |
|---|---|
| true | 所有 measure **强制走** `renderMeasureLineApproximate`（`ChartNormalization.cpp:839-917`）。候选 subdivision 集 = 16, 24, 32, 48, 64, 96, 128, 192, 384（即 ≥16 的 384 因数）。逐个 beat-segment 挑能在 `kSnapToleranceWhole ≈ 1/768` 内容纳所有 moment 的**最小** candidate。`{16}` 谱在正常情况下输出仍是 `{16}`，**不会被推到** `{384}`。**非 384 分母的 moment（如 1/7、1/9）会被 round 到 384 grid，丢失精度**（如 `{7}` 输入会被压成 `{384}` 并 snap）。 |
| false | **每个 measure 独立判定**：扫描该 measure 内所有 `MeasureMoment::positionWhole.denominator`，若**全部都是 384 约数**（即 `384 % denom == 0`）→ 走 approximate（与 reduce=true 相同行为）；若**任一 moment 落在非 384 约数位置**→ 整个 measure 走 `renderMeasureLineExact`（`ChartNormalization.cpp:996-1059`），用 moment 位置分母的 LCM 选 subdivision，能产生 `{7}`、`{9}`、`{15}` 等非-384 因数，**保留 moment 精度**。 |

关键含意：

- 普通谱面（每个 moment 都落在 384 grid 上）在 reduce=true / false 下
  **输出完全一致** —— 用户不会看到「reduce=false 突然把空白合并成 `{1}`
  大 chunk」的意外行为。
- 只有真正含 `{7}` / `{9}` / `{15}` 等异常分音的 measure，才会在
  reduce=false 下落入 exact 路径（保留精度），而在 reduce=true 下被
  强制 snap 到 384 grid（精度损失）。
- 对话框文案「统一近似至 384 分音」对应 reduce=true；reduce=false 表达
  「保留非 384 分音原位（其余照旧近似）」的语义。

#### Duration 时长字符串（`[beats:numerator]` 或 `[ms#beats:numerator]`）

时长由 `normalizePlainDurationSignature`（`ChartNormalization.cpp:330+`）处理。
**当且仅当以下三个条件同时满足时，时长字符串才会被改写**；否则原 signature
**逐字保留**，用户的书写选择完全不动：

1. 时长字符串**不含 `#`**（`parsePlainDurationSignature` 见 `#` 直接 return false）
2. `reduceTo384Grid=true`（"统一近似至 384 分音" 选项打开）
3. **原始 `beats` 值不是 384 的约数**（即 `384 % beats != 0`）

例：

| 输入时长 | 原 `beats` | 384 约数？ | reduce=true 输出 | reduce=false 输出 |
|---|---|---|---|---|
| `[8:1]` | 8 | 是 | `[8:1]` 不动 | `[8:1]` 不动 |
| `[24:6]` | 24 | 是 | `[24:6]` 不动 | `[24:6]` 不动 |
| `[48:5]` | 48 | 是 | `[48:5]` 不动 | `[48:5]` 不动 |
| `[1:3]` | 1 | 是 | `[1:3]` 不动 | `[1:3]` 不动 |
| `[7:1]` | 7 | 否 | 改写到 384 grid | `[7:1]` 不动 |
| `[500:1]` | 500 | 否 | `[384:1]`（units=1） | `[500:1]` 不动 |
| `[2000:1]` | 2000 | 否 | units=0 → 进零时长分支 | `[2000:1]` 不动 |
| `[120#24:3]` | (含 `#`) | n/a | `[120#24:3]` 不动 | `[120#24:3]` 不动 |

#### 触发改写时的零时长分支

当三个条件都成立、且改写后 `units = round(numerator/beats * 384) ≤ 0` 时，
策略由 token 类决定（`renderTokenForGrid` 传不同的
`DurationNormalizationOptions`）：

| Token 类 | `allowZeroDuration` | `omitZeroDurationBracket` | 含义 |
|---|---|---|---|
| Touch hold | true | false | 输出 `[1:0]`，bracket 保留（touch 的 `[]` 语法有可见性） |
| Note hold | true | true | **整段 bracket 删除**，token 留 `h` 字符（例：`2h[2000:1]` → `2h`） |
| Slide duration | false | false | Slide 必须有时长，floor 到 `units=1`，输出 `[384:1]` |

回归 `ChartBatchTransformSpec.cpp:1216-1228` 验证 `[2000:1]`（non-384）→
`2h` 与 `[1:3]`（384 约数）→ 不动；`:1230-1241` 验证 `[500:1]` →
`[384:1]`；`:1244-1255` 验证 `[192:1]` 已是 384 约数 → 不动。

### 3.3 触发小节切分的事件

主 parse 循环（`ChartNormalization.cpp:1334-1463`）按字符走，下面四种事件
会切小节，其余字符都只是更新 token 或推进 phase：

| 事件 | 行为 | 实现 |
|---|---|---|
| `(BPM)` 且数值 ≠ currentBpm | 关闭当前 measure → 开新 measure（同 meter）→ 把 `(BPM)` 作为 leading boundary 放在新 measure 顶端 | `:1387-1392` `restartMeasureAtCurrentPosition` |
| `(BPM)` 但数值 = currentBpm | **不切**，只挂在当前位置作 inline `StandaloneText` 注解 | `:1393-1396` `appendBoundaryItem` |
| 合法 `\|\| x/y` 内联 time-signature | 关闭当前 measure → 开新 measure（**新 meter**）→ 把 `\|\| <normalized>` 作为 leading boundary | `:1352-1363`，靠 `parseInlineTimeSignatureComment`（`SimaiTimingMetadata.cpp:70+`） |
| 其他 `\|\| ...` 注释（非 time-signature） | 关闭当前 measure → 开新 measure（**保留原 meter、保留相位进位**）→ 把注释原文作为 leading boundary | `:1364-1367` `splitMeasureAtCurrentPosition` |
| `{N}` subdivision 变化 | **不切小节**，只更新 `currentBeats` | `:1403-1417` |

`splitMeasureAtCurrentPosition` 与 `restartMeasureAtCurrentPosition` 的关键
差别：前者把 `currentMeasure.startPhaseWhole + currentPositionWhole` 作为
新 measure 的 startPhase（让后续 token 继续填满半个小节），后者把新
measure 的 startPhase 设回 0（小节从头开始）。

### 3.4 输出版面规则

| 规则 | 实现 |
|---|---|
| 每个 measure 一行，前缀 `{N}` 由渲染器从 moment 位置反推 | `renderMeasureLine` `:1061-1066` |
| 同一 moment 内多 token 用 `/` 串（合击） | `buildMomentText` `:729-747` |
| 同一 moment 内多 group 用 `` ` `` 串（连击） | 同上 |
| measure line 内 beat 边界用空格分隔 | approximate: `:910-912`；exact: `:1048-1050` |
| 每 emit 4 个 measure line 追加一个空行 | `:1483-1485` `(emittedMeasureLines % 4) == 0` |
| 末尾连续空行剪掉 | `:1487-1489` |
| `(BPM)` 与 `\|\| x/y` 相邻 boundary item 合并为单行 `(180) \|\| 4/4` | `appendBoundaryItems` `:785-826`；回归 `ChartBatchTransformSpec.cpp:1258-1268` |
| `normalizeChartText`（全文路径）在末尾追加单独的 `E` | `:1490-1492`，由 `appendTerminalMarker=true` 触发 |
| `normalizeChartSelectionText`（选区路径）**不**追加 `E` | 调用时传 `appendTerminalMarker=false`（`:1568-1574`） |

### 3.5 Token 规范化

`canonicalizeToken`（`:628-651`）依次尝试三种解析器；任意一个成功就用对应
build 函数重组并返回；**三个都失败则原文（仅 trim）输出**。

| Token 类 | 识别条件 | parse 接受的修饰符 | 重组顺序 | 备注 |
|---|---|---|---|---|
| Touch | 首字符 `C`（可带 `1`/`2`）或 `A`/`B`/`D`/`E` + lane 数字 | `b` `x` `h` `f` | prefix → `b` → `x` → `h` → `f` → bracketSuffix | bracket 与 `h` 互为充要：有 `[…]` 必须有 `h`，反之亦然 |
| Note | 首字符 1-8 lane 数字、不含 slide operator、不是纯数字串 | **仅** `b` `x` `h`；其余字符（包括 `f`、`?`、`!`、`@` 等）落入 `extraModifiers` | lane → `sortedModifierText(extraModifiers)` → `b` → `x` → `h` → bracketSuffix | `extraModifiers` 按 unicode codepoint 升序排列；bracket 与 `h` 互为充要 |
| Slide head | 首字符 1-8 lane 数字、含 slide operator `-^v<>Vpqszw` | head 区识别 `b` / `x` / `?` / `!` / `@`（直到第一个 shape 字符）；**`h` 在 head 出现 → 整 token reject** | lane → `sortedModifierText(headExtraModifiers)` → `b` → `x` → core 剩余（trackBreak `b` 插回第一个 `[` 之前） | `?` `!` `@` 同样按 unicode 升序：输出固定为 `!` < `?` < `@` |
| 都不匹配 | —— | —— | —— | `canonicalizeToken` 返回 `trimmed` 原文（silent passthrough） |

涉及典范顺序的两个细节，前一版 §3 没有讲清：

1. **「b x h f」严格只成立于 touch**。Note token 不识别 `f` —— `f` 会被
   当作 extra modifier 排到 `b` 之前；slide head 既不识别 `h` 也不识别
   `f`。
2. **「`?` `!` `@` 按 ASCII 排」是隐性规则**。研究文档 decision 7 第二行
   明文规定，代码通过 `sortedModifierText` 实现，但前一版 §3 完全没写。

### 3.6 `changedCount` / `measureLineCount` 语义

`:1497`：

```cpp
result.changedCount = result.text == input ? 0 : qMax(1, emittedMeasureLines);
result.measureLineCount = emittedMeasureLines;
```

- `changedCount` **不是**改动 token / atom 数，而是「未改动 → 0；改动 →
  emit 出的 measure 行数（至少 1）」。当 UI 想区分「等价输入 → 提示
  *Already normalized*」与「实际改动 → 提示 *N measure line(s)*」时（见
  `MainWindow.DocumentTransforms.cpp:384-389, 432-437`），这个字段是布尔
  开关 + 大致规模指示，**不应当作精确变更数**。
- `measureLineCount` 是 emit 的 measure 行数本身（用于 status bar 显示）。

### 3.7 错误 / 中止行为

`buildValidationReport.errorCount > 0` → 直接 return
`{ ok=false, errorMessage }`，错误文本取自报告中第一条 issue 的本地化消息
（`summarizeValidationError` `:1068-1075`）。`MainWindow.DocumentTransforms.cpp:369-381`
把它转成 `QMessageBox::Warning` 弹给用户后保留原文档；**绝不会提交部分
重写**。

选区路径的额外早退：选区范围越界（`selectionStart < 0`、`selectionEnd <
selectionStart`、`selectionEnd > fullText.size()`）→
`{ ok=false, errorMessage="Invalid selection range." }`
（`:1554-1559`）。

### 3.8 偏好持久化

`chartNormalizationOptionsFromPreferences` /
`saveChartNormalizationOptionsToPreferences`（`:1501-1526`）把
`startAtNewMeasure` 与 `reduceTo384Grid` 写入编辑器 preview JSON。
`onNormalizeWholeChart` 在用户切换 checkbox 时即时回写
`savePortableState()`（`MainWindow.DocumentTransforms.cpp:341-349`） ——
即便用户最终取消对话框，对 checkbox 的更改也已被保留。

### 3.9 已有详细规格

`docs/SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md` 是设计依据：10 条
最终决策、9 项架构发现、前置条件、输出规则、空行策略、modifier 顺序
规范、5 阶段实现拆分。该文档写于实现之前，部分决策已在代码中变形 ——
例如 decision 9 把 384-grid 描述为唯一基准 + fallback rounding，而当前
代码通过 `reduceTo384Grid=false` 选项开放了 exact 渲染路径；以本节
（§3）为准。

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
