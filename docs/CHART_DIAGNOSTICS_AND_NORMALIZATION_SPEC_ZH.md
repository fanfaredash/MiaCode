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

### 入口

| 表面 | 文件 / 符号 |
|---|---|
| 公共 API | `SimaiNativeParser::validateSyntax()` —— `src/core/chart/parser/SimaiNativeParser.h:62` |
| 结构化报告 | `SimaiNativeParser::buildValidationReport()` —— `:67` |
| UI 宿主 | `MainWindow::ValidationSection` —— `src/app/mainwindow/sections/validation/MainWindow.Validation*.cpp` |
| 底部 tab 门控 | `BottomTabsQuickHost.qml` 中的 `controller.validationTabVisible` |

### 检测分类

strict pass 调用了若干个验证组（都在 `SimaiNativeParser.*.cpp` 下）：

| 组别 | 文件 | 例子 |
|---|---|---|
| 括号 / 结构 | `SimaiNativeParser.Driver.cpp` | `[ ]`, `( )`, `{ }` 不匹配；未终止字符串 |
| Lane 合法性 | `MuriConfig.h::padTokenIsValid()` | lane 不在 `1..8`；ring 不在 `A..E`；未知中心键（`C` 之外） |
| Slide / wifi | `SimaiNativeParser.Slide.cpp` | slide 路径异常、缺端点、`[N:M]` 时间分数非法 |
| Tap / touch / hold | `SimaiNativeParser.TouchTap.cpp` | tap+slide-head 冲突、hold 缺时间 |
| Modifier 顺序 | `SimaiNativeParser.StrictChecks.cpp` | `b/x/h/f` 非典范顺序；同一 modifier 重复 |
| Time-signature | `SimaiNativeParser.StrictChecks.cpp:105` | `||x/y` 放置；纯注释行被跳过 |

### 问题呈现

每个 issue 是 `SimaiNativeValidationIssue`（头文件 `:36-43`）：

```cpp
struct SimaiNativeValidationIssue {
    int line;
    int col;
    int endCol;
    SimaiNativeValidationSeverity severity;  // Error | Warning
    QString rawMessage;
    QString displayMessage;     // 已本地化
};
```

UI 上的呈现：

- **面板列表**：每个 issue 一行，`[Lline Ccol]` 前缀 + 显示文本。
- **编辑器 extra-selection**：在 `(line, col..endCol)` 上叠加红色波浪线 /
  琥珀色下划线（由 `refreshEditorExtraSelections` 驱动）。
- **header 级 ignore**：每条 issue 带一个 issue-type 字符串 key，谱面作者
  可以在文件顶部加 `||#ignore <key>` 行来项目级别禁用某个分类。

### 语法检测**不**做什么

- 不报无理 / 可奏性问题（那是子系统 2 的事）。
- 不自动修复；重写是子系统 3 的工作。
- 不阻塞 parse —— `parseForTimeline()` 是 lenient 的，无论 strict 错误是否
  存在都会跑，让用户能继续编辑。

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
