# 首次播放失败探针（Windows）

用来验证这个问题：**打开 MiaCode 后第一次点播放没反应（按钮不变、画面不走），必须重启才能恢复**，而且只在 Windows 上出现。

原因（已确认，`4d7c274e` 已修复，详见[根因与修复记录](../../../docs/audit/PREVIEW_FIRST_PLAY_DEV_DEFAULT_ROOT_CAUSE_AND_FIX_ZH.md)）：
在 Windows 上，每个进程只会尝试一次关闭 BASS 的"跟随系统默认设备"模式，而这一步必须在进程里第一次
BASS 设备枚举或初始化之前完成。修复前这一步在预览引擎初始化时才做。打开一个没有波形缓存的谱面时，
波形解码会先初始化 BASS 的 no-sound 设备，这一步就会失败，之后整个进程里预览音频都起不来，播放也就开始不了。
重启后波形缓存已经存在，所以问题会消失。修复后 MiaCode 在启动时就完成这一步。

实测这不是偶发竞态：修复前，只要进程打开的第一个谱面没有波形缓存，就一定会触发。

## 测试员怎么跑

1. 拉取仓库最新代码（或者只拿到 `scripts/debug/first_play_probe/` 这个文件夹）。
2. 准备好要测的 MiaCode 版本（安装或解压后的目录），以及一个带音频的谱面文件夹（包含 `maidata.txt`），
   最好是这台电脑上没打开过的谱面。
3. 双击 `Run_FirstPlayProbe.bat`，按提示操作：
   - 选择 MiaCode 所在文件夹（选择框可能被挡在黑色窗口后面）。
   - **第一阶段**全自动运行，大约十几秒。
   - **第二阶段**选择谱面文件夹。脚本会把谱面复制到一个全新目录（不带缓存），用 `--debug` 启动 MiaCode，
     然后提示你：打开谱面 → **一加载出来就立刻点播放** → 看有没有反应 → 退出 MiaCode → 回到窗口按回车 → 回答 y/n。
   - 修复前的版本从第 2 轮起基本每轮都会触发；`4d7c274e` 之后的版本每轮都应该正常。建议跑 3 轮。
     从第 2 轮开始，MiaCode 会自动打开测试谱面，只需要启动后立刻点播放。
4. 结束后，桌面上会生成 `miacode_first_play_probe_<时间>.zip`，把它发回来。
   zip 里只有日志和结果，不包含谱面和音频。

跑之前请先完全退出 MiaCode。

## 结果怎么读（开发者）

`summary.txt` 里：

| 行 | 含义 |
| --- | --- |
| `STAGE1=CONFIRMED` | 在这台机器的 bass.dll 上，`BASS_Init(0)` 之后 `BASS_CONFIG_DEV_DEFAULT` 就改不了了。库层面的机制成立 |
| `STAGE1=REFUTED` | Windows 上 `BASS_Init(0)` 不影响这个设置，假设不成立，需要另找原因 |
| `VERDICT=HIT` | 这一轮日志里出现了 `bass_endpoint_bind_failed reason=disable_default`，预览引擎初始化失败。对 `4d7c274e` 之后的版本，这表示启动顺序回归 |
| `VERDICT=MISS` | 这一轮预览引擎正常初始化（`bass_engine_ready`），没有触发。修复后的版本应该每轮都是这个结果 |
| `NOTE: failure observed without ...` | 测试员看到播放失败，但日志里没有这个失败记录，说明还有其他原因，重点看这一轮 |

每轮的完整日志在 `round_N/` 目录下（`MIACODE_LOG_DIR` 指向这里）。
`stage1_bass_probe.txt` 是每个场景的原始输出。
第一阶段各个场景的含义见 `../bass_sync_probe/dev_default_probe.cpp`，它是同一个探针的 C++ 版本，给 macOS/Linux 开发机用。

## 参数

```powershell
powershell -ExecutionPolicy Bypass -File FirstPlayProbe.ps1 -AppDir "D:\MiaCode" -ChartDir "D:\charts\song"
powershell -ExecutionPolicy Bypass -File FirstPlayProbe.ps1 -ProbeOnly   # 只跑第一阶段
```

不传 `-AppDir` 且取消选择时，第一阶段会改用仓库里的 `third_party/bass/bin/<arch>/bass.dll`。
