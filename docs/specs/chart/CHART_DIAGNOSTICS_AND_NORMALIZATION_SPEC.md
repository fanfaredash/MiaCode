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
| 9 | Slide 时值块位置无效 | Error | `SimaiNativeParser.cpp` `classifySlideDurationPlacementStrict()` / `Slide.cpp::parseSlideToken` | `1-5[8:1]-1`、`1>5[8:1]<5` 报 Error；`1-5[8:1]-1[8:2]` 是 #10 的 Warning 例外 |
| 10 | 多段 slide 每段都带 wait+duration | Warning | `classifySlideDurationPlacementStrict()`；`parseStandardSlideChain()` 继续折叠分段时值 | `1-5[8:1]-1[8:2]` —— 分段时值部分支持/非典范，提示但不阻断 |
| 11 | 缺失 beat 分隔符 `,` | Error | `StrictChecks.cpp:119-123` `runStrictFormatChecks` | 含有 note 字符且非终止 `E` 的行，整行没有 `,` |
| 12 | 不匹配的右括号 | Error | `StrictChecks.cpp:131-137`（在 `runStrictFormatChecks` 中） | `]` 没有对应的 `[`；`}` 没有 `{`；`)` 没有 `(` |
| 13 | 未闭合的左括号 | Error | `StrictChecks.cpp:142-145`（在 `runStrictFormatChecks` 中） | `[` / `{` / `(` 在文件结尾仍残留在 stack 中 |
| 14 | 非典范的中心 Touch 音符（`C1`/`C2`） | Warning | `TouchTap.cpp:12-31` `kNonCanonicalCenterTouchPrefix` | `C1` —— lenient 与 strict 都把它归一化为 `C`，strict 额外标注非典范写法。 |
| 15 | Slide head 修饰符位置错误（`?` / `!` / `@`） | Error | `Driver.cpp` `detectMisplacedSlideHeadModifierMessage` / `kMisplacedSlideHeadModifierPrefix` | 这三个字符是仅 slide 才有的 head 修饰符。strict 要求每个 `?`/`!`/`@` 必须落在 token 中的 `[1, firstShapeIdx)` 范围内（`firstShapeIdx` 是 token 中第一个 slide shape 字符的下标，shape 字符集为 `-^v<>VpqszwW`）。它们与 `b`/`x` 的相对顺序没有限制。例如 `1!`、`1h@`、`1-5![8:1]`、`1-?5[8:1]` 都会被拒绝；`1!-5[8:1]`、`1x?-5[8:1]`、`1@bx-5[8:1]` 都通过。 |
| 16 | Tap-star 修饰符位置错误（`$` / `$$`） | Error | `Driver.cpp` `detectMisplacedTapStarModifierMessage` / `kMisplacedTapStarModifierPrefix` | `$` 是仅 tap 才有的修饰符（设置 `tapUsesStarMaterial`，`$$` 额外置 `tapStarDouble`）。strict 拒绝 `$` 出现在任何非纯 tap 的 token 中 —— 即非数字开头、含 slide shape 字符、含 `[…]`、或含 `h`（不区分大小写）的任意 token。与 `b`/`x` 的任意顺序组合都允许：`1$`、`1$$`、`1b$`、`1$x`、`1$bx`、`1bx$$` 都通过；`A1$`、`1-5$[8:1]`、`1h$`、`1$h[1:1]` 都拒绝。 |
| 17 | Slide 段链语法无效 | Error | `SimaiNativeParser.cpp` `isValidSlideChainStrict` / `kInvalidSlideChainPrefix`，由 `Slide.cpp::parseSlideToken` 调用 | 在 `*` 拆分 + head 还原 + 剥掉 `b` 之后，逐字符走过 slide core，确保每个字符都被一个合法构造消费。**Lane 数字。** head 必须是 1-8；每个 shape 的尾随数字也必须是 1-8。**Shape 格式。** `V` 必须紧跟两个 1-8 数字；`p` / `q` 后可跟一个数字（单环），或再跟一个相同字符再跟一个数字（双环 `pp` / `qq`）；`-^v<>szw` 中每个字符后必须恰好跟一个 1-8 数字。**无 orphan 字符。** 任何不是 shape / 不是 `[`、且未被任何 shape 的尾随数字槽消费的字符，都会被拒绝 —— 这正是用来抓 lenient chain parser 的 silent-skip：例如 `1v35-7[8:1]` 中的 `5` 会被 lenient 默默丢掉，parse 成 `[1v3, 3-7]`。**至少一个 shape。** 整段只有 bracket 的 token（如 `1[8:1]`）会被拒绝（不过这种 token 在上游 dispatch 时就走 hold 分支了，不会进到本检查）。**Chain 自动连接** —— 每个 shape 的 source 就是上一段的 destination，只要数字位数正确，chain 自然连得起来。**几何合法性**（start, V, via, end 是否真是合法的 V 组合）_不_ 在这里检查 —— 那是 `Slide.cpp:147` 已有的 slide-data 查表负责的，两种模式下都会对未知 shape key 报错（如 `1V32` 今天就已经会被拒绝）。 |
| 18 | 音符与指令之间缺少 `,` | Warning | `Driver.cpp` `warnDirectiveAfterNote` / `kMissingCommaBeforeDirectivePrefix` | `{N}` / `(BPM)` / `<HS*N>` 指令紧跟在音符之后、中间没有 `,` —— 通常是漏写了拍间分隔符。追踪状态跨越空白与换行（典型 repro：`{16} 1,1,1,1,1` ↵ `{16},,,` —— 第一行以裸音符结尾）。每个漏掉的 `,` 只报一次：`1{16}(120)` 这样的指令连串只在第一个指令处报警。只有 `,` 会复位该状态。 |
| 19 | `*` slide 分支携带 head 数字 | Error | `Slide.cpp::parseSlideToken` 的 `*` 拆分循环 / `kInvalidSlideStarBranchPrefix` | 同头 `*` 分支必须写成去掉头部的 slide：`5q2[4:1]*p8[4:1]` 是典范写法；`5q2[4:1]*5p8[4:1]` 与 `5q2[4:1]*4p8[4:1]` 都拒绝。`*` 后面的数字**不是**新的 head —— lenient 解析会把它替换为共享的 head lane（`*4p8[4:1]` 会被默默解析成 `5p8[4:1]`），所以 strict 必须把它标成错误。每个 token 只报一次；lenient 的替换行为保持不变，谱面仍可预览。 |
| 20 | 空 `*` slide 分支 | Error | `Slide.cpp::parseSlideToken` 的 `*` 拆分循环 / `kEmptySlideStarBranchPrefix` | `1-5[8:1]*,`、`1-5[8:1]**-6[8:1],` 都拒绝；每个 token 只报一次并覆盖整个 token。 |

#### Slide duration placement 审计与修复状态（2026-07-09）

多段（拼接）slide 的目标判定规则：

- **只有最后一段后面有时值：合法。** 例如 `1-5-1[8:3],`。
- **每一段后面都有时值：Warning。** MiaCode 能把分段时值折叠为总时值并继续 parse，但分段时值只算部分支持/非典范。例如 `1-5[8:1]-1[8:2],`。
- **其他情况：Error。** 包括非最后段带时值但不是每段都有时值，或时值后继续出现 shape 文本。例如 `1-5[8:1]-1,`、`1>5[8:1]<5,`。

本次修复后的收口点：

- **时值后的 shape-like 后缀不再被静默吞掉。** `classifySlideDurationPlacementStrict()` 在 strict 下把 `1>5[8:1]<5,`、`1>5[8:1]V35,`、`1-5[8:1]-1,`、`1w5[8:1]<5,` 等归为 #9 Error。
- **每段都带时值不再被多 `[` 粗检查升成 Error。** `1-5[8:1]-1[8:2],` 现在是 #10 Warning，仍继续抽取 marker。
- **空 `*` slide 分支不再被跳过。** strict 对任意空分支报 #20 Error；lenient 仍保留历史预览行为。
- **诊断范围覆盖整个 token。** `Invalid slide chain syntax`、`Invalid slide duration placement`、空 `*` 分支和携带 head 的 `*` 分支都会传入 `endCol = column + token.size() - 1`。

`runStrictFormatChecks(state, lines)` 在 `Driver.cpp:652` 即主 parse loop
之**后**才跑。它会先剥掉控制块 `( … )`、`{ … }`、`<HS* … >`、以及整行
`||`-前缀注释（`StrictChecks.cpp:101-115`），然后再做检查 #11-#13；
所以注释掉的内容**不会**触发这三项 strict-only 检查。

**兼容解析行为**（用于继续抽 marker，不等于语法放行；strict 可在同一 token 上追加诊断）：

| 兼容处理 | 来源 | 行为 |
|---|---|---|
| 多段 slide chain 带每段 wait+duration | `parseStandardSlideChain()` + `classifySlideDurationPlacementStrict()` | parser 会折叠分段时值为总时值并按形状长度重新分配；strict severity 是 Warning。 |
| 未闭合 `[HS*` 块 | `Driver.cpp:582-587` | Lenient 直接 silent break；strict 报上表 #3。 |

> **关于 `C1`/`C2` 的说明。** 这一项以前是仅 lenient 的宽松处理（lenient
> 改写、strict 不改写）。现在两种 pass 都做归一化，所以解析出来的 marker
> 一致，仅 strict 额外发一条 Warning（即上表 #14）。两条流水线不再对
> 「这个 token 是否合法」有分歧。

### 修饰符字母顺序 —— 明确**不是** strict 检查

Parser **不强制 `b / x / h / f` 的典范字母顺序**。
`parseTapModifierSequence`（`SimaiNativeParser.cpp:90+`）按字符迭代，
**仅在出现重复或大写 `B` / `X` 时拒绝**。也就是说
`1bx`、`1xb`、`1xh`、`1hx`、`1bhxf`、`1fxhb` parse 出来都一样，而
`2B` / `2X` 会被拒绝。

中文显示串「Hold 修饰符顺序无效」历史上读起来像「顺序错」，但底层
英文 `Invalid hold modifier sequence: <token>` 实际上对应以下几种条件，
**全部与模式无关（lenient 和 strict 都报）**：

| 条件 | 来源 | 例子 |
|---|---|---|
| 前缀或后缀里出现重复修饰符 | `parseTapModifierSequence` 返回 `false` → `TouchTap.cpp:126` | `1bb`、`1xx`、`1hh` |
| 大写 `B` / `X` 修饰符 | `containsUppercaseTapBreakOrExModifier` → `parseTapModifierSequence` 返回 `false` | `2B`、`2X` |
| `]` 之后的后缀里出现 `h` 修饰符 | `TouchTap.cpp:119-122` | `1[4:1]h`、`1x[4:1]h` |

另有两处与模式无关的收紧不在 `parseTapModifierSequence` 内：

- **Touch 音符拒绝 `x`。** `parseTouchSuffix` 不再接受 touch /
  touch-hold token 上的 `x`（touch EX 并不存在）；`B1x`、`Cx`、
  `A1fxh[4:1]` 都以 `kInvalidTouchModifierPrefix` 报错。
- **Slide head 拒绝大写 `B` / `X`。** `parseSlideHeadModifierPrefix`
  对它们返回 `false`，`2X-6[8:1]` / `2B-6[8:1]` 落入通用 invalid-note
  路径。`B2` / `B4` 仍是合法 touch 音符。

仅 strict 的 Warning `kNonCanonicalHoldModifierPlacementPrefix`（即上表
#6 / #7）是**另一回事**：它谈的是修饰符相对 `[duration]` 块的*位置*，
不是修饰符*顺序*。仅当 `[duration]` 块存在 AND `]` 之后还有非空后缀
修饰符时才触发。

### 大小写混淆风险审计（2026-07-09）

simai 谱面语法原则上应严格区分大小写。本次审计只记录**用户输入层面**
会被解析为相同功能的大小写兼容点；内部用于索引 / 显示的归一化（例如
touch pad key 转大写、marker type 转小写）不计入风险。

当前主 parser 中确认仍存在的大小写等价：

| 输入类别 | 当前等价输入 | 当前行为 | 主要来源 |
|---|---|---|---|
| Tap / hold 的 hold 修饰符 | `1h[4:1]` 与 `1H[4:1]` | 都解析成 hold，strict 不报错/不报警告 | `parseTapModifierSequence` 对修饰符 `toLower()`；只专门拒绝大写 `B` / `X` |
| Terminal marker | `E` 与 `e` | 独占行或行尾 terminal marker 都终止谱面；strict 不报错/不报警告 | `isTerminalMarkerText()` 用 `Qt::CaseInsensitive`，主循环也显式接受 `E` / `e` |

已核对但**未**形成大小写等价放行的点：

- `b/B`、`x/X`：主 parser 在 tap 和 slide head 上的大写 `B` / `X` 已被拒绝，
  见上文表格。
- `m/M`：地雷修饰符严格只接受小写 `m`。`1M`、`A1M`、`1-3[2:1]M`
  和 `1M-3[2:1]` 在 lenient 与 strict 中都会被拒绝。
- `w/W`：`Slide.cpp` 中有 `sanitizedCore.contains('w', Qt::CaseInsensitive)`，
  但实际 shape lookup 不接受大写 `W`；`1W5[8:1]` 当前报错。
- Touch 头 `A/B/C/D/E`：主 parser 的 `isTouchPrefix()` 仍大小写敏感；
  `c1` 当前不会被当作中心 touch。
- `v/V`：小写 `v` 与大写 `V` 是不同 slide shape，不是大小写 alias。

需要同步审计/收紧的镜像解析入口：

| 路径 | 当前大小写宽松点 | 风险 |
|---|---|---|
| `src/timeline/TimelineQuickModelParser.cpp` | 已同步拒绝 tap/touch/slide mine 的大写 `M`，也拒绝主 parser 已拒绝的 tap / slide-head 大写 `B` / `X`；`isTerminalMarkerText()` 仍接受 `E/e`，tap hold `H` 仍等价 | 剩余风险主要是 `1H[4:1]`、`e` 等还可能被快时间线当作有效对象；`1M` 不再渲染为地雷 |
| `src/core/chart/transform/ChartBatchTransform.Parsers.cpp` | 已同步不把 note / slide 的大写 `M` 当作 mine，也拒绝 tap / slide-head / touch 修饰符上的大写 `B` / `X`；`touchPrefixLength()` 仍对 touch 头 `toUpper()`，部分 `H/F` 仍会被折叠 | 选择变换不再把 `1M`、`A1M`、`1-3[2:1]M` 识别或重写成合法地雷；剩余风险是小写 touch 头与非地雷大写修饰符的兼容解析 |
| `src/core/chart/transform/ChartNormalization.cpp` | touch 头 `toUpper()`，touch / note / slide modifier 解析使用 `toLower()`，builder 输出典范小写修饰符 | 全文 normalization 入口当前会先过 strict validation，通常挡住非法大小写；但解析器本身仍有折叠逻辑，后续复用或选区路径调整时需要同步收紧 |
| `src/core/chart/transform/ChartBatchTransform.Subdivision.cpp` | terminal marker 边界判断用 `Qt::CaseInsensitive` | 分段/细分变换会把小写 `e` 当作终止符处理 |

目标收紧方向：如果把“simai 语法严格区分大小写”作为 strict 契约，
则剩余的 `H`、小写 terminal `e` 等当前等价输入应至少进入 strict 诊断。
主 parser 已拒绝的 `B` / `X` / `M` 要同步到快时间线和变换解析器，否则会出现
“语法检查报错，但预览/选区操作仍按合法 token 处理”的分歧；本轮已先收紧 `M`，并补齐 `B` / `X` 的主要镜像入口。

### 地雷修饰符 `m`（Mine note）

`m` 是 Majdata 扩展的**地雷**修饰符（maimai 官方没有）。MiaCode 是 autoplay
模拟器（无玩家输入），所以地雷被建模为「永远被完美躲过」的音符：**有独立贴图，
但不发任何 SFX、不出判定特效、被无理分析跳过**；按产品决策**计入物量**。

- **接受位置**：地雷修饰符严格只接受小写 `m`；大写 `M` 是非法输入，
  不会被当作地雷兼容解析：
  - tap / hold：`1m`、`1bm`（break+雷）、`1xm`（ex+雷）、`1hm[4:1]`（hold 雷）。
  - touch / touch-hold：`A1m`、`C2hm[4:1]`。
  - slide：`1-3[2:1]m`（`m` 在末尾）。`m` 只置位 `trackMine`，星星头保留原本材质；
    `m` 字符在构建 slide shape lookup key 时被剥离（同 `b`）。
  - 拒绝示例：`1M`、`1HM[4:1]`、`A1M`、`C2hM[4:1]`、
    `1-3[2:1]M`、`1M-3[2:1]` 均为无效 token。
  - **slide 上 `m` 的位置不限**：`1w5[8:1]m`（末尾）与 `1w5m[8:1]`（shape 后、
    bracket 前）**均合法，且都不报警告**。这与 `b` **不同**——`b` 的非典范位置
    （非紧贴 slide token 第一个 `[` 之前）会触发 strict 警告（上表 #8 的
    `kInvalidBreakSlideModifierPosition`）；`m` 没有这条位置约束:解析器扫描整段
    `noteCore` 任意位置的 `m`（`SimaiNativeParser.Slide.cpp` 的 trackMine 扫描
    循环,无 strict 位置检查）。
- **数据模型**：`TimelineNoteMarker.isMine`（tap/hold/touch/touch_hold）、
  `trackMine`（slide/wifi 轨道，星星头不置位 `headMine`）；timeline 镜像 = `TimelineRenderFlagIsMine` /
  `TimelineRenderFlagTrackMine`（`TimelineRenderData.h`）。
- **EX 抑制**：地雷头覆盖 break/each/ex —— 用专用 mine 贴图，**不叠 EX overlay**
  （对标 MajdataPlay `if (isEX && !isMine)`）。
- **贴图**：`<base>_mine.png`（`tap_mine` / `hold_mine` / `star_mine` /
  `star_mine_double` / `slide_mine` / `touch_mine` / `touchhold_*_mine` /
  `wifi_mine_*`）。skinSTD 已入库（源自 MajMine 皮肤）；skinDX 待补（由普通贴图
  色彩派生，延后）。缺图时各 selector 回退到普通贴图。
- **三方同步**：parser 落 `isMine`/`trackMine` 且拒绝大写 `M` ↔
  `TimelineQuickModelParser.cpp` 不渲染大写 `M` 地雷 ↔
  `ChartBatchTransform.Parsers.cpp` 不识别/重写大写 `M` ↔
  `SimaiParserSpec.cpp`、`TimelineModelSpec.cpp`、`ChartBatchTransformSpec.cpp` 地雷用例 ↔
  本文档。归一化/变换通过 `extraModifiers`（tap）与 segment-text 保留（slide）
  让小写 `m` round-trip。

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
| `2B` / `2X`（tap 上的大写修饰符） | **Error** | `containsUppercaseTapBreakOrExModifier` → `parseTapModifierSequence` 返回 `false`（与模式无关） |
| `2B-6[8:1]` / `2X-6[8:1]`（slide head 上的大写修饰符） | **Error** | `parseSlideHeadModifierPrefix` 返回 `false` → 通用 invalid-note 路径（与模式无关）。`B2` / `B4` 仍是合法 touch 音符。 |
| `B1x` / `Cx` / `C2x`（touch 上的 `x`） | **Error** | `parseTouchSuffix` 拒绝 `x` → `kInvalidTouchModifierPrefix`（与模式无关） |
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
| `1>3`（slide 缺失时值块） | **Error** | `kInvalidSlideDurationPrefix` —— 数字开头且含 slide shape 的 token 先走 slide parser；缺少 `[wait:duration]` 应显示为 Slide 时值错误，而不是 Hold 修饰符错误。 |
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
> 权威参考是 `docs/specs/muri/MURI_DETECTION_SPEC.md`，以及下方入口表中引用的
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
映射规则 —— 共六条 —— 收录在已有的 `docs/specs/muri/MURI_DETECTION_SPEC.md` 里，
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

`docs/specs/muri/MURI_DETECTION_SPEC.md` 是该子系统的权威参考（覆盖所有六条 alert-level
规则、180-TPS 基准、面板合并 / 去重、跳转语义）。测试覆盖：
`docs/tests/MURI_DETECTION_TEST_CHECKLIST.md`。

---

## 3. 谱面规范化 —— "谱面整理 / Format Chart"

谱面整理把当前难度文本或当前选区重写为标准形式。它**绝不会被隐式触发**，
只从菜单 / 工具箱入口进入，并在编辑器里用单个 undoable edit block 写回。
主窗口路径即使在“全文”场景下也会调用选区 API：无选区时把选区范围视作
`0..全文长度`，有选区时只替换所选片段。

### 入口与所有权

| 表面 | 文件 / 符号 |
|---|---|
| 菜单入口与写回 | `MainWindow::DocumentSection::onNormalizeWholeChart()` —— `src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp` |
| 整理弹窗 | `showNormalizeSelectionDialog()` —— 同上；使用 `QComboBox` + `UiTheme::styleDialogComboBox` |
| 公共 API（全文） | `normalizeChartText()` —— `src/core/chart/transform/ChartNormalization.h` |
| 公共 API（选区） | `normalizeChartSelectionText()` —— 同上 |
| 内部主流程 | `normalizeChartFragment()` —— `src/core/chart/transform/ChartNormalization.cpp` |
| 选项结构体 | `ChartNormalizationOptions { startAtNewMeasure, reduceTo384Grid, splitEveryFourMeasures }` |
| 结果结构体 | `ChartNormalizationResult { ok, text, errorMessage, changedCount, measureLineCount }` |
| 分段策略 helpers | `src/core/chart/transform/ChartNormalizationSegmentPolicy.{h,cpp}` |
| snap 规则 | `snapXOverY()` / `kSnap384Modulus` —— `src/core/chart/transform/Non384SnapTable.*` |
| 回归测试 | `runInlineSpecs()` —— `src/tools/chart_transform/ChartBatchTransformSpec.cpp` |

`docs/specs/chart/CHART_NORMALIZATION_SEGMENT_POLICY.md` 记录了 segment policy
落地前的目标与迁移计划。本章描述的是当前代码事实；如两者冲突，以本章和代码为准。

### 3.1 输入与门控

`normalizeChartFragment()` 的第一步是调用
`SimaiNativeParser::buildValidationReport(input, English, nullptr, timingMetadata)`。
按 §1.2 的拆解，`buildValidationReport` 是 strict validation 报告，因此：

- 任意 **Error** 都会让 normalization 立刻返回
  `{ ok=false, errorMessage=<第一条 issue 的本地化消息> }`，编辑器保持原文不动。
- **Warning** 全部放行，例如 `{7}` 非 384 因数、非典范 hold 修饰符位置、
  break-slide `b` 位置警告等。
- 失败不会提交部分重写。主窗口只在 `normalized.ok == true` 且 replacement
  与原选区不同的时候进入 `beginEditBlock()` / `endEditBlock()`。

`normalizeChartText()` 的 terminal `E` 行为不是“永远补 E”：它只在输入文本本来
包含 terminal marker 时才让 fragment renderer 追加最终 `E`。没有 `E` 的片段会保持
片段形态，这一点由 `ChartBatchTransformSpec.cpp` 的
“does not invent terminal E” 回归覆盖。

### 3.2 可见选项与隐藏选项

核心 `ChartNormalizationOptions` 的 C++ 默认值是 `{ true, true, true }`，
方便底层 API 与老测试保持“全功能打开”的默认。但应用层偏好使用
`{ startAtNewMeasure=true, reduceTo384Grid=false, splitEveryFourMeasures=true }`。

| 选项 | UI 暴露 | 应用默认 | 核心默认 | 语义 |
|---|---|---|---|---|
| `startAtNewMeasure` | 不再显示 | 固定 true | true | 选区整理时把选区起点视为新小节线。若选区原本从半小节开始，会在输出前注入 `\|\| <meter>`，让选区内文本从 phase 0 开始。全文整理基本是 no-op。 |
| `reduceTo384Grid` | `约分至384分音`：`开启/关闭` | 关闭 | true | 开启时把非 384 网格位置/无 `#` duration 近似到 384 体系；关闭时对不能被 384 精确表示的 measure 走 exact 渲染，尽量保留显式特殊分音。 |
| `splitEveryFourMeasures` | `谱面分段`：`每4小节/不分段` | 每4小节 | true | 开启时每 4 个 emitted measure line 之后插入一个空行；遇到真正 `Bpm` / `TimeSignature` leading boundary 会重置计数。不分段时不插这些额外空行。 |

旧的 `chart_normalize_start_at_new_measure` 偏好键仍由底层读写函数支持，
但主窗口加载和保存都会强制写回 true，避免用户被隐藏的历史 false 值困住。
弹窗中的两个可见下拉项在切换时会立即调用 `savePortableState()`，即使用户随后取消
整理对话框，偏好更改也会保留。

本章后文使用的内部术语：

| 术语 | 含义 | 简例 |
|---|---|---|
| token | 一个可独立规范化的谱面记号，通常是 note / touch / slide；不包含 `,`、`/`、反引号这些分隔符，也不包含 `{N}`、`(BPM)`、`\|\|`、`<HS*>` 这类控制项。 | `1b`、`2h[8:1]`、`C1h[4:1]`、`1-5[8:1]` |
| group | 同一时刻内用 `/` 连接的一组 token，即一次合击组。 | `1/2h[8:1]` 是一个 group，里面有 `1` 与 `2h[8:1]` 两个 token。 |
| moment | 一个时间相位上的全部内容；一个 moment 可以有多个 group，group 之间用反引号分隔，直到 `,` 推进到下一个相位。 | ``{4}1/2`3,`` 表示同一 moment 内有 `1/2` 和 `3` 两个 group，随后 `,` 推进 1/4 小节。 |
| segment | 由 `{N}`、小节边界或终止符切出来的时间段，记录当前 subdivision、起止相位、是否消费过逗号等信息。 | `{5},` 会形成一个显式特殊 segment，exact 渲染时可能强制保留 `{5}`。 |
| chunk | 渲染器为了选择某个 `{N}` 而处理的连续片段；exact 路径会优先在 beat boundary 或强制 reset 的特殊 segment 前后切 chunk。 | 4/4 小节通常可按 beat 切成 4 个 chunk；`{4},{5},{4},` 会让 `{5}` 前后成为不同 chunk。 |

### 3.3 扫描模型与边界

全文路径使用 `seedFromTimingMetadata()`：若 `&whole_time_signature=` 有效，就从
对应拍号开始；否则用 simai 默认拍号。选区路径先用 `scanNormalizationSeed(prefix, timingMetadata)`
扫描选区前缀，恢复当前 `{N}`、meter、BPM 和 measure phase。

扫描与主流程都遵守同一组边界规则：

| 输入事件 | 行为 |
|---|---|
| `,` | flush 当前 token/group/moment，按 `1/currentBeats` 推进相位；跨过当前 measure 末尾时 append 一个 `RenderMeasure` 并从新 measure 继续 overflow。 |
| `/` | flush 当前 token，仍属于同一 each group。 |
| `` ` `` | flush 当前 token 并结束当前 group；同一 moment 的多个 group 输出时用 `` ` `` 连接。 |
| `{N}` | 更新 `currentBeats`，并开启一个显式 subdivision segment；该 segment 记录 beats、起止相位、是否消费过逗号、相对 moment 位置。 |
| `(BPM)` | 任何合法 BPM 指令都会 `restartMeasureAtCurrentPosition()`，即使数值等于当前 BPM；输出为 `Bpm` boundary item。 |
| 合法 `\|\| x/y` | 作为 time-signature 控制，关闭当前 measure，从新 meter 的 phase 0 开始，并输出 `TimeSignature` boundary item。 |
| 普通 `\|\| ...` 注释 | `splitMeasureAtCurrentPosition()`：关闭当前 measure，但把累计相位带到下一 measure；注释原文作为 standalone boundary 保留。 |
| `<HS*...>` | 作为 standalone boundary item 保留，不参与 token 规范化。 |
| `E` / `e` terminal marker | 终止或关闭当前活动 segment；是否最终输出 `E` 由调用路径决定。 |

`restartMeasureAtCurrentPosition()` 与 `splitMeasureAtCurrentPosition()` 的区别是
phase：前者让下一 measure 从 0 开始，后者让下一 measure 继承
`currentMeasure.startPhaseWhole + currentPositionWhole`，用于普通注释拆行后继续填满原小节。

### 3.4 渲染策略：approximate、exact 与 segment policy

`renderMeasureLine(measure, options)` 是 dispatcher：

- `reduceTo384Grid=true`：总是走 `renderMeasureLineApproximate()`。
- `reduceTo384Grid=false`：先跑 `measureRequiresExactRendering()`；只有当前 measure
  携带 384 grid 无法精确表示的信息时才走 `renderMeasureLineExact()`，否则仍走 approximate。

`measureRequiresExactRendering()` 的触发条件包括：

- measure 的 `startPhaseWhole` 或 `lengthWhole` 无法被 384 精确表示。
- 存在消费过逗号、且 `beats` 不是 384 约数的显式 `{N}` segment。
- 当前 meter 下的 beat boundary 无法被 384 精确表示。
- 任一 moment 的 `positionWhole` 无法被 384 精确表示。

#### approximate 路径

approximate 路径先把 measure 的起点、长度和 moment 位置落到 384 内部网格。
每个 beat segment 独立选择 `{N}`：

1. 对段内每个 moment 调 `snapXOverY(position.numerator, position.denominator)`，
   取所有 `snap.q` 的 LCM。
2. 段内无 moment 时从 1 开始；若结果是 `1/2/4/8/16` 之一，则 bump 到 16，
   保持空 beat 的默认视觉密度。
3. 与 `meterDenominator` 对齐，保证拍内分段能拆成整数 slot。
4. 交给 `segment_policy::approximateSegmentSubdivision()`，把 segment 自身长度的
   denominator 纳入约束；如果 preferred `{N}` 不能精确表达这个 segment 的长度，
   就提升到能表达的 subdivision，最高到 384。

因此 `reduceTo384Grid=true` 的完整含义是：

```text
原始时间 -> 近似到 384-grid 时间 -> 输出必须精确表达这个近似后的时间
```

它允许 `{5},` 变成 77 个 `{384}` slot，但不允许已经能被 384 表达的
`{32},{1},` 被空段 fallback 放大成 `{16},`。

#### exact 路径

exact 路径不把位置吸附到 384 grid。它对每个 chunk 选择能精确表达
chunk 长度和 chunk 内 relative moment 位置的最小 `{N}`，并优先尝试在 beat boundary
处切 chunk，使输出不会因为一个局部特殊分音把整行都提升到过密 subdivision。

显式特殊 `{N}` segment 还会经过
`segment_policy::specialSegmentForcesReset(segment, meterDenominator)`：

- 必须是消费过逗号的 segment。
- `beats` 必须不是 384 约数。
- segment 的起点或终点若不在当前 meter 的半拍网格
  `1 / (2 * meterDenominator)` 上，就强制 reset。

强制 reset 的 segment 会在 exact renderer 中单独保留原 `{N}`，前后范围分别渲染。
如果特殊 segment 起止都在半拍网格上，就允许 exact 最小表达自行约简：

| 输入（`reduce=false`） | 结果倾向 | 原因 |
|---|---|---|
| `{5},` | 保留 `{5},` | 1/5 不在 4/4 半拍网格上，segment forces reset。 |
| `{10}1,,,,,` | 可约为 `{2}1,` | 长度 1/2 在半拍网格上，且 note 在 segment 起点。 |
| `{10},1,,,,` | 保留能表达 1/10 内部落点的 subdivision | 内部 moment 需要特殊分音表达。 |
| `{4},{10},,,,,{4},` | `{10}` 段可被 exact 简化 | 起止在半拍网格上。 |
| `{4},{5},{4},` | `{5}` 段前后 reset 并保留 `{5}` | 特殊段不在半拍网格上。 |

### 3.5 时值渲染策略：`snapXOverY`

所有 384 近似都使用 `snapXOverY(x, y)`。规则：

| 输入情况 | 返回 |
|---|---|
| `y` 是 384 的约数（或 `y == 1`） | `(x, y)` 直通 |
| `y > 384` | `(round(384 * x / y), 384)` |
| `0 < y <= 384` 且 `y` 不是 384 约数 | `q = max{d : d \| 384, d <= 4y}`，`p = round(q * x / y)` |

典型映射：`1/5 -> 3/16`，`1/7 -> 3/24`，`1/28 -> 3/96`，
`1/2000 -> 0/384`。`x >= y` 不做 carry split，直接按同一公式 round。

Duration 字符串只在以下条件同时满足时改写：

1. signature 是普通 `[beats:numerator]`，不含 `#`。
2. `reduceTo384Grid=true`。
3. 原始 `beats` 不是 384 约数。

改写结果是 `[snap.q:snap.p]`。如果 `snap.p == 0`：

| Token 类 | 结果 |
|---|---|
| Touch hold | 保留 bracket，输出 `[1:0]`。 |
| Note hold | 删除整个 bracket，只留下 `h`。 |
| Slide duration | slide 不允许零时长，floor 到 `[q:1]`，通常是 `[384:1]`。 |

Hold / slide duration 不参与渲染 `{N}` 的选择；输出行的 subdivision 只由
moment/rest segment 的位置与长度决定。带 `#` 的时值签名逐字保留。

### 3.6 Token 规范化

`canonicalizeToken()` / `renderTokenForGrid()` 依次尝试 touch、note、slide 三类
轻量 tokenizer。任一成功就重组 token；全部失败则输出 trimmed 原文
（通常会先被 strict validation 拦住）。

| Token 类 | 识别条件 | 重组顺序 | 注意点 |
|---|---|---|---|
| Touch | `C`（可带 `1/2`）或 `A/B/D/E` + lane | prefix -> `b` -> `x` -> `h` -> `f` -> bracket | bracket 与 `h` 互为充要。主 parser 已拒绝 touch `x`，所以含 `x` 的 touch 通常进不到规范化输出。 |
| Note | 1-8 lane、非 slide、非纯数字串 | lane -> sorted extra modifiers -> `b` -> `x` -> `h` -> bracket | `f`、`?`、`!`、`@` 等不是 note 的内建 modifier，会作为 extra modifier 按 codepoint 排序。 |
| Slide head | 1-8 lane 且含 slide operator | lane -> sorted head extra modifiers -> `b` -> `x` -> slide body | head 区的 `h` 会让整个 token tokenizer 失败；`?`、`!`、`@` 作为 extra modifier 排序。 |
| Slide `*` 分支 | slide body 内按 `*` 拆分 | 每个分支独立保留自己的 break `b`，插回该分支第一个 `[` 前 | 这是回归点：`1-5[8:1]*-4b[8:1]` 不能被重写成 `1-5b[8:1]*-4[8:1]`。 |

### 3.7 输出版面规则

| 规则 | 当前行为 |
|---|---|
| measure 输出 | 通常每个 `RenderMeasure` 输出一行；forced exact segment reset 可能让一个 measure 的渲染文本内部含换行。 |
| 同一 moment 多 token | 用 `/` 连接。 |
| 同一 moment 多 group | 用 `` ` `` 连接。 |
| beat boundary | approximate / exact 都在 beat boundary 处插入空格。 |
| `{N}` 前缀 | 每个 chunk/segment 第一次输出或 subdivision 变化时写 `{N}`；相邻 chunk 若 `{N}` 相同，可省略重复前缀。 |
| `Bpm` + `TimeSignature` boundary | 相邻时合并成单行，例如 `(180) \|\| 3/4`。 |
| 普通 `\|\| ...` 注释 | 拆成 standalone 行，不当作 time-signature control。 |
| 4 小节分段 | `splitEveryFourMeasures=true` 时，每个真实 `Bpm` / `TimeSignature` section 内每 4 个 emitted measure line 后追加空行；末尾多余空行会被剪掉。 |
| terminal `E` | `normalizeChartText()` 仅当输入本来有 terminal marker 时输出最终 `E`；选区 API 仅在选区文本本身包含 terminal marker 时保留它。 |

`changedCount` 与 `measureLineCount` 的语义：

```cpp
result.measureLineCount = emittedMeasureLines;
result.changedCount = result.text == input ? 0 : qMax(1, emittedMeasureLines);
```

- `changedCount` 不是 token/atom 级精确改动数，而是“未改动为 0；有改动则给出
  至少 1 的 measure 级规模提示”。
- `measureLineCount` 计数的是 emitted `RenderMeasure` 数，不一定等于最终文本的物理行数。

### 3.8 选区整理与 `{N}` carry

`normalizeChartSelectionText(fullText, selectionStart, selectionEnd, ...)` 先校验范围。
范围非法时返回 `{ ok=false, errorMessage="Invalid selection range." }`。

选区路径有两个额外行为：

1. **前置拍号注入。** `scanNormalizationSeed()` 发现选区前缀留下非零
   `startPhaseWhole`，且 `startAtNewMeasure=true` 时，输出前注入当前 meter 的
   `|| <meter>`，并把 fragment seed phase 归零。主窗口当前固定使用 true。
2. **尾部 `{N}` carry。** 如果选区文本不包含 terminal marker，并且选区后方文本会在
   遇到 `E`、新的 `{N}` 或普通 `||` 边界之前继续消费当前 subdivision，则在输出末尾
   追加最终 active `{N}`，以免替换选区后改变选区外文本的解析。

`followingTextNeedsBeatsCarry()` 的判定顺序是：跳过空白；先遇到 terminal `E/e`、
`{` 或 `||` 返回 false；先遇到 `,` 或普通 token 内容返回 true；直到末尾都没有内容则 false。

### 3.9 偏好持久化与测试覆盖

底层偏好键：

- `chart_normalize_start_at_new_measure`
- `chart_normalize_reduce_to_384_grid`
- `chart_normalize_split_every_four_measures`

主窗口加载时用 `{true, false, true}` 作为应用默认，并强制
`chartNormalizeStartAtNewMeasure_ = true`。保存时也写入 `startAtNewMeasure=true`，
以及用户可见的 `reduceTo384Grid`、`splitEveryFourMeasures`。

关键回归覆盖位于 `ChartBatchTransformSpec.cpp`：

- terminal `E` 不凭空新增、选中 `E` 时可保留。
- touch / note / slide 修饰符规范化。
- no-`#` duration 的 384 snap、零时长 hold/slide 分支、`#` duration 保留。
- `{7}`、`{28}` 等非 384 分音在 reduce=true 下的 `snapXOverY` 结果。
- BPM + inline time-signature 合并、普通 `||` 注释 standalone 拆行、3/4 metadata 默认拍号。
- `splitEveryFourMeasures=false` 时不插每 4 小节空行。
- `reduce=false` 下 384-grid measure 回落 approximate、特殊 `{5}` / `{10}` segment 的
  reset / exact 简化策略。
- 选区起点注入 `|| 4/4` 与尾部 `{N}` carry。

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
  本文 §3。

本文档是导航索引。详细规格才是唯一真相。
