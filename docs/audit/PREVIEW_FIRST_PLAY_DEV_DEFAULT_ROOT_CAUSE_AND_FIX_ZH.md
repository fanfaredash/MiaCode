# Windows 首次播放无响应：BASS DEV_DEFAULT 时序根因与修复（v1 / dev）

- 日期：2026-09-23
- 代码基线：`dev` `2ddb6939`
- 修复提交：`fc155b03`，移植自 `feature/qml-ui` 的 `4d7c274e`。v2 的完整记录在该分支的同名文档（`b8a305c4`）。
- 验证环境：Windows 11 10.0.26200，MSVC 2022，Qt 6.8.3，bass.dll 2.4.17.0（dev 随包版本）

## 结论

v1 与 v2 是同一个问题，根因、修复方式都相同。

Windows 预览音频引擎要绑定到具体的 Core Audio 输出端点，前提是先用
`BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE)` 关闭 BASS 的"跟随系统默认设备"。这个设置是进程级的：
进程里第一次 BASS 设备枚举或 `BASS_Init` 之后就再也改不了，任何设备（包括 0 号 no-sound 设备）、
任何线程的调用都算。修复前，这一步只在预览引擎初始化时尝试一次，并用 `std::call_once` 缓存结果。
打开没有波形缓存的谱面时，波形解码线程会先 `BASS_Init(0)`，引擎那一次尝试必然以 `err=37`
（`BASS_ERROR_NOTAVAIL`）失败。失败结果被缓存后，本进程内每次引擎初始化都直接失败，
包括点播放触发的那一次，所以播放无响应，只能重启。重启后波形从磁盘缓存读取，不再走 BASS 解码，问题"消失"。

修复把这一步挪到 `main()` 最开始、任何 BASS 调用之前，引擎只读回结果。

| | 修复前（`2ddb6939`） | 修复后（`fc155b03`） |
| --- | --- | --- |
| 打开无缓存谱面，引擎初始化 | 6/6 失败（`bind_failed err=37`） | 6/6 成功（`bass_engine_ready`） |
| 保留缓存的对照轮 | 2/2 成功 | 2/2 成功 |
| 打开无缓存谱面后点播放 | 1/1 无响应 | 3/3 正常播放 |
| 启动 beacon | — | 11/11 进程 `disabled=1 err=0` |

已发布的 v1 打包版 1.1.0-beta.12 用同样方法测了 3 轮，3/3 触发。

## 1. 现象与影响范围

- 打开 MiaCode 后第一次点播放没反应（按钮不变、画面不走），必须重启才能恢复；只在 Windows 上出现。
- 触发条件：这个进程打开的第一个谱面在 `.miacode/` 下没有波形缓存。
- 影响版本：包含 `2fd4f960`（2026-08-10，引入具体端点绑定和 DEV_DEFAULT 关闭）、
  不包含 `fc155b03` 的 Windows 构建。
- macOS / Linux 不受影响：只有 Windows 分支依赖这个设置。

## 2. 根因

### 2.1 BASS 库行为

每个场景单独起一个进程测试。dev 随包的 2.4.17.0 和 v2 随包的 2.4.18.3 结果一致：

| 场景（全新进程） | `set_ok` | 错误码 |
| --- | --- | --- |
| 什么都不做，直接设置 | 1 | 0 |
| `BASS_Init(0)` 之后（设备仍持有） | 0 | 37 |
| `BASS_Init(0)` + `BASS_Free` 之后 | 0 | 37 |
| 另一个线程 `BASS_Init(0)` 之后 | 0 | 37 |
| 只调用 `BASS_GetDeviceInfo` 枚举设备之后 | 0 | 37 |
| `BASS_PluginLoad` 之后 | 1 | 0 |
| 先设置，再 `BASS_Init(0)`，读回 `BASS_GetConfig` | 设置成功，读回 0（保持关闭） | — |

### 2.2 修复前的代码时序（v1）

1. 打开谱面后，波形 worker 立即请求波形。缓存未命中时，`src/common/WaveformCache.cpp`
   在波形线程上 `BASS_Init(0, …, BASS_DEVICE_NOSPEAKER)` 解码，约 0.2 秒完成。
2. 预览音频引擎在 `reload_assets` 时才初始化（`BassPreviewAudioBackend::initializeAudioEngine()`），
   实测比波形请求晚 0.50–1.15 秒，在 BASS 解码完成之后。
3. 修复前，引擎初始化里的 `disableBassDefaultDeviceEntry()` 是进程内唯一一次
   `BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE)`，此时设置已被关闭，失败并缓存 `err=37`。
4. 点播放时再次调用 `initializeAudioEngine()`，读到缓存的失败直接返回，
   播放启动报告 `preview audio backend is unavailable native_error=37`。

v1 引擎初始化与波形请求的间隔比 v2 短（v2 为 0.7–6.4 秒），但顺序没有变：
波形的 BASS 解码总在引擎之前开始，点播放也要等谱面加载完。因此只要第一个谱面没有缓存，就会触发。
进程内导出音频（`src/tools/video_export/BassExportAudioBackend.cpp`）同样会 `BASS_Init(0)`，修复同样覆盖它。

## 3. 修复前的运行时日志证据（`2ddb6939`）

### 3.1 验证方法

- 判定规则：出现 `bass_endpoint_bind_failed reason=disable_default` → 触发（HIT）；
  出现 `bass_engine_ready` → 未触发（MISS）。
- 每轮把 `samples/mine_demo` 复制到不带 `.miacode/` 的新目录，临时把
  `%LOCALAPPDATA%\MiaCode\preferences.json` 的 `last_open_file` 指向副本，让 MiaCode 启动即自动打开它。
  每次运行后按字节恢复该文件并比对哈希，全部一致。
- 带 `--debug` 启动，`MIACODE_LOG_DIR` 指向本轮目录，约 30 秒后正常关闭窗口。
- 打开谱面本身就会初始化引擎，所以引擎级判定不需要点播放。
- 点播放：用 UI Automation 对 MiaCode 自己的播放按钮调用 Invoke，不抢焦点，也不发全局按键。
  v1 的播放按钮没有可访问名称，按顺序定位：它是播放条里名为 `1x` 的倍速按钮前面那一个，紧跟在停止按钮之后。
  日志里的 `play_request` 确认按下的是播放。
- 下文日志为脱敏节选：去掉了 pid、本地路径、音频端点 GUID 和音轨元数据。
  时间是 UTC，保留线程号。原始日志留在本机，未入库。

### 3.2 引擎级结果

| 轮次 | 波形来源 | 结果 | 波形请求 → 解码完成 | 波形请求 → 引擎初始化 |
| --- | --- | --- | --- | --- |
| 清缓存 5 轮 + 对照第 1 轮 | BASS 解码 | 6/6 `bind_failed err=37` | 0.19–0.21 s | 0.50–1.15 s |
| 对照第 2–3 轮（保留缓存） | 磁盘缓存 | 2/2 `bass_engine_ready` | — | 0.99–1.53 s |

典型时间线（清缓存第 2 轮）：

```text
05:52:01.486Z tid=22760 [audio/preview/waveform] event=request source=worker
05:52:01.680Z tid=26124 [audio/preview/waveform] event=build decoder=bass elapsed_ms=193.060
05:52:01.692Z tid=26124 [audio/preview/waveform] event=worker_done source=build elapsed_ms=205.465
05:52:01.986Z tid=19876 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
05:52:01.986Z tid=19876 [audio] bass_endpoint_bind_failed reason=disable_default err=37
05:52:01.986Z tid=19876 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
05:52:01.990Z tid=22760 [audio/preview/stage_media] action=set_chart_path chart=<chart> kind=none
```

对照（保留缓存的第 2 轮），没有 BASS 解码，引擎正常就绪：

```text
05:55:16.318Z tid=19344 [audio/preview/waveform] event=request source=worker
05:55:16.323Z tid=6304  [audio/preview/waveform] event=worker_done source=disk elapsed_ms=3.154
05:55:16.852Z tid=36276 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
05:55:17.847Z tid=36276 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint>
05:55:17.847Z tid=36276 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=994 ok=1 sample_rate=48000
```

### 3.3 点播放的症状

这次用的是真实的修复前二进制，不是 A/B 变体：

```text
05:57:59.686Z tid=25116 [audio/preview/waveform] event=build decoder=bass elapsed_ms=209.519
05:58:00.036Z tid=35156 [audio] bass_endpoint_bind_failed reason=disable_default err=37
05:58:00.036Z tid=35156 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
05:58:23.379Z tid=13108 [runtime/preview/interaction] action=toggle_entry op=1 playing=0 startup_pending=0 device_change_seq=0 pause_second=0.000000 authoritative_second=0.000000 pending_play_op=0
05:58:23.379Z tid=13108 [runtime/preview/interaction] action=play_request op=1 source=toggle_action requested_second=0.000000 device_change_seq=0
05:58:23.381Z tid=35156 [audio] bass_endpoint_bind_failed reason=disable_default err=37
05:58:23.381Z tid=35156 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
05:58:23.387Z tid=13108 [runtime/preview/interaction] action=play_dispatch op=1 source=toggle_action requested_second=0.000000 resume=1
05:58:23.394Z tid=13108 [audio/preview/playback] action=audio_startup_completion_failed kind=14 txn=1 generation=5 sequence=18 success=0 degraded=1 error=3 detail=preview audio backend is unavailable native_error=37
```

点播放后引擎重试初始化，仍然失败，音频启动报告失败。整个进程日志里没有 `audio_startup_completion_accepted`，
没有 `play_complete`，也没有任何 `bass_audio_health` 采样行，说明引擎从未运行。

## 4. 修复策略

原则：从源头消除时序依赖，让关闭 DEV_DEFAULT 成为进程里第一个 BASS 调用。引擎只读回这次结果。
改动与 `4d7c274e` 相同，已移植到 dev：

- 新增 `src/audio/PreviewBassDefaultDevice.h/.cpp`：
  - `disableBassDefaultDeviceEntry()` 持有每进程一次的状态，沿用 `std::call_once`。
    设置一旦关闭就无法重开，重试无效，所以只尝试一次。
  - 非 Windows 平台是返回 `true` 的空操作。
- `src/app/main.cpp`：
  - 在 `runStartupDiagnostic()` 之后、`MC_OP("main")` 和 `QApplication` 构造之前调用。
    此前没有任何代码会触及 BASS，`bass.dll` 按普通导入链接。
  - 结果写入启动 beacon：`phase=bass_default_device_entry disabled=%d err=%d`。
  - 对 GUI、CLI 导出和导出 worker 所有进程角色都生效。
- `src/audio/BassPreviewAudioBackend_EngineInit.cpp`：删除本地实现，改为读回共享结果。
  失败日志 `bass_endpoint_bind_failed reason=disable_default` 保持不变。
- 回归测试 `preview_audio_worker_spec`：
  - 源码顺序检查：`main()` 里的调用先于 `QApplication app(`。v1 用 `QApplication`，v2 用 `QGuiApplication`。
  - 引擎不再自行 `BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT`。
  - Windows 子进程行为：
    - 正向：启动调用 → 另一线程 no-sound 初始化 → 引擎时再调用仍成功。
    - 反向对照：先 no-sound 初始化，首次尝试被拒。
- 仓库指引 skill：三份 `cross-chain-linkage.md` 的第 11 节各加一条规则，
  即 BASS 进程级配置由 `main()` 最先设定，新增 BASS 消费者不得抢在它之前。

与 v2 的差异只有三处：spec 的应用构造 token 是 `QApplication app(`；spec 在 `CMakeLists.txt` 中注册；
三份 skill 副本已分叉，分别按各自格式添加。

未采用的方案：

- **让波形解码不走 BASS，或推迟到引擎之后**：只能避开这一个调用者，进程内导出等其他 BASS 使用者仍会抢先。
- **引擎失败后重试**：BASS 不会重新开放这个设置，重试无效。
- **改回 `BASS_Init(-1)`**：会回退 `2fd4f960` 解决的设备热插拔问题。

## 5. 修复后的日志证据（`fc155b03`）

验证构建：`fc155b03`，工作区干净（`git_dirty=0`）。方法同 §3.1。

### 5.1 启动 beacon

全部 11 个验证进程（§5.2 的 8 个 + §5.3 的 3 个）都记录了同一行，节选：

```text
phase=diag_end
phase=bass_default_device_entry disabled=1 err=0
phase=pre_mc_op
```

### 5.2 引擎级结果

| 轮次 | 波形来源 | 结果 | 波形请求 → 解码完成 | 波形请求 → 引擎就绪 |
| --- | --- | --- | --- | --- |
| 清缓存 5 轮 + 对照第 1 轮 | BASS 解码 | 6/6 `bass_engine_ready` | 0.20–0.23 s | 0.96–1.41 s |
| 对照第 2–3 轮（保留缓存） | 磁盘缓存 | 2/2 `bass_engine_ready` | — | 1.00 s |

所有轮次都没有出现 `bass_endpoint_bind_failed`。`bass_engine_ready` 在引擎初始化结束时记录，包含初始化耗时。

典型时间线（清缓存第 2 轮）。波形仍先在自己的线程上走 BASS 解码，顺序与修复前相同，但引擎正常绑定：

```text
06:07:41.205Z tid=23324 [audio/preview/waveform] event=request source=worker
06:07:41.422Z tid=23964 [audio/preview/waveform] event=build decoder=bass elapsed_ms=215.643
06:07:41.433Z tid=23964 [audio/preview/waveform] event=worker_done source=build elapsed_ms=227.126
06:07:41.721Z tid=24900 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
06:07:42.198Z tid=24900 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint>
06:07:42.198Z tid=24900 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=476 ok=1 sample_rate=48000
```

### 5.3 首次点播放

方法同 §3.3。3/3 次正常播放，节选其中一次：

```text
06:12:36.466Z tid=35616 [audio/preview/waveform] event=build decoder=bass elapsed_ms=221.313
06:12:37.607Z tid=18084 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint>
06:12:37.607Z tid=18084 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=839 ok=1 sample_rate=48000
06:13:00.212Z tid=32924 [runtime/preview/interaction] action=toggle_entry op=1 playing=0 startup_pending=0 device_change_seq=0 pause_second=0.000000 authoritative_second=0.000000 pending_play_op=0
06:13:00.212Z tid=32924 [runtime/preview/interaction] action=play_request op=1 source=toggle_action requested_second=0.000000 device_change_seq=0
06:13:00.218Z tid=32924 [runtime/preview/interaction] action=play_dispatch op=1 source=toggle_action requested_second=0.000000 resume=1
06:13:00.222Z tid=32924 [audio/preview/playback] action=audio_startup_completion_accepted kind=19 txn=1 generation=5 sequence=16 requested=0.000000 effective=0.000000
06:13:00.227Z tid=32924 [runtime/preview/interaction] action=play_complete op=1 source=toggle_action effective_second=0.000000 txn=1
```

- 3 次都出现了 `audio_startup_completion_accepted` → `play_complete`，且没有 `audio_startup_completion_failed`。
- 播放后的音频健康采样（`bass_audio_health`）显示 `txn=1 mixer_active=playing bgm_active=playing underrun=0`，
  其 `second` 字段在相隔 5.006 秒的两次采样之间前进 5.005 秒，确实在播放。

### 5.4 自动化测试

- `ctest -R '^preview_audio_worker_spec$'` 通过（约 4 s）。两个子进程模式单独运行时，退出码都是 0。
- 变异验证：临时把 `main()` 里的调用改名后，spec 按预期失败在
  `main() settles DEV_DEFAULT before the application can reach BASS`。还原后重新通过。

## 6. 遗留与后续

- 已发布的 v1 版本（含 1.1.0-beta.12）仍有这个问题，需要发新版。
  临时规避：出现无响应时重启一次（此时波形缓存已生成），或先打开一个已有波形缓存的谱面。
- 首次播放探针脚本（`scripts/debug/first_play_probe/`）只在 `feature/qml-ui` 上，未移植到 dev。
  它的判定对 v1 同样适用：修复后的版本每轮都应是 `VERDICT=MISS`。
- 今后新增 BASS 进程级配置，都应放进同一个启动入口，不要在各自子系统里懒设置。
