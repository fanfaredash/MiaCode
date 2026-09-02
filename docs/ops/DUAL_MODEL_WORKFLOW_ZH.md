# 双模型工作流：Sonnet 5 执行 + Opus 5 推理

目的：把**读代码 / 搜索 / 跑命令 / 改代码 / 跑测试**这些吃 token 但不吃推理的活交给 Sonnet 5，
把**架构设计 / 跨模块决策 / 疑难 Bug 根因**交给 Opus 5，两者之间只传压缩过的 Packet，
不传完整项目上下文。

## 两种拓扑

| | 主会话 | 子代理 | 何时用 |
|---|---|---|---|
| **A（默认）** | Opus 5 编排 + 推理 | `miacode-executor` / `miacode-investigator`（Sonnet） | 会话本来就开在 Opus 上 |
| **B** | Sonnet 5 驱动 | `miacode-architect`（Opus） | 会话开在 Sonnet 上 |

**A 是默认**。两者的经济性一样——吃 token 的文件读取、构建日志、测试输出都落在 Sonnet
的上下文里，Opus 只见到 Packet。差别在连续性：A 里编排者跨整个阶段持有计划和已做决策，
B 里每次调 Opus 都是冷启动，多阶段主线会反复重建同一批背景。
所以长主线（例如阶段 4.5–4.9）用 A，孤立的疑难问题用 B。

切换：`/model` 改主会话模型即可，两套 agent 定义都在 `.claude/agents/` 里常驻。

## 角色

- `miacode-executor`（Sonnet，可写）——按 Execution Packet 改代码、构建、跑测试，回传 Result Packet。
  不做架构决策。
- `miacode-investigator`（Sonnet，只读）——定位根因、盘点调用链、核对文档与代码，回传证据链。
  绝不改文件。
- `miacode-architect`（Opus，只读）——拓扑 B 下接收 Task Packet，回传结构化方案。

## Packet 规范

三种 Packet，共同的硬规则：**只放真正影响判断的信息**。不要为了"让对方有背景"而复制大段代码；
对方拿不准时会回来要，那比一次性灌满便宜。

### Execution Packet（编排者 → Sonnet）

```
## 目标
一句话：这次要达成什么

## 已定结论
设计决策（这是输入，不是建议；不同意就升级，不要自行改道）

## 动手范围
明确要改的文件；明确不要碰的文件

## 约束
除仓库通用硬约束外，本任务特有的限制

## 验收
可执行的判定条件（命令 + 期望输出）

## 交付
Result Packet；是否提交（默认不提交）
```

### Escalation Packet（Sonnet → Opus，上限 3K token）

卡在哪 / 现场证据（逐字报错 10–30 行）/ 相关代码（≤60 行）/ 已排除 / 需要裁决的点。
格式见 `.claude/agents/miacode-executor.md`。

### Task Packet（拓扑 B：Sonnet → `miacode-architect`，1–3K token）

目标 / 关键代码 / 核心约束 / 已尝试方案 / 待解决问题。
回传格式（判断 / 依据 / 方案 / 取舍 / 验收 / 风险）见 `.claude/agents/miacode-architect.md`。

## 什么时候该升级到 Opus

**该升级**：跨模块契约要改；同一状态有两个所有者要定谁是权威；同一处修了 2 次仍不通；
根因指向架构而不是某一行；需要在两个都能跑通的方案之间做取舍。

**不该升级**：路径 / 符号 / 调用方查找；照着已定方案做机械改动；构建报错照着编译器提示修；
补测试；跑验证。这些直接让 Sonnet 做完。

## 反模式

- 把完整项目上下文塞进 Packet——这套流程存在的意义就是不这么干。
- Sonnet 撞到设计分歧后"先按自己的理解改了再说"——必须升级。
- Opus 越过 Packet 自己去把仓库重读一遍——补读控制在 5 个文件以内。
- 用 `-R` 挑子集充当"测试通过"——阶段 4.8/4.9a 就是这么把
  `debug_flag_index_spec` 红着带过两个提交的。跑全量。
