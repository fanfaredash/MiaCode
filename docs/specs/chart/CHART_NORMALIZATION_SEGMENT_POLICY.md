# 谱面整理分音段策略规格

本文档记录谱面整理在 validation 通过之后的核心布局策略，重点约束
`reduceTo384Grid=true` 的近似边界、`reduceTo384Grid=false` 的特殊分音段
处理、选区尾部 `{N}` 恢复逻辑，以及后续实现拆分计划。

当前主实现仍在 `src/core/chart/transform/ChartNormalization.cpp`。本规格描述的是
下一步应落地的目标行为。

## 1. 核心原则

谱面整理必须满足两个层级的时值守恒：

1. `reduceTo384Grid=true` 时，允许把非 384 网格时间近似到 384 网格；但输出文本
   必须精确表达这个“近似后的 384-grid 时值”，不能再由空段 fallback、默认
   `{16}` 或 slot rounding 引入额外误差。
2. `reduceTo384Grid=false` 时，非 384 特殊分音段应尽量保留作者显式分音语义；
   只有在明确可约简的边界情况下才交给 exact 最小表达。

整理器不能因为局部选区替换改变选区外文本的解析时值。必要时可以追加恢复用
`{N}`，但只有当选区后方确实会继续消费当前 subdivision 时才追加。

## 2. 术语

### 2.1 384 网格

一个小节以 `384` 作为内部格式化网格。`N` 是 384 的约数时，`{N}` 的每个逗号
长度都能被 384 精确表达。

例如：

- `{32},` = `1/32` 小节，能精确表达，因为 `384 / 32 = 12`
- `{5},` = `1/5` 小节，不能精确表达，因为 `384 / 5 = 76.8`

### 2.2 特殊分音段

特殊分音段是显式 `{N}` 开始、且 `N` 不是 384 约数的分音段。

段的结束点是下列任一事件：

- 下一个 `{M}`
- `(BPM)`
- 合法 `|| x/y` 拍号控制
- 普通 `||` 注释
- `<HS*...>`
- `E`
- 选区或文本结束

`非空` 不再按“段内是否有 note”判断，而按“段是否消费过逗号”判断。一个只有
休止逗号的 `{N},,,` 仍然是非空特殊分音段。

### 2.3 当前拍号半拍网格

半拍网格按当前 meter 计算。若当前 meter denominator 为 `D`，半拍网格单位为：

```text
1 / (2 * D) whole
```

4/4 下半拍网格是 `1/8` 小节；3/4 下仍按当前 denominator 得出 `1/8`
whole，但小节长度为 `3/4`。

判断特殊分音段是否“起点和终点都在半拍网格线上”时，使用当前小节相位，而不是
固定 4/4 全局网格。

## 3. `reduceTo384Grid=true` 目标行为

`reduceTo384Grid=true` 的语义是：

```text
原始时间 -> 近似到 384-grid 时间 -> 输出精确表达该 384-grid 时间
```

它不是：

```text
原始时间 -> 空段用默认 {16} 表达
```

### 3.1 允许的误差

非 384 分音允许近似。例如 `{5},` 的长度 `1/5` 无法被 384 精确表达，可近似为：

```text
round(384 / 5) / 384 = 77 / 384
```

这个误差属于 `reduceTo384Grid=true` 的允许范围。

### 3.2 不允许的误差

已经能被 384 精确表达的时值不能被改写成其他长度。

示例：

```text
输入片段：{32},{1},
原始时值：1/32 + 1
```

`1/32` 能被 384 精确表达，因此整理结果不能把它变成 `{16},`：

```text
错误结果：{16},,,, ,,,, ,,,, ,,,,
          {16},
```

其中最后 `{16},` 是 `1/16` 小节，时值被放大。

正确策略是：approximate renderer 在为每个 segment 选择 `{N}` 时，必须把
segment 自身长度的 denominator 纳入约束。若 segment 长度是 `1/32`，输出
subdivision 至少要能精确表达 `1/32`。

## 4. `reduceTo384Grid=false` 目标行为

`reduceTo384Grid=false` 的语义是：不把非 384 分音吸附到 384 网格。特殊分音段
在大多数情况下保留显式 `{N}`，但有一个可约简例外。

### 4.1 特殊分音段不在半拍网格上

如果特殊分音段的起点或终点任一不在当前拍号半拍网格线上：

1. 在段起点前结束当前小节布局。
2. 特殊分音段从相位 0 开始渲染。
3. 段内强制保留原显式 `{N}`。
4. 段终点后重新开始后续小节布局。

这就是“该特殊分音段的起点和终点会重置小节线”。

示例：

```text
{5},
```

`1/5` 不在 4/4 半拍网格线上，因此即使段内只有休止，也保留 `{5},`，并作为
独立布局段处理。

### 4.2 特殊分音段在半拍网格上

如果特殊分音段的起点和终点都在当前拍号半拍网格线上：

1. 不重置小节线。
2. 不强制保留原 `{N}`。
3. 交给 exact 最小表达规则决定是否约简。

这意味着段内起点 note 不会阻止约简，因为起点本身是段边界，不需要特殊分音来
表达。

示例：

```text
输入：{10}1,,,,,
```

长度为 `5/10 = 1/2` 小节，起终点都在半拍网格线上；note 在段起点。可约简为：

```text
{2}1,
```

但如果 note 落在段内部：

```text
输入：{10},1,,,,
```

note 相对段起点位于 `1/10`，`{2}` 无法表达该内部落点。exact 最小表达会保留
能表达 `1/10` 的分音，结果应保持类似：

```text
{10},1,,,,
```

因此，本节的核心判断不是“段内是否为空”，而是“段内是否存在需要特殊分音表达的
内部落点”。

## 5. 选区尾部 `{N}` 恢复逻辑

选区整理会消费选区内的原 `{N}` 并重新输出格式化后的 `{N}`。如果只替换选区，
新的 subdivision 状态可能影响选区后的未整理文本。

恢复用 `{N}` 只在下列条件同时成立时追加：

1. 选区内没有 `E`
2. 选区后方存在会继续消费当前 subdivision 的文本

选区后方的判断规则：

- 只剩空白：不追加
- 先遇到 `E`：不追加
- 先遇到 `{N}`：不追加
- 先遇到逗号或普通 note 内容：追加
- 普通 `||` 注释不应让注释内的 `{}` 影响判断；它只作为文本边界处理

示例：

```text
全文：{1},
选区：全文
```

因为选区后方为空，不追加 `{1}`。

```text
全文：{1},
     E
选区：第一行
```

因为后方先遇到 `E`，不追加 `{1}`。

```text
全文：{1},2,
选区：{1},
```

因为后方的 `2,` 会继续消费当前 subdivision，需要追加恢复用 `{1}`。

## 6. 远程提交处理

远程 `origin/dev` 上的提交：

```text
24ecdce9 incomplete modification of the normalization function to handle edge cases and improve performance.
```

该提交尝试给 `RenderMeasure` 增加 `subdivisionLcmHint`，并把见过的 `{N}` 取 LCM，
用于 incomplete measure 的空段精度保留。

可吸收的点：

- rest-only / incomplete segment 需要记录分音上下文，不能只看 note moment
- 空段 fallback 到 `{16}` 是导致时值漂移的根因之一

不能直接合并的点：

- 全片段 `{N}` LCM 粒度过粗，会让无关分音影响后续 measure
- trailing carry 从 `currentBeats` 改成全局 LCM，会过度恢复，违背“只恢复后文要消费的状态”
- 该实现没有表达本规格的半拍对齐、特殊段 reset、起点 note 可约简等规则

实现时应基于 `origin/dev`，保留远程提交中“需要记录 rest 段分音信息”的方向，
但替换为本规格定义的结构化 segment policy。

## 7. 修改计划

### 7.1 新增策略文件

新增：

```text
src/core/chart/transform/ChartNormalizationSegmentPolicy.h
src/core/chart/transform/ChartNormalizationSegmentPolicy.cpp
```

该文件负责 validation 通过后的核心布局判断，包括：

- trailing `{N}` 是否需要追加
- approximate segment 的输出 subdivision 是否能精确表达 segment 长度
- 特殊分音段识别
- 特殊分音段半拍网格对齐判断
- special segment 是否强制保留原 `{N}`
- special segment 是否在起点/终点插入 reset boundary

`ChartNormalization.cpp` 保留扫描与渲染编排，但把上述判断调用到新策略文件。

### 7.2 扩展扫描模型

扫描 `{N}` 时记录显式 subdivision segment：

- `beats`
- 起点相位
- 终点相位
- 是否消费过逗号
- 段内 moment 相对位置
- 是否由控制边界结束

这份信息用于 exact policy，不用于 parser marker。

### 7.3 修正 approximate renderer

为每个 segment 选择 `{N}` 时，除了 moment snap q，还要合并 segment length
denominator。`slotCount` 必须用 exact scale；若当前 `{N}` 不能精确表达该 segment
长度，应提升到能表达的 subdivision，最高可退到 384。

### 7.4 修正 exact renderer

`reduceTo384Grid=false` 下，进入 exact 的条件不再只看 `MeasureMoment`：

- 特殊分音段存在
- measure length 非 384 可表达
- start phase 非 384 可表达
- beat boundary 非 384 可表达
- moment position 非 384 可表达

特殊分音段按第 4 节规则插入强制保留或允许约简。

### 7.5 修正选区尾部恢复

用 `followingTextNeedsBeatsCarry(...)` 替代当前的
`followingTextRedefinesBeatsBeforeUse(...)`。

## 8. 规格测试

在 `src/tools/chart_transform/ChartBatchTransformSpec.cpp` 增加 inline specs。

### 8.1 trailing `{N}`

```text
normalizeChartSelectionText("{1},", 0, len)
=> 不追加尾部 {1}
```

```text
normalizeChartSelectionText("{1},\nE", 0, before_E)
=> 不追加尾部 {1}
```

```text
normalizeChartSelectionText("{1},2,", 0, after_first_comma)
=> 追加恢复用 {1}
```

### 8.2 approximate 时值守恒

```text
normalizeChartText("{32},{1},")
=> 尾段保持 1/32；不能输出 {16},
```

```text
normalizeChartText("{24},{1},")
=> 尾段保持 1/24；不能被空段默认 {16} 放大
```

```text
normalizeChartText("{5},")
=> 允许近似到 384 grid，但输出必须精确表达近似后的 384-grid 长度
```

### 8.3 exact 特殊分音

```text
normalizeChartText("{5},", reduce=false)
=> 保留 {5},，并作为 reset 段
```

```text
normalizeChartText("{10}1,,,,,", reduce=false)
=> 可约简为 {2}1,
```

```text
normalizeChartText("{10},1,,,,", reduce=false)
=> 保留能表达内部 1/10 落点的分音，不能约简为 {2}
```

```text
normalizeChartText("{4},{10},,,,,{4},", reduce=false)
=> {10} 段起终点在半拍网格上，允许 exact 约简，不重置小节线
```

```text
normalizeChartText("{4},{5},{4},", reduce=false)
=> {5} 段不在半拍网格上，段前段后重置，并保留 {5}
```

### 8.4 回归

- 普通 384-grid 谱面输出不变
- `{7}` / `{28}` 在 `reduce=true` 下仍走 `snapXOverY`
- duration rewrite 规则不变
- 普通 `||` 注释仍拆到独立行并保留书签语义
- BPM / 拍号边界仍重启 section 计数
- slide `*` 分支上的 `b` 不被移动
