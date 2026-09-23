---
lifecycle: working
---

# Windows 首次播放无响应：BASS DEV_DEFAULT 时序根因与修复

- 日期：2026-09-23
- 分支：`feature/qml-ui`
- 修复提交：`4d7c274e`（`fix(audio): disable BASS DEV_DEFAULT at startup, before any BASS use`）
- 配套提交：`f6968af7`（修复 `preview_audio_worker_spec` 的编译，回归测试依赖它）
- 探针：`5fc77405` 引入的 [first_play_probe](../../scripts/debug/first_play_probe/README_ZH.md)

## 结论

Windows 预览音频引擎要绑定到具体的 Core Audio 输出端点，前提是先用
`BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE)` 关闭 BASS 的"跟随系统默认设备"。这个设置是进程级的：
进程里第一次 BASS 设备枚举或 `BASS_Init` 之后就再也改不了，任何设备（包括 0 号 no-sound 设备）、
任何线程的调用都算。修复前，这一步只在预览引擎初始化时尝试一次，并用 `std::call_once` 缓存结果。
打开没有波形缓存的谱面时，波形解码线程会先 `BASS_Init(0)`，引擎那一次尝试必然以 `err=37`
（`BASS_ERROR_NOTAVAIL`）失败。失败结果被缓存后，本进程内每次引擎初始化都直接失败，
包括点播放触发的那一次，所以播放无响应，只能重启。重启后波形从磁盘缓存读取，不再走 BASS 解码，
引擎成为第一个 BASS 使用者，问题"消失"。

这在实测中是确定性的，不是偶发竞态：只要进程打开的第一个谱面没有波形缓存就必现。
修复把这一步挪到 `main()` 最开始、任何 BASS 调用之前，引擎只读回结果。

| | 修复前（3 个构建） | 修复后（`4d7c274e`） |
| --- | --- | --- |
| 打开无缓存谱面，引擎初始化 | 17/17 失败（`bind_failed err=37`） | 6/6 成功（`bass_engine_ready`） |
| 保留缓存的对照轮 | 4/4 成功 | 2/2 成功 |
| 打开无缓存谱面后立即点播放 | 1/1 无响应（A/B，见 §3.4） | 4/4 正常播放 |
| 启动 beacon | — | 12/12 进程 `disabled=1 err=0` |

## 1. 现象

- 打开 MiaCode 后第一次点播放没反应（按钮不变、画面不走），必须重启才能恢复；只在 Windows 上出现。
- 触发条件：这个进程打开的第一个谱面在 `.miacode/` 下没有波形缓存（新谱面，或清过缓存）。
- 影响版本：包含 `2fd4f960`（2026-08-10，`fix(audio): make device hotplug cutoff immediate`，
  引入具体端点绑定和 DEV_DEFAULT 关闭）、不包含 `4d7c274e` 的 Windows 构建。实测已发布的
  1.1.0-beta.12、本地 1.1.0-beta.15（`03399249`）和 `5fc77405` 都会触发。
- macOS / Linux 不受影响：BASS 在 macOS 上的库行为相同（见
  [bass_sync_probe](../../scripts/debug/bass_sync_probe/README.md)），但只有 Windows 分支依赖这个设置。

## 2. 根因

### 2.1 BASS 库行为

`BASS_CONFIG_DEV_DEFAULT` 只在进程里还没有发生任何 BASS 设备枚举或初始化时可以修改。
第一阶段探针对每个场景单独起进程测试，两个 BASS 版本结果一致：

| 场景（全新进程） | `set_ok` | 错误码 |
| --- | --- | --- |
| 什么都不做，直接设置 | 1 | 0 |
| `BASS_Init(0)` 之后（设备仍持有） | 0 | 37 |
| `BASS_Init(0)` + `BASS_Free` 之后 | 0 | 37 |
| 另一个线程 `BASS_Init(0)` 之后 | 0 | 37 |
| 只调用 `BASS_GetDeviceInfo` 枚举设备之后 | 0 | 37 |
| `BASS_PluginLoad` 之后 | 1 | 0 |
| 先设置，再 `BASS_Init(0)`，读回 `BASS_GetConfig` | 设置成功，读回 0（保持关闭） | — |

- 版本：bass.dll 2.4.18.3（`0x02041203`，仓库 `third_party/bass`，HEAD 随包版本）和 2.4.17.0
  （`0x02041100`，1.1.0-beta.12 / beta.15 随包版本），x64，Windows 11 10.0.26200。
- 错误码 37 即 `BASS_ERROR_NOTAVAIL`。`BASS_Free` 不会重新开放这个设置，跨线程同样生效，
  只做设备枚举也会关闭它；加载插件不会。

### 2.2 修复前的代码时序

1. 打开谱面后，波形 worker 立即请求波形。缓存未命中时，`src/common/WaveformCache.cpp` 的
   `ScopedBassWaveformDecodeDevice` 通过 `PreviewBassDeviceLease` 在波形线程上
   `BASS_Init(0, …, BASS_DEVICE_NOSPEAKER)` 解码。
2. 预览音频引擎要等 `set_chart_path` → `reload_assets` 之后才初始化
   （`BassPreviewAudioBackend::initializeAudioEngine()`）。实测比波形请求晚 0.7–6.4 秒，
   比 BASS 解码完成还晚。
3. 修复前，引擎初始化里的 `disableBassDefaultDeviceEntry()` 是进程内唯一一次
   `BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE)`，此时设置已被波形解码关闭，失败并缓存 `err=37`。
4. 点播放时，`preparePreviewPlaybackTransaction()` 再次调用 `initializeAudioEngine()`，
   读到缓存的失败直接返回，播放启动报告 `preview audio backend is unavailable native_error=37` 并取消。

除波形解码外，进程内另一个会先初始化 BASS 设备的是进程内导出音频
（`src/tools/video_export/BassExportAudioBackend.cpp`，同样 `BASS_Init(0)`）。修复同样覆盖它。

### 2.3 为什么不是竞态

波形的 BASS 解码在谱面加载时立即开始，引擎初始化至少要晚 0.7 秒（§3.2）。
点播放要等谱面加载出来，实际上无法抢在波形解码前面。因此只要第一个谱面没有缓存，这条路径每次都会触发
（§3.2 中 17 个无缓存轮全部触发）。
探针 README 原先"竞态、不一定每轮触发"的说法偏保守，已同步更新。

## 3. 修复前的运行时日志证据

### 3.1 验证方法

- 第二阶段采用无人值守方式，判定规则与探针 `FirstPlayProbe.ps1` 的 `Get-RoundVerdict` 一致：
  - 出现 `bass_endpoint_bind_failed reason=disable_default` → `HIT`
  - 出现 `bass_engine_ready` → `MISS`
- 每轮把 `samples/mine_demo`（track.mp3 7.6 MB，149.8 s）复制到不带 `.miacode/` 的新目录，
  临时把 `%LOCALAPPDATA%\MiaCode\preferences.json` 的 `last_open_file` 指向副本，
  让 MiaCode 启动即自动打开它。每次运行后按字节恢复该文件并比对哈希，全部一致。
- 带 `--debug` 启动，`MIACODE_LOG_DIR` 指向本轮目录，约 30 秒后正常关闭窗口。
- 打开谱面本身就会初始化引擎，所以引擎级判定不需要点播放。点播放的验证见 §3.4 和 §5.4。
- 下文日志为脱敏节选：去掉了 pid、本地路径、音频端点 GUID 和音轨元数据。
  时间是 UTC，保留线程号以显示跨线程关系。原始日志留在本机，未入库。

### 3.2 引擎级结果

| 构建 | 清缓存轮 | 保留缓存对照 | 波形请求 → 引擎初始化 |
| --- | --- | --- | --- |
| 打包版 1.1.0-beta.12（经启动器启动，Qt 6.8.3） | 3/3 HIT | — | 0.70–1.16 s |
| 本地 1.1.0-beta.15 @ `03399249`（Qt 6.8.3） | 6/6 HIT | 第 1 轮 HIT，第 2–3 轮 MISS | 1.85–6.42 s |
| HEAD `5fc77405`（Qt 6.11.1，与 CI 一致） | 6/6 HIT | 第 1 轮 HIT，第 2–3 轮 MISS | 1.35–2.79 s |

对照轮第 2–3 轮缓存命中（`event=worker_done source=disk`），全程没有 BASS 解码，引擎正常就绪。
这说明触发条件就是"引擎之前有人初始化了 BASS 设备"。

### 3.3 典型时间线（HEAD `5fc77405`，清缓存第 1 轮）

```text
04:23:01.280Z tid=5500 [audio/preview/waveform] event=request source=worker
04:23:01.497Z tid=788  [audio/preview/waveform] event=build decoder=bass elapsed_ms=213.778
04:23:01.524Z tid=788  [audio/preview/waveform] event=worker_done source=build elapsed_ms=241.001
04:23:02.676Z tid=5500 [audio/preview/stage_media] action=set_chart_path chart=<chart> media=(none) kind=none
04:23:02.691Z tid=4768 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
04:23:02.691Z tid=4768 [audio] bass_endpoint_bind_failed reason=disable_default err=37
04:23:02.691Z tid=4768 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
```

波形在 788 号线程完成 BASS 解码，约 1.2 秒后引擎在 4768 号线程初始化，DEV_DEFAULT 已无法关闭。

对照（同一构建，保留缓存的第 2 轮）：

```text
04:26:56.139Z tid=796   [audio/preview/waveform] event=request source=worker
04:26:56.158Z tid=25996 [audio/preview/waveform] event=worker_done source=disk elapsed_ms=16.270
04:26:57.558Z tid=796   [audio/preview/stage_media] action=set_chart_path chart=<chart> media=(none) kind=none
04:26:57.570Z tid=30464 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
04:26:58.697Z tid=30464 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint> …
04:26:58.697Z tid=30464 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=1127 ok=1 sample_rate=48000
```

### 3.4 点播放的症状（A/B）

- 用 UI Automation 调用 MiaCode 自己窗口里名为「播放」的按钮（Invoke），不抢焦点，也不发全局按键。
- 1.1.0-beta.12 的播放按钮没有可访问名称，无法可靠定位，因此改用 A/B：
  以 `4d7c274e` 为基础，只把 `main()` 里的启动调用临时换成空操作，其余二进制相同，时序等价于修复前。
  这个版本的启动 beacon 为 `disabled=0 err=-1`，未提交。

```text
05:04:04.114Z tid=792   [audio/preview/waveform] event=build decoder=bass elapsed_ms=227.516
05:04:05.430Z tid=24400 [audio] bass_endpoint_bind_failed reason=disable_default err=37
05:04:05.430Z tid=24400 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
05:04:27.937Z tid=7984  [runtime/preview/interaction] action=play_request op=1 source=toggle_action requested_second=0.000000 device_change_seq=0
05:04:27.938Z tid=24400 [audio] bass_endpoint_bind_failed reason=disable_default err=37
05:04:27.938Z tid=24400 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=0 ok=0 reason=disable_default err=37
05:04:27.941Z tid=7984  [runtime/preview/interaction] action=play_dispatch op=1 source=toggle_action requested_second=0.000000 resume=1
05:04:27.943Z tid=7984  [audio/preview/stage_media] action=prepare_playback_start txn=1 second=0.000000 rate=1.000 offset=0.000000 has_video=0
05:04:27.944Z tid=7984  [audio/preview/playback] action=audio_startup_completion_failed kind=14 txn=1 generation=6 sequence=18 success=0 degraded=1 error=3 detail=preview audio backend is unavailable native_error=37
05:04:27.944Z tid=7984  [audio/preview/stage_media] action=prepare_playback_cancel txn=1 pending=0 ready=0
```

点播放后引擎重试初始化，仍然失败，播放启动被取消。日志里没有 `play_complete`，也没有 `playback_summary`，
与"按钮不变、画面不走"一致。

## 4. 修复策略

原则：从源头消除时序依赖，让关闭 DEV_DEFAULT 成为进程里第一个 BASS 调用。引擎只读回这次结果。

改动（`4d7c274e`）：

- 新增 [`src/audio/PreviewBassDefaultDevice.h`](../../src/audio/PreviewBassDefaultDevice.h) 和 `.cpp`：
  - `disableBassDefaultDeviceEntry()` 持有每进程一次的状态，沿用 `std::call_once`。设置一旦关闭就无法重开，
    重试只会覆盖真正有意义的错误码，所以只尝试一次。
  - 非 Windows 平台是返回 `true` 的空操作。
  - 放在 `src/audio/`，符合"BASS 访问留在音频边界"的约定。
- [`src/app/main.cpp`](../../src/app/main.cpp)：
  - 在 `runStartupDiagnostic()` 之后、`MC_OP("main")` 和 `QGuiApplication` 构造之前调用。
    此前没有任何代码会触及 BASS：`src/app` 不直接调用 BASS，`bass.dll` 按普通导入链接，不存在延迟加载。
  - 结果写入启动 beacon：`phase=bass_default_device_entry disabled=%d err=%d`。
  - 对 GUI、CLI 导出和导出 worker 所有进程角色都生效，代价只是一次进程内配置调用。
- `src/audio/BassPreviewAudioBackend_EngineInit.cpp`：删除本地实现，改为读回共享结果。
  失败日志 `bass_endpoint_bind_failed reason=disable_default` 保持不变，探针脚本依赖它。
- 回归测试 `preview_audio_worker_spec`：
  - 源码顺序检查：`main()` 里的调用先于 `QGuiApplication app(`，引擎不再自行
    `BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT`。
  - Windows 子进程行为：
    - 正向：启动调用 → 另一线程 no-sound `BASS_Init(0)` → 引擎时再调用仍然成功，
      `BASS_GetConfig` 读回 0。
    - 反向对照：先 no-sound 初始化，首次尝试被拒。这证明测试能区分修复前后的时序。
- 仓库 skill 的跨模块边界增加一条：BASS 进程级配置由 `main()` 最先设定，新增 BASS 消费者不得抢在它之前。

未采用的方案：

- **让波形解码不走 BASS，或推迟到引擎之后**：只能避开这一个调用者。进程内导出和将来任何新的 BASS 使用者
  （包括只做设备枚举的界面）都会再次抢先。
- **引擎失败后重试**：BASS 不会重新开放这个设置，重试无效。
- **放弃具体端点绑定，改回 `BASS_Init(-1)`**：会回退 `2fd4f960` 解决的设备热插拔问题。

配套的 `f6968af7`：`ec5cb0e2` 给 `common/ChartAssetPaths.h` 加了 `<QImage>` 之后，
`preview_audio_worker_spec` 在所有 BASS 平台上都无法编译，只链接了 `Qt6::Core`。
没有 CI 运行 ctest，所以一直未被发现。修复方法是为它补上 `Qt6::Gui`。

## 5. 修复后的日志证据

验证构建：`4d7c274e`（Qt 6.11.1，MSVC 2022，bass.dll 2.4.18.3）。方法同 §3.1。
工作区只有与本修复无关的未跟踪文件，因此 `git_dirty=1`。

### 5.1 启动 beacon

全部 12 个验证进程（§5.2 的 8 个 + §5.4 的 4 个）都记录了同一行，节选：

```text
phase=bass_default_device_entry disabled=1 err=0
phase=pre_mc_op
```

### 5.2 引擎级结果

| 构建 | 清缓存轮 | 保留缓存对照 | 波形请求 → 解码完成 | 波形请求 → 引擎就绪 |
| --- | --- | --- | --- | --- |
| `4d7c274e` | 5/5 MISS（`bass_engine_ready`） | 3/3 就绪（第 1 轮走 BASS 解码） | 0.20–0.24 s | 2.13–2.29 s |

- 所有轮次都没有出现 `bass_endpoint_bind_failed`。
- `bass_engine_ready` 在引擎初始化结束时记录，包含 0.8–0.9 秒初始化耗时，所以比修复前失败行晚。
- 提交前的同一份代码（`0c81a035` 加未提交修改）另跑了 5+3 轮，结果相同。

### 5.3 典型时间线（`4d7c274e`，清缓存第 1 轮）

```text
04:54:56.650Z tid=5068  [audio/preview/waveform] event=request source=worker
04:54:56.892Z tid=25056 [audio/preview/waveform] event=build decoder=bass elapsed_ms=240.052
04:54:56.926Z tid=25056 [audio/preview/waveform] event=worker_done source=build elapsed_ms=273.632
04:54:58.091Z tid=5068  [audio/preview/stage_media] action=set_chart_path chart=<chart> media=(none) kind=none
04:54:58.104Z tid=34064 [audio] bass_init op=invalidate_retained_state reason=reload_assets previous=invalidated
04:54:58.936Z tid=34064 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint> …
04:54:58.936Z tid=34064 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=832 ok=1 sample_rate=48000
```

波形仍然先在自己的线程上走 BASS 解码，顺序与修复前相同。区别只在于 DEV_DEFAULT 已在启动时关闭，
引擎正常绑定到具体端点。

### 5.4 首次点播放

同 §3.4，用 UI Automation 在引擎就绪后调用「播放」。修复后 4/4 次正常播放，节选其中一次：

```text
05:05:30.502Z tid=27540 [audio] bass_engine_ready sample_rate=48000 output_index=1 output_endpoint=<endpoint> …
05:05:30.502Z tid=27540 [audio] bass_init op=initialize_audio_engine reused=0 elapsed_ms=867 ok=1 sample_rate=48000
05:05:52.341Z tid=20776 [runtime/preview/interaction] action=play_request op=1 source=toggle_action requested_second=0.000000 device_change_seq=0
05:05:52.344Z tid=20776 [runtime/preview/interaction] action=play_dispatch op=1 source=toggle_action requested_second=0.000000 resume=1
05:05:52.347Z tid=20776 [audio/preview/stage_media] action=prepare_playback_start txn=1 second=0.000000 rate=1.000 offset=0.000000 has_video=0
05:05:52.368Z tid=27540 [audio] bass_prepare txn=1 start=0.000000 resume=1 rate=1.000 groups=70
05:05:52.370Z tid=20776 [audio/preview/playback] action=audio_startup_completion_accepted kind=14 txn=1 generation=6 sequence=16 requested=0.000000 effective=0.000000
05:05:52.371Z tid=20776 [runtime/preview/interaction] action=play_complete op=1 source=toggle_action effective_second=0.000000 txn=1
05:05:58.577Z tid=20776 [runtime/ui/v2_preview_probe] action=playback_summary shell_state_changes=364 …
```

4 次都出现了 `audio_startup_completion_accepted` → `play_complete`，且没有 `audio_startup_completion_failed`。
点播放后约 6 秒内的 `shell_state_changes` 为 363–364，播放头确实在走。

### 5.5 自动化测试

- `ctest -R '^preview_audio_worker_spec$'` 通过（约 4 s）。两个子进程模式单独运行时，退出码都是 0。
- 变异验证：临时把 `main()` 里的调用改名后，这个 spec 按预期失败在
  `main() settles DEV_DEFAULT before the application can reach BASS`。还原后重新通过。

## 6. 遗留与后续

- 已发布的 Windows 版本仍有这个问题，需要发新版。临时规避：出现无响应时重启一次（此时波形缓存已生成），
  或先打开一个已有波形缓存的谱面。
- 对 `4d7c274e` 之后的版本，探针第二阶段每轮都应是 `VERDICT=MISS`。出现 `HIT` 即表示启动顺序回归。
  探针 README 和 [调试索引](../ops/DEBUG_INDEX.md) 已同步。
- 今后新增 BASS 进程级配置，都应放进同一个启动入口，不要在各自子系统里懒设置。
