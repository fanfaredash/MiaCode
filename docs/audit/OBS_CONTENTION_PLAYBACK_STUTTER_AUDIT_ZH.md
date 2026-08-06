# OBS 推流下预览播放卡顿审查（问题 2）+ 与空闲冻结的关联重构

- 日期：2026-08-04
- 基准：`dev` `677a9625`（`1.1.0-beta.7`）；故障版本 `1.1.0-beta.2`
- 关联文档：`WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md`、`docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md`

---

## 0. 最重要的一条：这两个问题可能不是两个问题

新反馈把问题 1 的场景改写了：

| | 原假设 | 用户实际 |
|---|---|---|
| 屏幕 | 黑屏 / 休眠 | **未黑屏、未锁定** |
| 窗口 | 前台放置 | **最小化 / 被浏览器挡在后面** |
| 机器负载 | 空闲 | **浏览器持续播放视频网站** |
| 时长 | 2 小时 | **5～10 分钟** |

问题 2 的场景是：**OBS 推流**时预览播放卡顿，密度阈值下降 10 倍。

两者的公因子是**第三方持续占用 GPU/CPU**，而不是「空闲」。

> **这个 bug 大概率不是「空闲 bug」，而是「争用 bug」。**
> 应用是空闲的，**机器不是**。原审计、我的复核和 worktree 复现套件都默认了「空闲 = 安静的机器」，而用户的真实条件是「安静的应用 + 繁忙的机器」。
> 5～10 分钟这个时间尺度也印证这点——那是主动争用的量级，不是缓慢漂移的量级。

因此问题 1 与问题 2 应当**合并成一条线索追**，本文的候选机制对两者同时适用，只是烈度不同。

---

## 1. 硬件前提（决定性，必须先算清楚）

来自 worktree 文档确认的用户机器：

| 部件 | 规格 | 本问题中的含义 |
|---|---|---|
| CPU | i5-1155G7 | **4 核 8 线程、15–28 W 移动端**。OBS 若用 x264 软编 1080p，可吃满全部核心 |
| dGPU | **NVIDIA MX450，2 GB VRAM** | 入门级；**2 GB 是本问题的核心约束**。NVENC 编码也在这块卡上 |
| iGPU | Intel Iris Xe | DXGI 默认适配器，预览合成表面所在 |
| RAM | 16 GB | — |

叠加 O-1（高性能 GPU 绑定默认开启，且该机为双显卡故**实际生效**）后的资源分布：

- **根窗口 → MX450（2 GB）**
- **预览合成表面 → Iris Xe（默认适配器，必须留在这里以维持 D3D11VA 同适配器桥）**
- OBS 若用 NVENC → **也在 MX450**
- 浏览器视频硬解 → 视策略可能落在任一块

即：**MiaCode 自己就横跨两块 GPU，而 OBS 的编码器与它的根窗口挤在同一块 2 GB 卡上。**

`QuickShellPreviewCompositeSurface.cpp:44-47` 的注释已经明确承认这个分裂是**已知并被接受的风险**：

> `Record the adapter this surface actually landed on, so a support log can spot inconsistency against the root window (plan P4.3 exposes adapter divergence as a log risk rather than force-merging surfaces in v1).`

同文件 `view_->setPersistentGraphics(true)` / `setPersistentSceneGraph(true)`：**窗口隐藏/最小化时不释放图形资源**。对 2 GB 卡而言，这意味着最小化期间预览纹理仍然常驻。

---

## 2. 为什么「10 倍」是关键信号

`(180){32}` 的单点流约为 **24 note/s**。用户称平时阈值是这个的 10 倍。

**10 倍不是线性劣化。** 纯 CPU 争用通常给出 2～3 倍的劣化，不会给出 10 倍。10 倍意味着**掉下了某个悬崖**——从快路径落到慢路径，或撞上了某个每帧同步点。

这个判断直接决定了候选排序：优先怀疑**有阈值行为的机制**（VRAM 驱逐、调度剥夺、present 背压），而不是「CPU 不够用」这类平滑劣化。

---

## 3. 候选机制

### C-1 MMCSS 保底在多进程争用下失效（高）

`PreviewQuickSceneRoot.cpp:775-807` 把 QSG 渲染线程注册为 MMCSS `Games` 任务类。该处注释写得非常明白，且**直接描述了本问题的症状**：

> `the standard fix used by every native rhythm game and DAW on Windows for the same bimodal-stall pattern we're hitting (render thread occasionally gets scheduled out for a full quantum, surfacing as render_submit_ms = 22ms with pre_render_wait_ms ≈ 0)`

**即：仓库已经遇到过一次「渲染线程被整个时间片调度出去」的双峰停顿，并用 MMCSS 修掉了。**

问题在于 MMCSS 是**系统级共享预留**（`SystemResponsiveness` 默认给 MMCSS 约 80% CPU）。OBS 自身也会注册 MMCSS 任务（采集/编码/音频），浏览器视频播放同样会。在 **4 核** 机器上，多个进程同时持有 MMCSS 预留时，boost 的实际效果被稀释——**MMCSS 保底失效，被它修掉的旧 bug 原样回归**。

这条最能解释 10 倍悬崖：不是渐进变慢，是保护机制被打掉。

**日志特征（代码注释已给出）：`render_submit_ms` 大 + `pre_render_wait_ms` ≈ 0。**

覆盖范围限制：MMCSS **只注册了 QSG 渲染线程**。GUI 线程、BASS 音频线程、SFX 线程都没有任何优先级保护。

### C-2 DXGI flip-queue / present 背压（高）

`PreviewQuickSceneRoot.cpp:1381-1385` 注释：

> `pre_render_wait_ms = gap between afterSynchronizing and beforeRendering. In Qt's threaded RHI render loop the swap-chain image acquire happens here, so a fat value is the smoking gun for vsync / DXGI flip-queue back-pressure`

OBS 的显示/窗口/游戏采集会介入 present 路径；叠加 MiaCode 双适配器 + DWM 跨适配器合成，present 延迟增长。

**日志特征：`pre_render_wait_ms` 大。**

### C-3 2 GB VRAM 耗尽 → DXGI 驱逐（高，且最能解释 10 倍）

MX450 只有 2 GB，其上同时承载：MiaCode 根窗口（因 O-1 绑定）、OBS NVENC 编码器与采集缓冲、可能还有浏览器视频。`PreviewTextureRepository` 持有纹理缓存，且 `setPersistentGraphics(true)` 在最小化时**不释放**。

VRAM 一旦超出预算，DXGI 会把资源驱逐到系统内存，此后**每帧都要走 PCIe 重新上传**。这是典型的悬崖式劣化，量级与「10 倍」吻合，也能解释问题 1：最小化后浏览器视频抢占 VRAM，MiaCode 的资源被驱逐，恢复时需要大量重新上传。

**好消息：查询能力已经存在。** `TimelineQuickItem.cpp:120-133` 已经在用 `IDXGIAdapter3::QueryVideoMemoryInfo` 同时查询 `LOCAL` 与 `NON_LOCAL` 段（`gpu_kb`）。目前只在时间轴泄漏计量表里用，**需要的只是把它改成按适配器周期采样**。

**日志特征：`CurrentUsage` 逼近或超过 `Budget`；`swap_gpu_ms` 大。**

### C-4 密度相关的 paint-node 构建被 CPU 饿死（中）

密度↑ → sprite 数↑ → `paint_ms` / `layer_sum_ms` 线性增长。OBS x264 软编在 4 核机上会抢走大部分 CPU。

但这是**平滑劣化**，单独不足以解释 10 倍，更可能是叠加因素。

**日志特征：`paint_ms` / `layer_sum_ms` 大，`top_layer` 指出具体层。**

### C-5 音频路径完全没有仪表（中，且是盲区）

若「卡顿」是**声音**卡顿，当前**查不到任何东西**：

- `src/audio/` 下搜不到任何 underrun / stall / starvation / 缓冲水位诊断；
- BASS 更新线程**没有** MMCSS 注册（MMCSS 只加在 QSG 渲染线程上）；
- 未发现 SFX 并发voice 上限。

高密度谱面意味着高频 SFX 触发；CPU 争用下音频线程欠载会直接表现为播放卡顿，而**日志里不会留下任何痕迹**。

---

## 4. 判别树：一条现成的日志行就能分开上述所有分支

`PreviewQuickSceneRoot.cpp:1413-1428` 的 `render_frame_profile` 已经把一帧拆成了各阶段，并且**对每个 ≥30 ms 的慢帧自动记录**（`slow=1`），不只是采样：

```
total_ms= paint_ms= sync_ms= pre_render_wait_ms= render_submit_ms=
swap_gpu_ms= layer_sum_ms= top_layer= top_layer_ms= layer_count= slow=
```

| 哪一项大 | 指向 |
|---|---|
| `render_submit_ms` 大，`pre_render_wait_ms` ≈ 0 | **C-1 MMCSS 失效**（代码注释给出的原始签名） |
| `pre_render_wait_ms` 大 | **C-2 present 背压** |
| `swap_gpu_ms` 大 | **C-3 GPU 执行 / VRAM 驱逐** |
| `paint_ms` / `layer_sum_ms` 大 | **C-4 CPU 密度** |
| 全都不大，但用户听到卡顿 | **C-5 音频**（当前无仪表） |

**开关：`MIACODE_PREVIEW_FRAME_PACING_DIAG=1`（配合 `MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS`，默认 1000）。**

这是本轮最重要的结论之一：**问题 2 有一个专门为它写的诊断，已经在仓库里，但没有人被要求打开它。** 在加任何新埋点之前，先把它打开跑一轮。

---

## 5. 仪表盲区（需要补的）

按性价比排序：

1. **音频欠载 / 缓冲水位**（C-5）—— 当前完全为零。若卡顿是听觉上的，现有全部埋点都看不到。
2. **按适配器的周期性 VRAM 采样**（C-3）—— 查询代码已存在于 `TimelineQuickItem.cpp:120-133`，只需提到共享位置并按 30 s 周期对**两块适配器**分别采样 `Budget` / `CurrentUsage`。
3. **窗口遮挡 / 最小化状态转换** —— `DXGI_STATUS_OCCLUDED` 只在 `PreviewDCompCore.cpp:517`（**DComp 路径，默认关**）被处理。默认 QSG 路径**没有任何遮挡感知**，也没有可见性转换日志。问题 1 的核心条件恰恰是「被遮挡/最小化」，目前不可观测。

   > **2026-08-04 确认：用户使用的均为默认路径，其余路径已弃用且后续不再维护。**
   > 这条约束反而**加重**了本项：DComp 路径里那套遮挡处理对用户永远不会生效，所以默认 QSG 路径的遮挡感知空白是**真实且完全的**，不能拿 `PreviewDCompCore.cpp` 的存在当作「已处理」。
   > 同理，`PreviewDCompRenderer.cpp:255` 的 MMCSS 注册也对用户无效——**唯一生效的是 `PreviewQuickSceneRoot.cpp:798`**（C-1 只应按这一处分析）。
   > 补埋点时不要往 `src/render/backend_d3d11/` 或 `src/render/` 添加任何内容。
4. **MMCSS 注册状态的后续变化** —— 目前只在注册时记一次。需要能看出运行中是否被降级/丢失。

---

## 6. 验证方案

### 阶段 A — 先用现有开关跑一轮，不改代码

```
MIACODE_PREVIEW_FRAME_PACING_DIAG=1
--debug
MIACODE_LOG_DIR=<宽裕路径>
```

四组，同一谱面、同一密度、同一时长：

| 组 | OBS | 浏览器视频 | 目的 |
|---|---|---|---|
| A1 | 关 | 关 | 基线，确认平时阈值 |
| A2 | **开** | 关 | 复现问题 2 |
| A3 | 关 | **开** | 逼近问题 1 的争用条件 |
| A4 | 开 | 开 | 最坏情况 |

每组收 `render_frame_profile` 的 `slow=1` 行，按第 4 节判别树读。**A2 相对 A1 哪一项膨胀，就是根因所在的分支。**

同时记录 OBS 的编码器设置（**x264 还是 NVENC**）——这决定争用发生在 CPU 还是在 MX450 上，是关键变量，必须记录而不是事后猜。

### 阶段 B — O-1 的 A/B（同样适用于问题 2）

`MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` 会把根窗口拉回 Iris Xe：**取消跨适配器分裂，并让根窗口离开 2 GB 的 MX450**（Iris Xe 用共享系统内存，无 2 GB 硬上限）。

若 C-2 或 C-3 成立，这一组应当有明显改善。**这一个开关同时检验问题 1 和问题 2**，优先级很高。

### 阶段 C — 按阶段 A 的结论补埋点

只补判别树指向的那一类，不要一次全加（见第 5 节清单）。

---

## 7. 与问题 1 复现套件的关系

`docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md` 当前的优先级矩阵把「显示器 5 分钟后关闭，允许休眠与唤醒」列为第 1 组、`Win+L` 列为第 3 组。**按新反馈，这两组测的都不是用户的实际场景**，应重排为：

| 新顺序 | 条件 | 时长 |
|---|---|---|
| 1 | **最小化 / 被浏览器遮挡 + 浏览器持续播放视频** | 5–15 min |
| 2 | 同上，`MIACODE_GPU_BIND_HIGH_PERFORMANCE=0` 对照 | 5–15 min |
| 3 | 前台放置 + 浏览器播放视频（分离「遮挡」与「争用」两个变量） | 5–15 min |
| 4 | 原显示器休眠 / 锁屏组（降为回归项，不再是首选） | 2 h |

时长从 2 小时降到 5–15 分钟，**单轮成本下降一个数量级**，可以跑更多对照。

---

## 8. 边界

本文是静态审查 + 硬件约束推算，**没有实机复现数据**。C-1/C-2/C-3 都有代码或注释层面的直接证据支撑其**存在**，但哪一条是问题 2 的主因，必须由阶段 A 的 `render_frame_profile` 数据判定。第 2 节「10 倍意味着悬崖」是推理，不是测量。

OBS 编码器类型（x264 / NVENC）目前**未知**，而它决定争用落在 CPU 还是 MX450 上——这是取证时必须先问清的一项。
