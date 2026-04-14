# 无理检测规则与行为规格

本文档汇总当前 `MiaCode` 无理检测的输入、判定类型、阈值、锚点、列表合并与去重行为，作为实现、回归测试和后续补充样例时的统一依据。

## 1. 范围

- 本规格覆盖三层结果：
  - 运行时无理分析：`MuriAnalyzer::analyze`
  - 静态参考：`miacode::muri::buildStaticMuriReferences`
  - 面板可见项合并：`miacode::muri::buildVisibleMuriPanelEntries`
- 本规格同时覆盖无理面板条目点击后的编辑器跳转行为。
- 本规格不覆盖：
  - 语法错误与警告
  - 预览渲染细节
  - 导出画面表现

## 2. 输入与前置归一化

- 输入基于 `SimaiNativeParser::parseForTimeline` 产出的 `TimelineNoteMarker`。
- 若上游对 marker 做了 `&first` 平移，则运行时分析和静态参考都必须使用平移后的秒数；即使平移后秒数为负，也必须原样进入判定，不能折到 `0`。
- slide / wifi 的头星会参与无理分析：
  - 运行时会生成头星对应的 tap-like judge target，但它复用所属 slide / wifi 的真实语义，不额外参与多押手动作统计。
  - 静态参考会生成辅助的 head-star target，用于 `SlideHeadTap` / `TapOnSlide` 参考构建。
- 同时刻、同位置的 press 叠键在多押统计前会先折叠为一个手部 cluster。
  - 因此“包含叠键，但去掉叠键后只剩两手以内”的情况，不视为多押。

## 3. 诊断类型矩阵

| 类型 | 中文 UI 名称 | 运行时分析 | 静态参考 | 说明 |
|---|---|---|---|---|
| `SlideTooFast` | `内无` | 是 | 否 | slide / wifi 最终完成时机落在临界窗之外 |
| `SlideHeadTap` | `外无` | 是 | 是 | slide / wifi 头部提前判定后续 `tap` / `hold` / `star` |
| `TapOnSlide` | `撞尾` | 是 | 是 | slide / wifi 尾部或路径碰撞后续 `tap` / `hold` / `star` |
| `Overlap` | `叠键` | 是 | 是 | 同位置普通物件重叠 |
| `MultiTouch` | `多押` | 是 | 否 | 运行时手部动作超过两手 |

## 4. 共享时间常量

- 判定时基：`180 TPS`。用于把所有判定、窗口和延迟常量统一换算成 tick 与秒。
- 抬手延迟：`3 / 180 = 16.7 ms`。用于普通按下动作结束后额外保留一小段按住时间，避免手部动作过早消失。
- 额外 pad down 延迟：`9 / 180 = 50.0 ms`。用于模拟 slide 头等场景的额外按下触发时机。
- tap critical：`3 / 180 = 16.7 ms`。用于判定 tap 何时开始落出 critical window。
- tap available：`27 / 180 = 150.0 ms`。用于限制 tap 在运行时还能被判定的最晚可用窗口。
- touch critical / available：`27 / 180 = 150.0 ms`。用于 touch 的 critical 判定和可判定窗口边界。
- slide critical：`42 / 180 = 233.3 ms`。用于计算 slide 最终完成判定的核心临界窗基准。
- slide available：`24 h = 86400.0 s`。用于限定 slide / wifi 在运行时仍可等待完成的整体可用窗口；超出后直接视为 bad。
- slide leading：`15 / 180 = 83.3 ms`。用于给 slide 相关运行时动作保留前导时间语义。
- tap-on-slide 起始阈值：`1 / 180 = 5.6 ms`。用于过滤过于贴近 slide 起点的极小时间差，避免过早把物件算进撞头/撞尾区间。
- touch-on-slide 阈值：`24 / 180 = 133.3 ms`。用于判定 touch 与 slide 之间的贴靠 / 碰撞时间边界。
- slide-too-fast 阈值：`1 / 180 = 5.6 ms`。用于判断 slide / wifi 完成时机是否已经快到超出允许误差。
- SlideHeadTap 无启动 tap 的警告阈值：`50.0 ms`。
- SlideHeadTap 晚窗口警告阈值：`150.0 ms`。

## 5. 运行时分析规则

### 5.1 SlideTooFast

- slide / wifi 的最终完成秒数会与其 critical window 比较。
- 若完成时机落在 critical window 之外，则报 `SlideTooFast`。
- `SlideTooFast` 只来自运行时分析，不生成静态参考。
- 只有当 slide / wifi 的最终完成判定区确实由其他 `slide` / `tap` / `touch` 的 pad-down 完成时，才应报 `SlideTooFast`。
- 若超长 slide 在后半段已经没有有意义的外部 pad-down，剩余只会由自身轨迹自然收束，则视为正常通过，不报 `SlideTooFast`，也不应额外显示 slide `Good` 判定特效。
- 若能追溯到“最后一个提前判定原因”，详情文本优先写成“被哪个物件提前判定”；否则写成“自身落在临界窗之外”。

### 5.2 SlideHeadTap

- 当普通物件因 slide / wifi 头部、额外 pad down 或头星触发而被提前判定时，报 `SlideHeadTap`。
- 运行时路径与静态参考都会覆盖 `slide` 和 `wifi`。
- 当前静态参考会对以下受影响目标生成参考：
  - `tap`
  - `hold`
  - `head star`
- `touch` / `touch-hold` 当前不在静态 slide 碰撞参考范围内。

### 5.3 TapOnSlide

- 当普通物件因 slide / wifi 尾部、路径进入时机或 critical 末端窗口而可能发生碰撞时，报 `TapOnSlide`。
- 运行时路径基于真实提前判定结果。
- 静态参考基于：
  - slide 各 segment 的 `pad enter time`
  - slide / wifi 的 critical second
  - 可配置的静态碰撞阈值
- 当前静态参考会对以下受影响目标生成参考：
  - `tap`
  - `hold`
  - `head star`
- `touch` / `touch-hold` 当前不在静态 slide 碰撞参考范围内。

### 5.4 Overlap

- 运行时 `Overlap` 来自普通物件最终以“同位置叠键”方式判定失败。
- 静态 `Overlap` 来自非 slide 物件之间的同 pad 重叠参考：
  - tap / touch 对 tap / touch：同 pad 且时差不超过 `6 / 180 = 33.3 ms`
  - tap / touch 对 hold / touch-hold：落在对方按住区间前后各 `33.3 ms` 的扩展区间内
  - hold / touch-hold 对 hold / touch-hold：两个扩展区间相交
- slide 头真撞尾的配对不会被再次当作晚到 overlap 补报。

### 5.5 MultiTouch

- 多押只来自运行时分析。
- 分析基于运行时手部动作和 touch point cluster。
- cluster 总手数大于 `2` 时，报 `MultiTouch`。
- 同中心、同半径的 press 叠键会先合并进同一个 cluster。
- 头星 judge target 不计入多押手动作。
- 因此：
  - `123` 仍然是多押
  - `118` 先折叠掉 `11` 的叠键后只剩两手，不再报多押

### 5.6 详情文本

- `SlideHeadTap` 详情会明确写出受影响目标，而不是只写裸类型：
  - `slide 1>5 start will early-judge tap 8, gap 40.0 ms.`
  - `slide 1>5 jump-start will early-judge hold 8, gap 170.5 ms.`
  - `slide 1>5 jump-start will early-judge star 1, gap 170.5 ms.`
- `TapOnSlide` 详情同样会明确写出受影响目标：
- `slide 8>4 trajectory may collide with tap 8, gap 187.5 ms.`
- `slide 8>4 trajectory may collide with hold 8, gap 187.5 ms.`
- `slide 8>4 trajectory may collide with star 1, gap 187.5 ms.`
- `Warning` 文案使用“可能”类措辞，`Muri` 文案使用确定措辞。
- `star N` 专门用于显示头星目标，不应退化成普通 `tap` 文案。
- 带保护的简单目标当前统一显示为 `protected ...`，并保留配置 token：
  - `protected tap 8x`
  - `protected hold 4xh`
  - `protected star 1x`

## 6. 告警级别规则

### 6.1 固定为无理

- `SlideTooFast`
- `Overlap`

### 6.2 SlideHeadTap 的 Warning 条件

满足以下任一条件时，`SlideHeadTap` 为 `Warning`：
- 受影响目标包含保护（当前实现详情会显示为 `protected tap 8x` / `protected hold 4xh` / `protected star 1x` 这类形式）

- `0 < gap <= 50.0 ms`，且该 slide head 没有启动时 tap
- `gap >= 150.0 ms`

否则为 `Muri`。

该规则要求运行时诊断、静态参考与可见面板条目保持一致。

### 6.3 TapOnSlide 的 Warning 条件

- `gap > 150.0 ms`
- `gap <= staticTapOnSlideThresholdSeconds`
- 或受影响目标包含保护（当前实现详情会显示为 `protected tap 8x` / `protected hold 4xh` / `protected star 1x` 这类形式）

默认静态阈值：

- 默认值：`200 ms`
- 可调范围：`150 ms .. 250 ms`
- 运行时分析会把传入值 clamp 到这个范围

### 6.4 MultiTouch 的 Warning 条件

- 本次多押涉及 touch 类动作
- 非 touch 的 hand count 不超过 `2`

满足时为 `Warning`，否则为 `Muri`。

## 7. 锚点、行列号与跳转

### 7.1 运行时诊断

- `MuriDiagnostic.second` 表示真正发生判定的秒数。
- `MuriDiagnostic.anchorSecond` 表示面板显示、排序和时间定位应使用的锚点秒数。
- 当诊断涉及多个物件时，锚点取“首个涉事物件”：
  - 优先按秒数更早者
  - 若秒数相同，则按文档顺序更早的 `line / col`
- 面板显示的 `line / col` 与锚点绑定，而不是默认挂在被影响的后续物件上。

### 7.2 静态参考

- `MuriStaticReference` 本身保留 `affected` 和 `cause` 两端。
- 面板显示时，锚点取 `affected` / `cause` 中更早的那个。
- `occurrenceSecond` 仍保留 `affected.second`，用于和实际参考来源对齐。

### 7.3 点击跳转

- 点击无理面板条目时，编辑器必须跳到条目存下来的精确 `line / col`。
- 预览 / timeline 仍可以使用 `anchorSecond` 做 seek。
- 但按秒 seek 不能覆盖精确列号跳转，避免出现“只跳到该行、列号丢失”的现象。

### 7.4 典型例子

对于：

```simai
(240){16}
8>3[4:1],,,,,
8,
E
```

- 运行时 `SlideHeadTap` 的真正发生秒数是 slide 头提前判定后续 tap 的时刻。
- 但面板显示和点击跳转必须落在 `slide 8>3` 本身所在行列，而不是后续 `8`。

## 8. 面板可见项合并规则

- 面板先放运行时诊断，再补静态参考。
- 当运行时和静态参考命中同一种 `MuriKind` 且同一个锚点 `LxCy` 时，运行时优先，静态参考不再重复显示。
- 面板排序键：
  - 先按 `entry.second`（锚点秒数）
  - 再按 `line`
  - 再按 `col`
  - 最后按 `kind`

## 9. 叠键去重规则

- 运行时 `Overlap`：
  - 同一发生秒数最多保留 `1` 条诊断
- 静态 `Overlap`：
  - 同一 `affected.second` 最多保留 `1` 条参考
- 面板可见项：
  - 同一锚点秒数最多保留 `1` 条 `Overlap`
  - 该规则同时作用于运行时与静态参考的合并后结果

这意味着：

- `111` 最终只显示一条叠键
- 即便同时存在运行时叠键和静态叠键参考，面板也只保留第一条可见项

## 10. 当前自动样例

`muri_spec` 当前固定覆盖以下最小样例：

- 锚点回到首个涉事物件：

```simai
(240){16}
8>3[4:1],,,,,
8,
E
```

- 同头星 slide 不再误报多押，同时保留合法的 `SlideHeadTap` / `TapOnSlide`：

```simai
(240){16}
8>4[4:1]/1>5[4:1],,,,,
1-5[8:1],
E
```

- 头星撞尾参考恢复：

```simai
(193){8},1/5-8[8:1],,5/2-7[8:1],,2/6-3[8:1],,1/6,
{8}2,3-6[8:1],,3/7-2[8:1],,7/4-1[8:1],,4/8-5[8:1],
E
```

- `Prophesy One UTG` 风格的晚窗口 `SlideHeadTap`，约 `170.5 ms` 时应降为 `Warning`
- 同时刻多个叠键只留第一条：

```simai
(240){16}
111,
E
```

- 去掉叠键后不再构成多押：

```simai
(240){16}
118,
E
```

- 真实多押仍然保留：

```simai
(240){16}
123,
E
```

## 11. 共享时间常量串联例子

一个 Slide，形状为 `1>4`，时间信息如下：

- `marker.second = 0 ms`：slide 头出现时刻
- `slideTraceSecond = 250 ms`：轨迹真正开始跑的时刻
- `endSecond = 500 ms`：正常完成时刻

完整判定过程时间轴示例如下：

- `-83.3 ms (slide leading)`

这时 slide 已经进入 runtime 可判定状态，`availableSecond = marker.second - 83.3 ms`。如果更早就有相关 pad-down（区域按下），slide 可以从一开始就继承这些状态。注意，`83.3 ms` 已经把抬手延迟 `16.6 ms` 算在内了，等价于实际判定边界约为 `-100 ms`。

- `0 ms (marker.second)`

slide 头出现。

- `250 ms (slideTraceSecond)`

slide 启动。同时代码会发一次真实的 slide-head pad-down。

- `275 ms` 或 `300 ms (extra pad down)`

这里会在 `slideTraceSecond + min(首 area 进入时刻, 50ms)` 再补一次更晚的头部 pad-down 机会，让更靠后的后续 note 也可能被 slide 头提前吃到。
可能影响的无理类型：`SlideHeadTap`

- 中间各个 area 起点

slide / wifi 每进一个新 area，simple-note 模拟都会收到一次那个 area 的 pad-down。

- `467 ms`（本例“被提前判掉”的时机）

如果最后一个 area 在这里就满足，slide 会在这一 tick 被判完成。此时真正发力的是 slide critical：系统拿这个完成时刻去和 `criticalSecond ± criticalDeltaSecond` 比。基础容忍是 `233.3 ms + 末 area 时长的四分之一`；实现里另有一个独立的 `+50 ms shift` 容错分支。
可能影响的无理类型：`SlideTooFast`

- `500 ms (endSecond)`

正常结束参考线。到这还没完成，不会立刻判死，只是进入“还能继续等”的区间。
可能影响的无理类型：`SlideTooFast`

- `endSecond + 24 h (slide available)`

这是最后等待点。到这还没完成，就强制 bad。

对于 `slide critical`，实现里另有一个独立的 `+50 ms shift` 容错分支。这个分支不是简单给 `criticalDeltaSecond` 再加 `50 ms`，而是额外用一扇向前偏了 `50 ms` 的基础窗口再检查一次，因此主要只会多宽容一部分过早完成的 slide。`MaiMuriDX` 的 slide 判定里也有同款双分支实现；wifi 不使用这个分支。伪代码如下：

```text
delta = judgeSecond - criticalSecond
if abs(delta) <= criticalDeltaSecond:
    pass
elif abs(delta + 50ms) <= 233.3ms:
    pass
else:
    bad
```
