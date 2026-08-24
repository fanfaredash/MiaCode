# PV 首播画面掉帧审计与交接报告

- 审计日期：2026-08-19（Asia/Shanghai）
- 审计范围：本轮对话中用户报告的「冷启动或首次播放带 PV 谱面时，音频继续而 PV/谱面画面掉帧或短暂静止」问题。
- 当前状态：**已定位到一次可量化的 QSG/D3D11 渲染提交阻塞；尚未定位到该阻塞内部的具体驱动或管线调用，未实施根治修复。**
- 当前代码基线：`0e85b0b2`（`dev`）；现场 beta.14/beta.15 均记录为 `git_dirty=1`。beta.15 的诊断代码和版本号改动尚在工作区，交接前必须拆分、审阅并提交，不能把“包已验证”误当作“提交已落库”。
- 后续复核与修复：`docs/audit/PREVIEW_FIRST_PLAY_RENDER_STALL_FIX_PLAN_ZH.md`（§5.1 的 `render_submit` 相位拆分、§5.2 的假 EOM 定性/取证/有界恢复、§5.4 的预热语义修复与首绘可见性、§6C 对照实验矩阵；并纠正了本文把 `position_ms≈duration` 当作独立证据的读法）。
- 相关旧审计：`docs/audit/PREVIEW_PLAYBACK_STUTTER_AUDIT_ZH.md`。该文解决的是此前已知的时间轴冻结、重复 commit seek、烟花预热每帧重建和音频资源竞态；本文记录其后仍存在的**首播 render-submit 长阻塞**。

---

## 0. 交接结论

### 已证实

在 beta.15、`--debug` 和 `MIACODE_PREVIEW_FRAME_PACING_DIAG=1` 均生效的一次现场复现中（PID `80340`）：

1. 首播事务在 `13:21:57.424Z` 正常 commit，`prepared_ready=1`、`prepared_landing_confirmed=1`、`reseek=0`。
2. 随后画面在视觉秒 `0.432882` 处等待了 **1014 ms**；此时记录的权威音频秒已到 `1.446208`。所以不是“播放整体停住”，而是音频继续、视觉没有及时 present。
3. 对应渲染帧总长 `1011.596 ms`，其中 `render_submit_ms=1005.612 ms`。绘制 CPU 时间只有 `paint_ms=4.689`、烟花图层本体 `1.631 ms`、`swap_gpu_ms=1.284`。
4. 同一时间窗中，双 D3D11 设备视频桥接的最大单帧时间为 `1.867 ms`，所有分项均为亚 2 ms；它不可能解释这一秒级阻塞。

因此当前最可靠的故障类别是：**Qt Quick Scene Graph / D3D11 在首播期间提交渲染命令时，被 GPU 驱动、首次管线编译、设备/队列同步或同类底层工作阻塞。**

这不是已证实的“烟花 CPU 计算慢”、音频素材首次同步加载慢、视频共享纹理创建慢、互斥锁超时，或普通 Present/vsync 等待慢。

### 同时发现的独立高优先级问题

同一 PID 在首播约 1.446 秒时收到：

```text
media_status status=EndOfMedia state=StoppedState position_ms=121066
active_elapsed_ms=1446 frames=38 last_pts_ms=1267
```

PV 实际时长约 121 秒，但只活跃 1.446 秒、最后视频 PTS 约 1.267 秒就报到了末尾；这不可能是自然播放到文件末尾。当前代码会把 PV 当作从属视觉媒体，所以不会终止 BGM/谱面 transport（现场也确认 `bgm_running=1`、`master_running=1`）；但它可能令 PV 过早停在最后一帧。该“假 EndOfMedia”与一秒渲染阻塞的发生顺序尚无法确认，**必须作为并行缺陷继续追查，不能直接当作本次一秒 render-submit 卡顿的根因。**

### 当前不应做的修复

- 不应仅因 `two-device` 路径存在而改成单设备路径。现场桥接最大 1.867 ms，和 1005 ms 的提交阻塞不匹配；改变解码/渲染设备拓扑有兼容性与回归风险。
- 不应把烟花图层 1.631 ms 当作一秒卡顿的根因，也不应简单撤回现有烟花预热修复。
- 不应恢复“首次按播放时同步加载 BASS/SFX”的旧路径；现有提交已把这部分移出 GUI 线程，且现场 commit 没有重复 seek。
- 不应靠增加更多常开逐帧日志来“碰运气”；当前诊断已经证明桥接不是主耗时。下一步要测 QSG/驱动提交链，而不是继续细分桥接。

---

## 1. 问题定义与证据分级

### 1.1 用户现象（用户陈述）

- 冷启动后直接播放最容易复现；多次播放后概率明显下降，疑似受系统/驱动缓存影响。
- 音频与音效正常播放，PV 与谱面渲染内容在刚开始时掉帧或短暂静止。
- 无 PV 时无法触发或显著难以触发。
- 不保证每次触发；有时为“整个程序卡两三秒”的主观感受，后续澄清为**视觉帧掉落而非播放 transport 停止**。

用户陈述是定位边界的高价值信息，但不单独构成代码归因证据。

### 1.2 证据等级

| 等级 | 含义 |
|---|---|
| A | 当前工作区可复核的原始日志，且有时间戳/进程号/指标闭环。 |
| B | 当前可复核日志支持现象，但缺少关键分项、无法单独归因。 |
| C | 本轮对话中已分析或由用户说明，但原始文件当前不在工作区，不能重新核验具体行。 |
| D | 合理假设或待验证方案，不能作为根因结论。 |

---

## 2. 每轮日志与结论台账

> 说明：当前 `C:\Users\admin\Desktop\logs` 中仅保留 `logs6`、`logs7`、`logs8` 和两个最终压缩包。对话中更早的 `logs`、`logs (1)`、`logs2`、`logs3`、`logs4`、`logs5` 原始文件已不在该目录或为空，以下只保留当时的对话结论，明确标为 C 级，避免把记忆性结论写成可复核日志事实。

| 轮次 / 材料 | 现场状态与记录 | 本轮排除 / 降级的方向 | 仍保留的问题 | 等级 |
|---|---|---|---|---|
| R0：`logs` | 用户在“首次播放卡顿”既有修复后仍反馈卡顿。原始文件未保留。 | 不能据此排除任何运行时路径。 | 首播、PV、音频、烟花、渲染均仍是候选。 | C |
| R1：`logs (1)`、`logs2` | 用户补充：可稳定复现、进程序/首播会卡、没有 PV 不触发。原始文件未保留。 | “纯音频文件/纯谱面逻辑即可触发”的假设被显著降级。 | PV 首帧、视频解码/纹理、首个 GPU 渲染状态、PV 关联的场景初始化。 | C |
| R2：`logs3` | 用于压缩用户测试步骤与打包后的反馈；原始文件未保留。 | 无独立可复核的技术排除项。 | 需将低噪诊断限制到首播窗口，避免测试本身干扰问题。 | C |
| R3：`logs4` | 用户补充：非必现，频次高后概率下降，符合缓存特征。原始文件未保留。 | “每次都会发生的确定性业务逻辑死锁”被降级。 | 驱动/着色器缓存、资源首次创建、磁盘/防病毒缓存、后台负载。 | C |
| R4：`logs5` | 用户说明：未开环境变量、同时开很多程序；第一次播放 PV 与谱面都卡。原始文件未保留。 | 不能将新诊断开销视为复现前提；问题在无诊断时已存在。 | 系统资源争用可能放大问题，但不是已证实根因。 | C |
| R5：`logs6` | beta.14、AMD Radeon、QtAV D3D11VA、`single_device=0`。PID `74728` 记录首播事务 `reseek=0`。 | 已排除“commit 又做一次同目标 seek”作为该次 1.2 秒卡顿原因；烟花图层 CPU 计算也明显不足以解释秒级阻塞。 | QSG/D3D11 提交、驱动首次编译、PV/解码引发的 GPU 队列状态。 | A |
| R6：`logs7` | 混合了旧 beta.14 运行时日志和 beta.15 启动信标。beta.15 PID 只出现在 beacon，运行时/音频正文没有对应 PID。 | 不能用它判断 beta.15 诊断是否失效；此前认为“没有新字段”的结论是采集不完整造成的，不应保留。 | 日志重定向与收集路径。 | B |
| R7：`logs8` | beta.15 正确运行、`--debug` 正确；但不含 `first_playback_bridge_trace` 和分项计时，硬解摘要仍是简略字段。 | 只能确认两设备路径、无 `acq_timeout` / `copy_fail`，不能归因性能。 | 诊断变量是否生效、根目录/工程目录哪一个是最终日志目录。 | B |
| R8：`logs根目录.zip` | 用户明确在 CMD 中执行 `set MIACODE_PREVIEW_FRAME_PACING_DIAG=1&&call Start_MiaCode_Debug.bat`。PID `80340` 记录 `env_log_dir_present=1`、`action=arm` 和 `action=render_stall`。 | 直接排除双设备桥接、纹理创建、共享资源打开、AcquireSync 超时、普通 CPU 图层绘制为这一次 1 秒卡顿的主因。 | render-submit 内部成因、假 EOM、GPU 驱动/队列/PSO、其他首播状态。 | A |
| R8：`logs谱面文件夹.zip` | 含 PID `50612` 等较早工程内日志，没有 PID `80340` 的完整现场链。原因是 R8 的 `MIACODE_LOG_DIR` 已把当前运行时日志固定到包根 `logs/`。 | 不应再要求用户从工程 `.miacode/logs` 找 R8 的关键字段。 | 日志收集说明需要明确“以 `runtime_log_path` 为准”。 | B |

### 2.1 R5（`logs6`）的关键复核

文件哈希：

- `miacode_runtime_debug.log`：`8E9BD88EDE7CFD8273FA0A92B385B37DF4E42432E6681DCF22077F55BE7C8917`
- `miacode_audio_debug.log`：`9AF0FC51FD59E8E29BCC665C543F39B614F85A39EEC4A0B3438DC319176A8F6B`

PID `74728`（beta.14）的时间线：

```text
19:07:42.425  commit_prepared_playback ... prepared_ready=1 ... reseek=0
19:07:42.860  firework_pso_warmup_done ... present_count=59
19:07:44.059  render_frame_profile total_ms=1203.564
              paint_ms=3.716 render_submit_ms=1197.075 swap_gpu_ms=2.754
              top_layer=judge_firework top_layer_ms=1.466
19:07:44.071  fixed_gate_watchdog_kick wait_ms=1211
              playhead=0.433724 audio_second=1.644166
```

另有 PID `68944` 的 `video_frame_stall_begin age_ms=145`，8 ms 后结束。这是一次短视频帧间隔异常，与 PID `74728` 的 1.2 秒 render-submit 阻塞不同；不能混为同一根因。

### 2.2 R8（最终压缩包）的关键复核

压缩包哈希：

- `logs根目录.zip`：`F70301128C1DB412687B0197751FDF7EF7CFFC013EF6A22E0484F56FD19949F0`
- `logs谱面文件夹.zip`：`52B0D9247F70E654F0E776187477FDEC68C8C07FC66658EB55C9F1BB10F36798`

R8 运行环境：beta.15、Qt 6.8.3、QtAVPlayer/FFmpeg、D3D11VA 硬解、Qt Quick RHI API = Direct3D11、适配器 = `AMD Radeon(TM) Graphics`。图表为 `狂水一華`，PV 为 H.264、`864x480` coded / `854x480` display。日志显示使用 `two-device` 路径。

PID `80340` 的完整因果顺序：

```text
13:21:57.419  start_request txn=1 has_video=1
13:21:57.421  prepare_playback_ready ... source=queued_frame
13:21:57.424  canvas_presented -> commit_prepared_playback ... reseek=0
13:21:57.424  first_playback_bridge_trace action=arm txn=1
13:21:57.446  hwframe_path path=two-device
13:21:57.857  firework_pso_warmup_done reason=drawn present_count=60
13:21:58.863  render_frame_profile total=1011.596 render_submit=1005.612
13:21:58.870  fixed_gate_watchdog_kick wait=1014 visual=0.432882 audio=1.446208
13:21:58.871  media_status EndOfMedia（异常，见 §5.2）
13:21:59.128  first_playback_bridge_trace action=render_stall（延迟 250 ms 的采样输出）
```

诊断桥接分项（上述 `render_stall` 行）：

| 项目 | 样本数 | 平均 | 进程内最大值 | 结论 |
|---|---:|---:|---:|---|
| 两设备桥接总计 | 14 | 1.351 ms | 1.867 ms | 不是 1 秒阻塞。 |
| 源侧 setup | 14 | 0.513 ms | 0.740 ms | 非主因。 |
| 目标侧 setup | 14 | 0.836 ms | 1.284 ms | 非主因。 |
| 源纹理创建 | 14 | 0.302 ms | 0.423 ms | 非主因。 |
| 目标纹理创建 | 14 | 0.139 ms | 0.300 ms | 非主因。 |
| 打开共享资源 | 14 | 0.453 ms | 0.660 ms | 非主因。 |
| 源/目标 AcquireSync | 14 | 0.002 / 0.092 ms | 0.007 / 0.220 ms | 无等待超时。 |
| 错误计数 | - | `acq_timeout=0`、`copy_fail=0` | - | 没有复制/互斥失败。 |

注意：`textures_delta=28` / `copied_two_delta=14` 确认当前实现每个跨设备视频帧仍创建两张纹理。这是长期效率和偶发驱动长尾的候选，但**这次采样中**其单帧最大成本仅 0.423/0.300 ms，不能倒推为已证实的一秒根因。

---

## 3. 相关提交、代码修复与本轮新增诊断

### 3.1 已有修复的适用范围

| 提交 | 已修内容 | 本轮现场验证 | 对当前问题的意义 |
|---|---|---|---|
| `ac21d628` `fix(preview): unfreeze the playback clock during PV catch-up; stop per-frame warm-up rebuilds` | 修复 PV catch-up 时间轴冻结、commit 重复 seek、烟花预热每帧使 prepared scene 全量重建、首播音频 reload 竞态。 | R5/R8 首播均 `reseek=0`；R5/R8 的烟花图层 CPU 时间仅约 1–2 ms。 | 这些修复是正确的，但不覆盖驱动层 `render_submit` 阻塞。 |
| `9b607f43` `fix(preview): preload audio and restore commit seek` | 将 SFX/BGM 预热移至文档加载，保留必要 commit seek；提交说明中已注明 QtAV 首播假 EOM 仍可能残留。 | R8 的 BASS 引擎在用户点击前已初始化，首播 commit 已 ready 且未重 seek。 | 同步音频资源加载不是本次 render-submit 1 秒阻塞的主因；假 EOM 风险仍需单列处理。 |
| `13ff44a3` / `0e85b0b2` | beta.14 发布与文档变更。 | R5 beta.14 使用这些基线。 | 是现场旧包可比基线。 |

### 3.2 本轮新增、尚未提交的 beta.15 诊断

本轮为定量排除两设备路径，新增了下列工作区改动：

- `third_party/QtAVPlayer/src/QtAVPlayer/qavhwdevice_d3d11.cpp`、`qavd3d11sharedcontext_p.h`
  - 仅在诊断打开时，统计两设备桥接、源/目标 setup、两侧纹理创建、共享资源打开、源/目标 `AcquireSync` 的次数、总时间和最大值。
- `src/preview/runtime/PreviewSharedD3D11Device.cpp`
  - 只有 `--debug` 和 `MIACODE_PREVIEW_FRAME_PACING_DIAG=1` 同时成立时，才打开上述逐帧计时。
- `src/preview/runtime/PreviewStageMediaHost_Playback.cpp`
  - 在实际 `play()` 前 arm 首播跟踪。
- `src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp`、`.h`
  - 首播 10 秒内首次 `>=100 ms` 的呈现等待，250 ms 延后输出一次聚合分项；延迟是为等渲染线程 RAII 计时提交。
- `src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp`
  - 将 display/fixed gate 的 watchdog 事件送到媒体 host。
- `CMakeLists.txt`
  - 打包版本从 beta.14 升至 beta.15。
- `docs/ops/DEBUG_INDEX.md`、`.codex/skills/miacode-dev-guide/references/debug-flags.md`
  - 记录开关、开销与收集方法。

诊断触发条件与限制：

- 命令：`set MIACODE_PREVIEW_FRAME_PACING_DIAG=1&&call Start_MiaCode_Debug.bat`
- `--debug` 由批处理提供；二者同时成立才有首播桥接分项。
- 仅首播事务、仅前 10 秒、仅第一次至少 100 ms 的 watchdog 等待输出详细行。
- 因此：没有 `action=render_stall` 只代表该会话没有捕获到“首播前 10 秒、至少 100 ms”的事件；**不代表不存在短掉帧或稍后掉帧。**

---

## 4. 已排除、已降级与不能排除的方向

### 4.1 已排除为“本次一秒卡顿主因”的方向

| 方向 | 排除依据 | 边界 |
|---|---|---|
| 音频/音效资源在点击时同步加载 | R8 的音频事务已 ready、`reseek=0`；BASS 初始化发生在点击前；卡顿发生在 QSG render thread 的提交段。 | BASS 的首播时钟/输出延迟仍值得单独观测，见 §5.3。 |
| commit 处重复同目标 PV seek | R5/R8 均为 `reseek=0`。 | 非同目标的真实 seek、暂停后跳转等路径仍可能有解码开销。 |
| 谱面/烟花图层 CPU 计算 | 1 秒帧中 `paint_ms=4.689`、烟花 layer `1.631 ms`；R5 同类证据相同。 | 某种烟花/材质状态可能仍触发底层 GPU pipeline 首次编译，不能将“CPU 不慢”误解为“与烟花状态绝无关联”。 |
| 双设备视频桥接及其当前已测子步骤 | `bridge_max=1.867 ms`，所有 setup/create/open/acquire 均远小于 1 秒，且无超时/失败。 | 它每帧两纹理创建会增加常态负担，也可能在不同驱动/分辨率/内存压力下出现未捕获的长尾。 |
| Present/Swap 本身 | `swap_gpu_ms=1.284`，而大头是 `render_submit_ms=1005.612`。 | 提交阻塞可能是等待先前 GPU 工作；它并不等价于“GPU 完全未忙”。 |
| 普通视频帧间隔短抖动 | R5 PID `68944` 的 145 ms `video_frame_stall` 8 ms 后恢复，性质与本次一秒 QSG 提交阻塞不同。 | 视频端短帧间隔仍可能叠加轻微掉帧。 |

### 4.2 明显降级、但尚未完全排除

- **烟花预热逻辑本身**：旧的“每帧 re-centre 导致全场景重建”已由 `ac21d628` 修复；R5/R8 均在预热 `done` 后才捕获到大阻塞。不过预热完成并不证明每一种实际首播 draw/material/driver pipeline 变体都已被编译或真正提交。
- **两设备路径的资源抖动**：平均和最大值不支持它是这次主因，但“每帧创建两张纹理”仍不是理想结构，尤其在其他驱动、4K PV、显存压力或解码/渲染并行时可能形成尾延迟。
- **系统后台负载**：R4 用户在多程序开启时观察到问题，但 R8 没有系统 CPU/GPU/磁盘/杀毒采样，不能量化其贡献。
- **日志自身开销**：详细时钟和原子计数只在诊断开关下打开，且本次逐帧桥接耗时本身很低；它无法合理制造 1 秒的 `render_submit`。不过任何性能对比仍应包含一次不开诊断的对照运行。

---

## 5. 尚存问题与广泛风险清单

以下按优先级列出，前两项已有直接日志证据，其他为待验证假设。

### 5.1 P0：QSG/D3D11 `render_submit` 内部阻塞

**证据：A。** 两个独立现场（R5 1197 ms、R8 1006 ms）都把主要时间放在 `render_submit_ms`，而 paint/swap/桥接均很低。

可能的内部机制（当前日志无法区分）：

1. Qt Quick/RHI 首次创建或绑定图形管线状态（PSO）、着色器变体编译、驱动缓存填充。
2. `QRhi` / D3D11 command-buffer flush、资源状态转换或驱动全局锁。
3. 同一物理适配器上 D3D11VA 解码与 QSG 渲染的队列串行化；桥接函数本身快速，不代表其先前提交的解码命令不会令 render submit 等待。
4. Windows Desktop Window Manager、显卡电源状态提升、混合 GPU 或驱动首次提交造成的长尾。
5. 其他进程的 GPU/显存争用、显存预算回收、桌面合成/叠加层变化。

### 5.2 P0：QtAV 首播假 `EndOfMedia`

**证据：A。** R8 的 PV 在 `active_elapsed_ms=1446` 时报告文件终点 `position_ms=121066`，日志自报最后 PTS 仍为 1267 ms，二者矛盾。

当前 `PreviewStageMediaHost_Backend.cpp` 已把 `EndOfMedia` 处理为从属 PV 生命周期，不再结束主 transport；这是为什么现场音频继续。但以下风险仍存在：

- PV 提前停在最后帧或黑帧，用户会将其描述为“PV 卡住”。
- 假 EOM 可能与 D3D11VA 解码/首帧队列异常有共同触发条件，并在一秒 render-submit 后由 GUI 线程延后处理。
- `9b607f43` 的 commit 已承认 QtAV queued-prepare 首播仍可能有这条风险；本次在 `reseek=0` 条件下仍出现，说明“避免 commit 重 seek”并未完全覆盖该场景。

需补充媒体层证据：QAV 的 duration、position 改变、stateChanged 顺序、EOF 产生线程/调用栈、首帧队列与 seek serial。修复前应验证“忽略明显不可能的 EOM”不会掩盖真实短 PV 的正常结束；正确边界是区分 stale/false EOM 与真实到达媒体时长，而不是一律忽略 EOM。

### 5.3 P1：BASS 首播时钟/输出起点滞后

**证据：B。** R8 watchdog 的 audio 侧计为 1.446 秒时，BASS 状态里的 `bgm_chart=0.580000`，差约 867 ms；第一个状态样本亦存在约 1 秒差。用户主观报告音频正常，且卡顿主证据在渲染提交，不可据此指认音频为根因。

仍需确认：这是 BASS 的预缓冲/状态采样语义、首播锚点/offset，还是实际声音起点和视觉墙钟不一致。若属实际延迟，它会放大“音频已走、画面停在 0.43 秒”的体感，但不会解释 render thread 被阻塞 1 秒。

### 5.4 P1：首播 GPU 预热覆盖不完整

**证据：B。** R8 中 `firework_pso_warmup_done` 在大阻塞前约 1 秒已出现，说明现有预热完成判据不能保证首播不会有驱动级阻塞。

可能缺失的预热状态：首张硬解视频纹理采样、视频 shader/material variant、透明/混合分支、首个 judge-firework 实际参数组合、窗口实际尺寸后的资源重建、DWM/compositor 参与后的第一次完整提交。预热不能在按播放时阻塞 GUI；应在窗口稳定显示后的空闲阶段分帧进行，并以实际呈现/提交完成为成功条件。

### 5.5 P2：启动阶段的其他 render-thread 或 GUI 任务

**证据：B。** 多个 beta.15 会话在按播放前已有 30–390 ms 的 `render_frame_profile total_ms`，而分项很小；R8 还记录了波形缓存、分析/slow-refresh、扩展激活、布局和媒体绑定等启动工作。

这些任务不直接解释 R8 的 `render_submit=1005 ms`，但可能改变首播时的资源压力、线程调度或被提交的首个场景状态。候选包括：波形缓存、谱面分析、Muri、插件、字体/纹理上传、窗口布局重建、首次 QML 组件/图标加载。

### 5.6 P2：系统/驱动/硬件变量

**证据：C/D。** 用户描述“多次运行后概率下降”和“后台程序多时更易发生”支持该方向；当前日志只给出 AMD 适配器名，未给出驱动版本、GPU 队列、显存预算、频率、电源状态或 DWM 事件。

待考虑项：AMD 驱动版本差异、Windows 更新、HAGS、HDR/VRR/多显示器、笔记本混合显卡、显示器刷新率切换、显存/共享内存压力、防病毒扫描 PV/缓存、OBS/录屏/浏览器硬件加速等。

### 5.7 P2：视频资源与解码器特性

**证据：B。** 复现使用 H.264 NV12 PV；无 PV 不触发是用户稳定观察。`acq_timeout=0` 与 `copy_fail=0` 只说明当前一段桥接成功，不能证明码流、关键帧、B 帧重排、色彩格式、分辨率、视频时长或 D3D11VA decoder 的首用永无问题。

需要针对同一谱面进行：无 PV、静态图片、同分辨率短 H.264、不同编码/分辨率 PV 的矩阵对照；并单独观察假 EOM 是否随文件改变。

### 5.8 P3：诊断盲区与统计边界

- 当前首播 trace 只捕获前 10 秒内首次 `>=100 ms` 呈现等待；短于 100 ms 的掉帧、第二次卡顿、10 秒后卡顿不会产生详细行。
- `render_submit_ms` 是包围式耗时，尚未拆到 Qt Quick/RHI/D3D API 调用或 GPU 队列。它证明“在哪里等”，不证明“谁持锁/谁在编译”。
- 桥接计时只覆盖已经插桩的 `copyToShared` 路径；解码器提交、Qt VideoOutput/QSG 材质创建、DWM 和驱动内部都在统计外。
- 当前 beta.15 是 dirty 构建；手工诊断改动的可重复性与二进制哈希尚未受正式 CI/版本控制保证。

---

## 6. 建议的后续行动（按优先级）

### A. 先补齐 QSG/驱动可见性，不先改设备路径

一次最小复现命令可在现有 beta.15 上同时打开已有首播诊断与 Qt Scene Graph 计时：

```cmd
set MIACODE_PREVIEW_FRAME_PACING_DIAG=1&&set MIACODE_PREVIEW_QSG_RENDER_TIMING=1&&call Start_MiaCode_Debug.bat
```

只需冷启动、打开同一带 PV 谱面、从 0 秒播放一次、退出。收集由日志中的 `runtime_log_path` 指向的目录；若 `MIACODE_LOG_DIR` 生效，通常是包根 `logs/`，不能机械地只取工程 `.miacode/logs`。

目标：将 `render_submit` 大块与 `runtime/preview/qsg_timing` 的 Qt Scene Graph 消息对齐；仍无法解释时再用 Windows Performance Recorder/GPUView/ETW 捕捉 D3D11、DXGI、DWM 与 GPU 队列。ETW 是识别一秒驱动等待的首选证据，需明确征得测试机操作者同意后执行。

### B. 分离“假 EOM”与“首播 submit 卡顿”

为 `QAVPlayer::EndOfMedia` 增加低频但足够的状态上下文（真实 duration、position 变化、最后 PTS、seek/prepare serial、播放已活跃时长、是否在 GUI 事件积压后交付）。先验证同一首播是否每次都出现不可能的 EOM，再决定修复策略。

当前 handler 不停 BGM 是正确的防火墙；不要为了修假 EOM 恢复“视频结束即结束主预览”的旧耦合。

### C. 对照实验的最小集合

1. 同一图表、同一机器：PV 与静态背景/无 PV 各一次冷启动首播。
2. 同一 PV：硬解与包内软件解码启动脚本各一次。
3. 同一环境：诊断开关开/关各一次，确认诊断没有改变复现概率。
4. 仅在 A/B 诊断需要时测试 `MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP=1`；它是渲染循环实验，不应作为默认修复。
5. 如问题仍高度相关于首播状态，临时禁用第三方扩展后复测一次，排除扩展改变皮肤/SFX/资源生命周期的间接影响。

任何单次 A/B 都只能改变一个变量，并记录是否出现 `render_submit` 尖峰与假 EOM；不要把“未复现”直接解释为修复成功。

### D. 可能的产品修复方向（需在 A/B 后实施）

1. 做真正的空闲期 GPU 预热：覆盖首张 PV 硬解纹理、VideoOutput material、烟花首用材质、实际窗口尺寸与混合状态；按帧切片，避免在用户点击播放时同步等待。
2. 将预热完成语义从“合成 marker 已被画过”升级为“覆盖目标首用状态且已完成可观察的 render/present”。
3. 若 QSG/ETW 指向 D3D11VA 与渲染队列竞争，再设计有测量支撑的解码/渲染协调或资源复用方案。不能仅凭 `two-device` 名称切到单设备。
4. 单独修复 QtAV 假 EOM：以事务、媒体时长和实际播放进度识别 stale 终止事件，保留真实短 PV 的从属结束行为。

---

## 7. 交接验收标准

后续负责人在宣称“已修复”前，至少应满足：

1. 冷启动带 PV 首播的复现样本不再出现 `render_submit_ms` / watchdog 的百毫秒级尖峰，且音频/视觉秒差没有异常放大。
2. 同一测试中不再出现“活跃约 1 秒却 `EndOfMedia position_ms≈完整时长`”的假 EOM；真实短 PV 仍不会终止主播放。
3. 无 PV、普通 PV、目标 PV、软件解码 A/B 的结果都有存档，不只报告单次“没有复现”。
4. 诊断改动、最终修复、版本号与文档均以干净提交交付；包内版本、`git_revision` 与报告记录一致。
5. 若最终方案涉及 D3D11 设备拓扑、QSG render loop 或视频后端，需补充针对 AMD/Intel/NVIDIA 和不同刷新率的回归验证计划。
