# 操作日志形态与排障参考

本文档说明 MiaCode 运行时日志的主要文件、行格式、读取顺序和崩溃排障方法。遇到用户提交的日志包时，建议先读本文档，再开始定位问题。

## 1. 日志频道

| 频道 | 文件 | 用途 | 写入方式 | 保留策略 |
| --- | --- | --- | --- | --- |
| `Operation` | `miacode_operation.log` | `MC_OP` 记录的操作链失败，包括显式 `fail()` 和异常栈展开 | 异步 | 4 MB 后轮转为 `.1/.2/.3`；失败路径常开，不依赖 `--debug` |
| `Fatal` | `miacode_fatal.log` | 死亡事件、已捕获异常和子进程崩溃摘要 | 同步 + fsync | 4 MB 后轮转；常开 |
| `Runtime` | `miacode_runtime_debug.log` | 常规运行时、supervisor、IPC、Quick shell 等调试事件 | 异步 | `--debug` 下写入并轮转 |
| `Audio` | `miacode_audio_debug.log` | BASS / miniaudio 音频引擎，以及部分预览视频/解码日志 | 异步 | `--debug` 下写入并轮转 |
| `Export` | `miacode_video_export.log` | 视频导出流程；关键失败阶段也会写入 `Fatal` | 异步 | `--debug` 下写入详细诊断；非 debug 下保留简要导出摘要 |
| `StartupTiming` | `miacode_startup_timing.log` | 应用启动阶段耗时 | 异步 | `--debug` 下写入，可用 `MIACODE_DISABLE_STARTUP_TIMING` 关闭 |
| `PreviewProfile` | `miacode_preview_profile_summary.txt` | 单次预览 session 的性能摘要 | 同步截断快照 | 每次整体重写，不轮转 |
| Shadow | `miacode_op_chain_<pid>.log` | 硬崩溃时的 heap-free 操作链快照 | Win32 直接写 | 每个进程一份，下一次同 PID 崩溃会覆盖 |

每条 `appendLine` 记录的统一格式：

```text
<UTC-ISO8601-Z> <LEVEL> pid=<n> tid=<n> [<channel>/<scope>] <payload>
```

示例：

```text
2026-06-18T18:36:51Z ERROR pid=68408 tid=52460 [op/failed] op=... reason=...
```

时间戳为 UTC；`LEVEL` 可为 `TRACE`、`DEBUG`、`INFO`、`WARN`、`ERROR`、`FATAL`。

## 2. 文件位置

日志目录按以下顺序决定：

1. 单频道 override 路径，例如 `MIACODE_RUNTIME_LOG_PATH`
2. `MIACODE_LOG_DIR`
3. 谱面工程本地 `.miacode/logs/`
4. `--debug` 时的应用目录 `logs/`
5. 系统临时目录

父进程会把日志目录传给子进程。为避免 Windows 下异步 writer 独占文件导致多进程追加失败，editor 和 worker 的 `runtime` / `fatal` 日志使用不同文件名，例如：

| 文件 | 写入方 |
| --- | --- |
| `miacode_runtime_debug.log` | editor |
| `miacode_runtime_debug_worker.log` | worker |
| `miacode_fatal.log` | editor |
| `miacode_fatal_worker.log` | worker |
| `miacode_audio_debug.log` | editor |
| `miacode_video_export.log` | editor / export worker |
| `miacode_op_chain_<pid>.log` | 崩溃进程 |

## 3. 主要行形态

### 3.1 `Operation` 显式失败

```text
[op/failed] op=<class>::<method> chain=<leaf> <- <parent> reason=<text> at=<file>:<line> notes=[<key>=<val>; ...]
```

含义：

- `op=`：当前失败的操作名。
- `chain=`：当前线程上的操作链，叶子节点在前。
- `reason=`：失败原因，通常来自 API 错误或业务检查。
- `at=`：`MC_OP` 调用点。
- `notes=`：失败前累积的关键上下文。

### 3.2 `Operation` 异常栈展开

```text
[op/failed] op=<class>::<method> chain=<...> reason=exception what=(unwind) at=<file>:<line> notes=[...]
```

`what=(unwind)` 是有意设计：析构函数运行时还没进入匹配的 `catch`，真实异常文本会由 `Fatal` 频道记录。

### 3.3 `Fatal` 捕获异常或死亡事件

```text
[fatal/<scope>] <payload> | chain=<...>
```

`Fatal` 同步写盘，适合作为崩溃和异常排障的第一入口。

### 3.4 Shadow 操作链快照

```text
miacode operation breadcrumb shadow
pid=<pid> utc=<ISO timestamp>

thread tid=<thread_id> depth=<N>
  [<N-1>] <leaf op name>
  [<N-2>] <parent op name>
  ...
  [0] <root op name>
```

Shadow 文件用于硬崩溃、SEH、signal、`__fastfail` 等无法正常走 Qt/堆分配路径的情况。

## 4. 排障流程

### 4.1 功能无反应且没有错误提示

1. 打开 `miacode_operation.log`。
2. 搜索 `[op/failed]`。
3. 用 `chain=` 判断用户动作在哪个内部步骤失败。
4. 如果没有 `Operation` 记录，说明失败路径尚未覆盖 `MC_OP`，应考虑在消费结果的上层入口补充操作链记录。

### 4.2 有 toast、状态栏或错误对话框

1. 先打开 `miacode_fatal.log`，查找同一时间窗口的 `[fatal/<scope>]`。
2. 用 `details=` 或 payload 读取异常文本。
3. 再对照 `miacode_operation.log` 中相同时间附近的栈展开记录。
4. `Operation` 的 `notes=` 往往包含路径、命令、输入参数等现场上下文。

### 4.3 进程直接消失

按顺序检查：

1. `miacode_fatal.log` 中是否有 `[fatal/preview/worker_shadow_chain]`。
2. 是否存在对应 PID 的 `miacode_op_chain_<pid>.log`。
3. `miacode_runtime_debug.log` 崩溃前最后几行。

如果 editor 崩溃而 worker 稍后出现 `stdin_eof treated as shutdown`，通常说明 worker 存活到 OS 关闭管道为止。此时 editor 侧最后一条 runtime 日志最接近真正死亡点。

### 4.4 editor 和 worker 状态不一致

把 editor / worker 两边日志按 UTC 时间戳合并阅读：

| editor | worker |
| --- | --- |
| `miacode_runtime_debug.log` | `miacode_runtime_debug_worker.log` |
| `miacode_fatal.log` | `miacode_fatal_worker.log` |

supervisor 从 worker stdout 解析到的事件会写在 editor runtime 日志里；worker 自己的内部状态仍以 worker runtime 日志为准。

### 4.5 日志完全为空

检查：

- 是否设置了 `MIACODE_LOG_DIR`，或是否以 `--debug` 启动。
- 进程是否真的启动，可看 `StartupTiming`。
- 非 debug Release 下异步日志可能来不及落盘；但 `Fatal` 是同步写盘，如果它也为空，通常说明进程没有走到 catch/fatal 路径。

## 5. `MC_OP` 使用约定

典型形态：

```cpp
bool MyClass::myMethod(const QString& arg) {
    MC_OP("MyClass::myMethod");
    _mc_op_.note(QStringLiteral("arg=%1").arg(arg));
    if (someEarlyFailure) {
        _mc_op_.fail(QStringLiteral("reason"));
        return false;
    }
    return true;
}
```

约定：

- 操作名使用 `<Class>::<Method>` 或自由函数名。
- `note()` 只记录真正有助于排障的上下文。
- `fail()` 用于显式失败返回，并避免析构时重复记录。
- 不要在每帧渲染循环、纯 getter、纯计算 helper 或热路径里滥用 `MC_OP`；应记录在用户动作或业务入口层。

## 6. 已知限制

- 异步 writer 队列有上限；溢出时会写入 `dropped=N reason=queue_overflow` gap marker。
- `Fatal` 同步写盘，不走异步队列，不会被丢弃。
- Shadow buffer 的线程槽数量固定；极端多线程 session 可能出现新线程没有 shadow 表示。
- 旧版日志曾假设 editor / worker 可共写同一文件，当前设计已改为分文件。

## 7. 新日志包推荐阅读顺序

1. 读 `miacode_fatal.log`。
2. 对每个 fatal 时间点，查看前后几秒的 `miacode_operation.log` 和 `miacode_runtime_debug.log`。
3. 检查是否有 worker shadow collation。
4. 检查是否有孤立的 `miacode_op_chain_*.log`。
5. 启动问题看 `StartupTiming`。
6. 音频、预览媒体、导出问题再分别看 `Audio` / `Export`。

## 8. 源码入口

| 概念 | 文件 |
| --- | --- |
| 日志频道、`appendFatalMessage`、异步 writer | `src/common/DebugLog.h`、`src/common/DebugLog.cpp` |
| `MC_OP`、`Scope`、当前操作链、shadow buffer | `src/common/OperationLog.h`、`src/common/OperationLog.cpp` |
| SEH/signal/terminate 崩溃恢复 | `src/common/CrashRecovery.cpp` |
| worker shadow 自动收集 | `src/preview/ipc/PreviewWorkerSupervisor.cpp` |
| 跨进程 demo / spec | `src/tools/oplog/OperationLogSpec.cpp` |
