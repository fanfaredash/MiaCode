# 日志机制审计 — 剩余重构待办（Remaining Refactor Backlog）

## 背景

2026-06-19 对 `miacode::debug_log`（`src/common/DebugLog.{h,cpp}` + `DebugOptions.h` +
`OperationLog.{h,cpp}` + 周边调用点）做了一次多代理设计审计 + 对抗式复核（46 个 agent，
39 条原始发现 / 36 条确认 / 3 条驳回）。

**已实施并提交**（commit `034769c`，分支 `refactor/god-file-split`，CTest 25/25）：
1. 溢出缺口标记（`dropped=N reason=queue_overflow`）；2. UTC 时间戳；3. 每行 pid/tid；
4. 正交 `Level` 枚举（durable flush 改由 `level==Fatal` 触发）；5. rename 轮转
（`.1/.2/.3`）；6. 分类 flag 原子缓存 + `MIACODE_SKIP_ASYNCLOG_FLUSH` 只读一次；
7. `leak_gauge`/`MemoryStageScope`/资源采样拆到 `ProcessDiagnostics.{h,cpp}`
（`miacode::diag`）+ `resetChannel` beacon 脚手架清理 + `PreviewPopupHwndTracker` 四写→单写
+ `permanentShutdown_`→`std::atomic` + worker 路径缓存 + `kChannelCount` static_assert。

**本文档**收录审计里**尚未实施**的剩余项（多为 `low`，少数 `med`，外加一个大重构）。
每条给：现状（含定位）→ 为什么是问题 → 修复方案 → 影响面/风险/工作量 → 依赖。
严重度采用复核**修正后**的值。⚠ 强调：整套是 `--debug` 门控的开发者诊断设施，**没有**
正确性/安全/数据损坏级别的活跃 bug；以下全是设计合理性 / 可运维性 / 可维护性层面。

---

## 优先级一览

| 序号 | 标题 | 严重度 | 工作量 | 依赖 | 状态 |
|---|---|---|---|---|---|
| B3 | flag 代码内注册表 + 启动 effective-config dump | med | 中 | — | 未做 |
| A3 | 移除 `PreviewProfile` 幽灵频道 | med | 中 | 注意 `kChannelCount` | 仅文档已修 |
| C3 | 行形态统一（`formatTitleLine`/startup 头） | med | 小 | — | 部分（`appendLine` 已统一） |
| A4 | `BassPreviewDebugLogRouting` 改用真 scope | low | 小 | — | 未做 |
| A2 | 预览视频日志移出 `Channel::Audio` | low | 中 | A1 | 仅文档已修 |
| A5 | `scope` 字符串注册表 + 漂移守卫 | low | 中 | — | 未做 |
| C1 | Qt `QtMsgType`→`Level` 折进日志 | low | 小 | — | `Level` 已加，未接 |
| B1 | 单频道单独开启 | low | 小 | — | 未做 |
| B2 | flag 极性统一 | low | 中 | — | 未做 |
| B4 | `Operation` 频道 kill switch | low | 小 | — | 文档已补，开关未加 |
| C2 | key=value payload 转义助手 | low | 小 | — | 未做 |
| D1 | 删除 path accessor zoo | low | 小 | — | 未做 |
| D2 | 合并 `logMutex` + `AsyncLogWriter::mutex_` | low | 中 | — | 未做 |
| D3 | 显式初始化单例 + 退役 `SKIP_ASYNCLOG_FLUSH` | low | 中 | 修根因 | 已缓存，根因未修 |
| D4 | 统一 worker / sync 两条写路径 | low | 小 | — | 未做 |
| E1 | 去掉 `force=true` 双重 gate 惯用法 | low | 小 | — | 未做 |
| **A1** | **`Channel` 三轴（文件/类别/策略）拆分** | **med** | **大** | A2/A5 的根因 | 未做 |

---

## A. 分类法（`Channel` 枚举）

### A1 — `Channel` 把三件正交的事焊死（根因） · `med` · 大重构
- **现状**：一个 `Channel` 值同时决定写哪个文件（`channelFileName`）、打印什么标签
  （`channelLabel`）、用哪个 `MIACODE_DISABLE_*` gating（`channelEnabled`），外加
  `channelPathOverride`——共 4 个 switch 都按同一枚举分发（`src/common/DebugLog.cpp`）。
- **为什么是问题**：每加一个日志关注点都被迫二选一——复用不合身的频道（标签/开关都错），
  或新增频道（同时改 4 个 switch + 新 env + 新 accessor + 调 `kChannelCount`）。A2/A3 都是它的后果。
- **修复**：拆成三轴——Category（类别，可枚举或自由）、Sink（目标文件）、Policy（开关 + level 阈值），
  用一张 `category → {默认 sink, 默认策略}` 小表保持常见情况一参，允许覆写。
- **影响面/风险**：改 API 形态 + ~183 个调用点的潜在适配；风险中等。**建议留到专门时间窗**，
  做的时候顺带消化 A2/A4/A5。

### A2 — `Channel::Audio` 被预览视频/解码日志复用 · `low`（仅文档已修）
- **现状**：`appendPreviewStageMediaLog`（`src/preview/runtime/PreviewStageMediaHostInternal.h`）
  把 `media_backend`/`video_frame_first`/`video_decode_*` 全发到 `Channel::Audio`，而同功能的
  `preview/hwframe`/`preview/seek_landing`/`preview/hwdecode_summary` 走 `Channel::Runtime`。
- **为什么是问题**：一个逻辑子系统（预览视频）被劈成两个文件，方向反直觉（视频解码在 *audio* 日志）；
  排查时 grep runtime 日志只看到 teardown，会误判"没记日志"。
- **现状补充**：本次已在 `OPERATION_LOG_PATTERNS_SPEC.md` 的频道表里把 Audio 标注为
  "+ 预览视频/解码"，但**没有真拆**。
- **修复**：给预览媒体独立 sink（如 `miacode_preview_media.log`），stage_media + hwframe/seek 落一处，
  按真实类别而非 `audio` gating。**依赖 A1 的轴拆分**，否则只能靠新增频道（治标）。

### A3 — `Channel::PreviewProfile` 是幽灵频道 · `med`（仅文档已修）
- **现状**：它占着完整机制（label `preview_profile`、filename `miacode_preview_profile_summary.txt`、
  env override `MIACODE_PREVIEW_PROFILE_PATH`、enable accessor、4 个 switch arm），但 grep 确认
  **没有任何 `appendLine`/`appendText` 通过它写入**。真正的 summary 由
  `PreviewRuntime::writeProfilingSummaryToFile()`（`src/preview/runtime/PreviewRuntime.cpp:1220+`）
  直接 `QFile(WriteOnly|Truncate)+QTextStream` 写出，**完全绕过异步写入器**（无 trim、无 drop 统计、无 flush 路径）。
- **为什么是问题**：误导读者以为它走 writer；`clearDebugSessionLogs()` 还为删一个 worker 从不打开的文件
  而 `clearChannel(PreviewProfile)`（flush+stop worker 一次）；它是 Truncate 快照而非追加流，违反其它频道的流不变量。
- **现状补充**：本次已把 `OPERATION_LOG_PATTERNS_SPEC.md` 里错标的 "Async" 改成 "Sync Truncate 快照（out-of-band）"，
  但**枚举里的死机制仍在**。
- **修复**：把 `PreviewProfile` 从 `Channel` 移除，`writeProfilingSummaryToFile` 自带独立 path helper，
  文档化为"快照产物"而非频道；删掉误导的 `clearChannel`/override/enable 死管线。
  ⚠ 移除频道要同步调 `kChannelCount=7` 和那条 `static_assert`，并检查 4 个 switch 的 `default`。中等改动。

### A4 — `BassPreviewDebugLogRouting` 是第二套（字符串）分类法 · `low`
- **现状**：`src/audio/BassPreviewDebugLogRouting.h` 定义了 `BassDebugRoute{Init,Transport}` +
  15 值 `BassDebugOperation` + `bassDebugRouteForOperation()` 映射；但 `appendBassDebugLog`
  （`src/audio/BassPreviewAudioBackend_PlaybackClock.cpp`）只把 route 转成 `bass_init`/`bass_transport`
  文本**拼进 payload**，再用**空 scope** 发给 `Channel::Audio`（`appendAudioDebugLog` →
  `appendLine(Channel::Audio, QString(), message)`）。
- **为什么是问题**：精心建模的两级枚举塌缩成 payload 里的不透明子串，没用 logger 唯一的结构化字段 `scope`，
  没法 `grep '[audio/bass_transport]'`；和 `scope` 的职责重复。
- **修复**：把 route 喂进真正的 `scope`（如 `appendLine(Channel::Audio, "preview/bass/transport", payload)`），
  删掉并行字符串方案。小改动、低风险。⚠ `BassPreviewDebugLogRoutingSpec.cpp` 只测枚举映射逻辑，不测输出，
  改动不会破坏它，但记得相应更新。

### A5 — `scope` 自由字符串无注册表 · `low`
- **现状**：`scope` 是未校验的自由 `QString`（`appendLine` 仅 `bracket = channelLabel + '/' + scope.trimmed()`）。
  深度不一（单 token `stage`/`crash_recovery` vs 三级 `render/backend_d3d11/core`）、同区域多拼法
  （`timeline/quick_item` vs `timeline/quick_scene`）、一个文件混两套命名空间、多处传空 scope（裸 `[audio]`）。
- **为什么是问题**：env flag 有 `debug_flag_index_spec` 漂移守卫，scope 却无任何守卫；grep 可靠性全靠记拼法，
  新人每个站点发明新 scope，熵只增不减。dev-guide §4 手维护的"stable tags"列表本身也不自洽。
- **修复**：精选 scope 常量（或 enum-like 头）+ `area/subarea` 约定 + 一个仿 `DebugFlagIndexSpec`
  的 CTest 漂移守卫（grep scope 字面量对照注册表），禁止空 scope。中等工作量（主要是收敛 ~183 个站点）。

---

## B. Gating / flag 控制面

### B1 — 无法单独开一个频道 · `low`
- **现状**：`debugCategoryEnabled = debugModeEnabled() && !envFlagEnabled(disableKey)`
  （`src/common/DebugOptions.h`）。每个详情频道都 AND 在全局 `--debug` 上，只能 opt-out，没有 `MIACODE_ENABLE_<CH>`。
- **为什么是问题**：想只看 audio 得开全局 `--debug`（连带整个 firehose）再逐个 `DISABLE` 掉其余四个——操作反直觉。
- **修复**：per-channel opt-in flag（不依赖 `--debug`），或采用 Qt `QT_LOGGING_RULES` 风格；至少让 `--debug`
  设默认 level、per-channel flag 可上调或下调。⚠ 审计自己提醒"尽量别加 flag"，且每频道已是独立文件
  （"把 audio 日志发我"本就能满足），**价值有限**。

### B2 — flag 极性不统一 · `low`
- **现状**：至少四种习惯并存——opt-out `MIACODE_DISABLE_*`；opt-in 默认关（`envFlagEnabled`）；
  tri-state `.value_or(true)`（默认开）/ `.value_or(false)`（默认关）；反义命名
  `MIACODE_PREVIEW_REJECT_NEGATIVE_HS=1`（关掉一个默认开的功能）。"设成 1"在不同 flag 上含义相反。
- **为什么是问题**：读者无法不看 accessor 就预测 `=1` 的效果；现场易误设。
- **修复**：统一约定——名字即功能、`1` 恒为开、默认值用 unset 表达、`REJECT_`→`ALLOW_`，文件顶部写一次；
  用已有的 `envOptionalFlagValue` 把 `MIACODE_DISABLE_X` 换成 tri-state `MIACODE_X`。牵涉 env 名 + 文档 + 兼容，中等。

### B3 — 72 个 env flag 无代码内注册表 · `med`（**剩余里最值得做**）
- **现状**：每个 flag 都是散落的内联字面量（如 `channelPathOverride` 的 7 个 `MIACODE_*_LOG_PATH`、
  `DebugOptions.h` 的各 accessor）。唯一防线是 `debug_flag_index_spec`（grep `src/` 的 `MIACODE_*`
  对照 `docs/DEBUG_INDEX.md`）。没有 `{name, default, category, purpose}` 表。
- **为什么是问题**：无法程序化枚举"所有 flag + 当前值"（bug 报告最需要的启动 effective-config dump 不存在）；
  拼错静默 no-op；默认值只写在散文里；doc 是 registry-of-record 却手写。dev-guide 记着"单一注册表 still pending"。
  （注：`DebugOptionsSpec` 已锁住 per-flag 默认值，部分缓解，但不提供枚举/防拼错/配置 dump。）
- **修复**：`DebugOptions.h` 建 constexpr 表，所有 accessor 走它；由表生成 `DEBUG_INDEX.md`（用生成校验替代 grep 守卫）；
  启动打一行把每个 flag 的生效值 dump 到 Runtime/StartupTiming 频道。独立的中等工程。

### B4 — `Operation` 频道无 kill switch · `low`（文档已补，开关未加）
- **现状**：`channelEnabled(Channel::Operation)` 恒返回 `true`（`DebugLog.cpp`），且两个生产者
  （`~Scope` 解栈、`Scope::fail`，`src/common/OperationLog.cpp`）都传 `force=true`——故 `--debug` 与
  disable flag 都管不到，是唯一一个不走 accessor 的频道。
- **现状补充**：本次已在 `DEBUG_INDEX.md` 把它和 Fatal 一起记成"有意常开（失败路径专用）"。
- **为什么是问题（残留）**：若将来某 per-frame op 持续失败，`miacode_operation.log` 会在 release 构建里泛滥且无关停手段。
- **修复（可选）**：让 `channelEnabled(Operation) = !operationLogDisabled()`（默认开、独立于 `--debug`），
  并**去掉两个 oplog 站点的 `force=true``（否则开关被 force 旁路）。⚠ 需新增一个 env flag（违反"别加 flag"指引）
  + 写进 `DEBUG_INDEX.md`。复核认为是"理论隐患",**非必须**。

---

## C. 格式 / 可运维性

### C1 — Qt 消息处理器丢弃 `QtMsgType` · `low`（`Level` 已加，未接上）
- **现状**：`src/app/main.cpp` 的 `qInstallMessageHandler`（仅在 `previewQsgRenderTimingEnabled()`
  这个 opt-in flag 下安装）只捕获 `qt.scenegraph.time.*`，其余 Qt warning/critical 原样转发给前一个 handler
  （默认 stderr），`QtMsgType` 被丢弃，没折进我们自己的日志。
- **为什么是问题**：现在已有 `Level` 枚举，本可 `QtMsgType → Level` 映射、把 Qt warning/critical 也收进
  `Channel::Runtime`（现场 bug 报告很有用）。复核确认"没东西消失"（仍透传 stderr），所以这是**增强**而非风险修复。
- **修复**：默认安装 handler，映射 `QtMsgType→Level`，**同时保留 stderr 透传**（复核特别提醒别独占转走），
  把非 scenegraph 的 Qt 消息复制进 `Channel::Runtime`。小改动，但碰 message handler 要谨慎。

### C2 — key=value payload 手拼、无转义 · `low`
- **现状**：每条 payload 都是手写 `k1=%1 k2=%2`，空格分隔，无引号/转义。带空格的 Windows 路径
  （如 `chart=C:\My Charts\song.txt`，`PreviewStageMediaHost_Media.cpp`）会破坏字段边界；oplog 的 `chain`
  含 ` ← `、`notes=[...]` 含自由文本。
- **为什么是问题**：日志自称 k=v schema 却不提供使其可解析的引号/转义。复核确认仓库里唯一真正的解析器
  （`scripts/compare_log_vs_video_trajectory.py`）用了 key-anchored 正则没被坑，所以只影响临时 grep。
- **修复**：加一个 logfmt 式 `kv(key, value)` 助手（值含空格/`=`/引号时加引号 + 反斜杠转义），先在带路径的站点采用。低价值锦上添花。

### C3 — 行形态不统一 · `med`（部分已解决）
- **现状**：`appendLine` 现在是统一文法 `<ts> <LEVEL> pid= tid= [ch/scope] payload`；但
  **`formatTitleLine`（`[<ts>] <title>`，无频道、ts 在括号内）** 和 **startup-timing 会话头**
  （`initializeStartupTimingLogSession` 手拼、带 pid 而后续 stage 行不带）仍是另一种形态，落在同一文件里。
  `MainWindow.WindowRuntime.cpp` 的 `appendOutput` 经 `formatTitleLine` 写进 runtime 日志。
- **为什么是问题**：`^<ts> \[` 的正则会漏掉 title 行;pid 只在会话头出现、不在它该标注的行上。
- **修复**：让 `formatTitleLine` 和 startup 头也走统一文法（title/session 信息作 payload 字段）。
  ⚠ `appendOutput` 影响 GUI 输出面板,改前看一下显示。小改动。

---

## D. 模块分层 / API（全 `low`，复核多判"非活跃风险"）

### D1 — path accessor zoo · `low`
- **现状**：`DebugLog.h` 暴露 `logPath(Channel)` + 7 个一行 wrapper（`runtimeLogPath()`/`audioLogPath()`/…），
  cpp 里各是 `return logPath(Channel::X);`。grep 显示 `runtimeLogPath`/`operationLogPath` 零调用。
- **修复**：删 wrapper，调用点直接用 `logPath(Channel::X)`（机械重命名）；至少别再每加频道就加一个 accessor。
  纯表面膨胀,零行为影响。

### D2 — 两把锁守着重叠文件状态 · `low`
- **现状**：`logMutex()` 与 `AsyncLogWriter::mutex_` 协调只靠不成文约定（destructive 路径先 `flush()+stop()`
  再取 `logMutex`）。复核**驳回**了具体竞争（Fatal 走独立文件且永不入队、两锁从不嵌套故不死锁），
  真正残留的只是 `trimFileToMaxBytesLocked`→现 `rotateFileLocked` 的 "Locked" 命名在 worker 路径上不持 `logMutex`
  + 一个窄 TOCTOU（session 边界 clear/reset 时 worker 可能重开文件）。
- **修复**：把所有写（含 Fatal/同步回退）经单一 writer 入口走 `mutex_` + 缓存句柄，取消 `logMutex`；
  destructive op 做成 writer 方法。一个所有者、一把锁。中等。

### D3 — Meyers 单例 + `MIACODE_SKIP_ASYNCLOG_FLUSH` 逃生舱 · `low`
- **现状**：`AsyncLogWriter` 是函数局部 static Meyers 单例,首次构造曾在某些 Win10 22H2 上 fault,
  应对是把 env 检查穿进写路径（`appendText`/`resetChannel`/`shutdownAsyncLogWriter`）。
- **现状补充**：本次已把该 env **只读一次缓存**（`skipAsyncLogFlush()`），去掉每行 env 读;但
  **根因没修、逃生舱仍是写路径里的载荷分支**。
- **修复**：用 main() 显式初始化的 writer 替代懒构造（构造 fault 在已知边界暴露）;把构造 fault 当真 bug 修
  （静态初始化顺序 / windows.h），再退役 `MIACODE_SKIP_ASYNCLOG_FLUSH`。中等。

### D4 — worker / sync 两条写路径分叉 · `low`
- **现状**：稳态走 worker 缓存句柄(不 flush);关停后(`permanentShutdown_`)与 `joinAndDrain` 残留排空
  走 `writeEntrySync`(每行 open/write/close,close 会 flush),trim/flush/锁语义不同。
- **修复**：合并成一个按 `sync` 参数化的 `writeEntry`,消除分叉。小改动、维护性收益。

---

## E. 调用点纪律

### E1 — `force=true` 双重 gate 惯用法 · `low`
- **现状**：多处 per-tick/per-frame 性能包装(`timeline/ui_perf`、`preview/frame_pacing`、`edit/muri_perf`、
  validation perf,见 `MainWindow.TimelineFramePacing.cpp`、`TimelineView.cpp` 等)先本地查一个 flag,
  再传 `force=true` 让 `shouldWrite` 跳过 `channelEnabled`。`frame_pacing` 用的是
  `MIACODE_PREVIEW_FRAME_PACING_DIAG`(非 runtime 频道开关),故 force 后 `MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT`
  对它失效;且 `appendPreviewFramePacingStatusLog` 是无本地 gate 的常开 force(低频状态行)。
- **为什么是问题**:`force=true` 本是 Fatal/Export 摘要的"无视 `--debug` 写"逃生舱,这里被当"我已 gate"用,
  既冗余又埋雷(后人删了本地 gate 就变 release 常开洪流)。
- **修复**:这些站点去掉 `force=true`,让 `channelEnabled` 当唯一 gate(本地 flag 作便宜的早退);
  frame-pacing 改成 `runtimeDebugOutputEnabled() && diagFlag`,使 runtime 频道开关仍有效。`force=true` 只留给 Fatal + Export 摘要。小改动。

---

## 推荐执行顺序（按性价比）

1. **B3**(flag 注册表 + 启动 dump)——现场诊断价值最高,独立工程。
2. **A3**(移除 PreviewProfile 幽灵频道)——清掉误导性死代码,中等。
3. **A4 + A2 + A5** 一组——把预览/bass 日志和 scope 收敛(治 A1 症状,不动 A1 大重构)。
4. **C1**(Qt warning 折进日志)——增强,现场报告有用。
5. **C3 / C2 / D1 / D4 / E1**——收尾,可顺手带。
6. **D2 / D3 / B1 / B2 / B4**——清晰度/逃生舱,按需。
7. **A1**(三轴拆分)——真正的大重构,留到专门时间窗,做时顺带消化 A2/A4/A5。

---

## 已驳回的审计声明（供参考,避免重复排查）

复核**驳回**了下面 3 条——它们**不是**问题,记此以免将来重新调查:

- ❌ **"`force=true` 绕过频道门是缺陷"**——是有意且已文档化的架构(诊断行由专属 flag gating);
  per-frame logger 其实是 gated 的。
- ❌ **"trim 每 200 行 close/reopen 有竞争 + 卡顿"**——有 `QFileInfo.size()` 短路,真正重写约每 ~7000 行一次;
  Fatal 走独立文件,所述同文件竞争不可达。(注:已改 rename 轮转,此项更不成立。)
- ❌ **"五套崩溃捕获机制没有阅读指引"**——`docs/OPERATION_LOG_PATTERNS_SPEC.md` 恰好就是那份指引
  (§4 triage recipes、§8 阅读顺序、Fatal 标为权威)。唯一小缺口:`DEBUG_INDEX.md` 未交叉链接到它。

---

## 关联文件 / 入口

- 已实施总览 + 行格式 + gating + ProcessDiagnostics 拆分:
  `.claude/skills/miacode-dev-guide/references/debug-and-logging.md`
- 排障流程(频道表/triage recipes):`docs/OPERATION_LOG_PATTERNS_SPEC.md`
- env-flag 索引:`docs/DEBUG_INDEX.md`
- 核心实现:`src/common/DebugLog.{h,cpp}`、`DebugOptions.h`、`OperationLog.{h,cpp}`、`ProcessDiagnostics.{h,cpp}`
