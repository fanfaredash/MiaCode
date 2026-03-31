# 无理检测规则与行为规格

本文档汇总当前 `MiaCode` 无理检测的输入、判定类型、阈值、锚点、列表合并与去重行为，作为实现、回归测试和后续补充样例时的统一依据。

## 1. 范围

- 本规格覆盖三层结果：
  - 运行时无理分析：`MuriAnalyzer::analyze`
  - 静态参考：`miacode::muri::buildStaticMuriReferences`
  - 面板可见项合并：`miacode::muri::buildVisibleMuriPanelEntries`
- 本规格不覆盖：
  - 语法错误与警告
  - 预览渲染细节
  - 导出画面表现

## 2. 输入与前置归一化

- 输入基于 `SimaiNativeParser::parseForTimeline` 产出的 `TimelineNoteMarker`。
- 若上游对 marker 做了 `&first` 平移，则运行时分析和静态参考都必须使用平移后的秒数。
- slide / wifi 的头星会参与无理分析：
  - 运行时会生成可判定的合成按下动作。
  - 静态参考会生成辅助 head tap，用于 slide-head-tap 检测。
- 同时刻、同位置的 press 叠键在多押统计前会先折叠为一个手部 cluster。
  - 因此“包含叠键，但去掉叠键后只剩两手以内”的情况，不视为多押。

## 3. 诊断类型矩阵

| 类型 | 中文 UI 名称 | 运行时分析 | 静态参考 | 说明 |
|---|---|---|---|---|
| `SlideTooFast` | `内无` | 是 | 否 | slide / wifi 最终完成时机落在临界窗之外 |
| `SlideHeadTap` | `外无` | 是 | 是 | slide / wifi 头部提前判定后续 tap / hold |
| `TapOnSlide` | `撞尾` | 是 | 是 | slide / wifi 尾部或路径碰撞后续 tap / hold |
| `Overlap` | `叠键` | 是 | 是 | 同位置普通物件重叠 |
| `MultiTouch` | `多押` | 是 | 否 | 运行时手部动作超过两手 |

## 4. 共享时间常量

- 判定时基：`180 TPS`
- 抬手延迟：`3 / 180 = 16.7 ms`
- 额外 pad down 延迟：`9 / 180 = 50.0 ms`
- tap critical：`3 / 180 = 16.7 ms`
- tap available：`27 / 180 = 150.0 ms`
- touch critical / available：`27 / 180 = 150.0 ms`
- slide critical：`42 / 180 = 233.3 ms`
- slide available：`108 / 180 = 600.0 ms`
- slide leading：`15 / 180 = 83.3 ms`
- tap-on-slide 起始阈值：`1 / 180 = 5.6 ms`
- touch-on-slide 阈值：`24 / 180 = 133.3 ms`
- slide-too-fast 阈值：`1 / 180 = 5.6 ms`

## 5. 运行时分析规则

### 5.1 SlideTooFast

- slide / wifi 的最终完成秒数会与其 critical window 比较。
- 若完成时机落在 critical window 之外，则报 `SlideTooFast`。
- `SlideTooFast` 只来自运行时分析，不生成静态参考。
- 若能追溯到“最后一个提前判定原因”，详情文本优先写成“被哪个物件提前判定”；否则写成“自身落在临界窗之外”。

### 5.2 SlideHeadTap

- 当普通物件因 slide / wifi 头部、额外 pad down 或头星触发而被提前判定时，报 `SlideHeadTap`。
- 运行时路径与静态参考都会覆盖 `slide` 和 `wifi`。
- 当前静态参考只对 `tap` / `hold` 生成 slide-head-tap 参考；`touch` / `touch-hold` 不在静态 slide 碰撞参考范围内。

### 5.3 TapOnSlide

- 当普通物件因 slide / wifi 尾部、路径进入时机或 critical 末端窗口而可能发生碰撞时，报 `TapOnSlide`。
- 运行时路径基于真实提前判定结果。
- 静态参考基于：
  - slide 各 segment 的 `pad enter time`
  - slide / wifi 的 critical second
  - 可配置的静态碰撞阈值
- 当前静态参考只对 `tap` / `hold` 生成 tap-on-slide 参考；`touch` / `touch-hold` 不在静态 slide 碰撞参考范围内。

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
- 因此：
  - `123` 仍然是多押
  - `118` 先折叠掉 `11` 的叠键后只剩两手，不再报多押

## 6. 告警级别规则

### 6.1 固定为无理

- `SlideTooFast`
- `Overlap`

### 6.2 SlideHeadTap 的 Warning 条件

- `gap > 0`
- `gap <= 50.0 ms`
- 且该 slide head 没有同时命中 tap-on-slide head 的强无理条件

满足上面条件时，`SlideHeadTap` 为 `Warning`，否则为 `Muri`。

### 6.3 TapOnSlide 的 Warning 条件

- `gap > 150.0 ms`
- `gap <= staticTapOnSlideThresholdSeconds`

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
- `MuriDiagnostic.anchorSecond` 表示面板显示、排序和跳转应使用的锚点秒数。
- 当诊断涉及多个物件时，锚点取“首个涉事物件”：
  - 优先按秒数更早者
  - 若秒数相同，则按文档顺序更早的 `line / col`
- 面板显示的 `line / col` 与锚点绑定，而不是默认挂在被影响的后续物件上。

### 7.2 静态参考

- `MuriStaticReference` 本身保留 `affected` 和 `cause` 两端。
- 面板显示与跳转时，锚点取 `affected` / `cause` 中更早的那个。
- `occurrenceSecond` 仍保留 `affected.second`，用于和实际参考来源对齐。

### 7.3 典型例子

对于：

```simai
(240){16}
8>3[4:1],,,,,
8,
E
```

- 运行时 `SlideHeadTap` 的真正发生秒数是 slide 头提前判定后续 tap 的时刻。
- 但面板显示和跳转必须落在 `slide 8>3` 本身所在行列，而不是后续 `8`。

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
