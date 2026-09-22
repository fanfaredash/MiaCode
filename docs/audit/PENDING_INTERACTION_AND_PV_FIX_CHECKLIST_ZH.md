# 待实施交互与 PV 修复清单

- 建立日期：2026-09-22
- 代码基线：`dev` `28e3b71b`
- 总体状态：代码实施与自动验证完成；实机验收待完成
- 执行方式：三项作为同一批次完成；每项独立实现、独立验证，最后统一做 Release 构建与回归

## 范围与顺序

- [x] A. 导出页文本输入焦点下放行空格
- [x] B. 从资源管理器拖入 `maidata.txt` 或谱面文件夹并打开
- [x] D. 为实时预览 PV 增加基于歌曲权威时钟的稳态同步纠偏
- [ ] E. 完整差异审阅、Release 构建、CTest 与实机回归（自动项完成，实机项待完成）

推荐按 A → B → D 实施。A 的范围最小，可先建立键盘事件回归基线；B 属于窗口交互；D 涉及播放状态机和真实媒体验证，风险最高，最后接入。

## A. 导出页空格输入与播放快捷键冲突

### 已复核结论

- [x] `VideoExportDialog` 将自身安装为应用级事件过滤器。
- [x] 无修饰空格会匹配面板级播放/暂停动作。
- [x] 当前过滤器在 `ShortcutOverride`、`KeyPress`、`KeyRelease` 阶段消费该按键，没有排除可编辑文本控件。
- [x] QuickShell 另有 `Qt.ApplicationShortcut` 范围的空格播放快捷键，因此只放行 `KeyPress` 不够；文本焦点必须接受 `ShortcutOverride`，同时让实际按键继续送达编辑器。

### 实施清单

- [x] 在 `VideoExportDialog.ExportFlow.cpp` 增加集中式“可编辑文本焦点”判断。
- [x] 覆盖非只读 `QLineEdit`、`QTextEdit`、`QPlainTextEdit`、`QAbstractSpinBox` 编辑器和可编辑 `QComboBox` 编辑器。
- [x] 文本焦点下的无修饰空格：接受 `ShortcutOverride` 以屏蔽全局快捷键，但不消费后续 `KeyPress` / `KeyRelease`。
- [x] 非文本焦点下维持现有空格播放/暂停行为和自动重复保护。
- [x] `Ctrl+Shift+C`、`Ctrl+Shift+X` 等显式预览快捷键维持现有行为。
- [x] 优先复用现有 `commitFocusedEditorOnReturn()` 的焦点归属经验，不扩散新的全局快捷键框架。

### 验收

- [ ] 输出路径、片头背景路径和流速编辑框可输入空格，播放状态不改变。
- [ ] 输入框外按一次空格只切换一次播放状态。
- [ ] 单谱导出页、批量共享设置面板和独立弹窗行为一致。
- [ ] QuickShell 全局空格快捷键不会在输入框场景二次触发。

## B. 拖入现有谱面并打开

### 已复核结论

- [x] QuickShell 原生桥接表面和拖放遮罩已经注册为 drop target。
- [x] 当前窗口级拖放只识别音频，用于创建新谱面工程。
- [x] `openStartupTarget()` 已支持“文件夹 → 根目录 `maidata.txt`”，但启动入口不承担运行期间的未保存确认，不能直接作为拖入打开入口。

### 实施清单

- [x] 在现有拖放事件链中统一分类本地 URL，不新增 Windows 专用旁路。
- [x] 支持单个、文件名大小写不敏感的 `maidata.txt`。
- [x] 支持单个根目录直接包含 `maidata.txt` 的文件夹；不递归搜索子目录。
- [x] 保留音频拖入创建谱面的现有行为。
- [x] 多个谱面目标或“谱面 + 受支持音频”混合拖入时给出明确冲突提示，不静默忽略部分内容。
- [x] 增加运行期间安全打开入口：先 `maybeSaveBeforeContinue()`，再复用 `openFileAtPath()`。
- [x] 拖放遮罩区分“打开现有谱面”和“从音频创建谱面”，补齐中/英/日文案。
- [x] 对路径做规范化和大小写不敏感去重。

### 验收

- [ ] 从资源管理器拖入 `maidata.txt` 或对应文件夹可打开谱面。
- [ ] 未保存文档的保存、放弃和取消三条路径均正确；取消时当前谱面完全不变。
- [ ] 拖到编辑区、预览区、侧栏及原生嵌入表面均可识别。
- [ ] 音频拖入创建谱面的既有功能无回归。

## D. 实时预览 PV 稳态同步纠偏

### 已复核结论

- [x] 播放中的歌曲/BASS 混音器时钟是预览权威时钟，并在每个预览 tick 传给 `PreviewStageMediaHost::syncPlayback()`。
- [x] QtAVPlayer 稳态分支只确保播放器处于播放状态，不依据 PV 与歌曲的时间差持续纠偏。
- [x] `clockDeltaSeconds_` 当前只用于诊断，没有驱动同步动作。
- [x] QtAVPlayer 路径不使用 QMediaPlayer 的常规无新帧 watchdog。
- [x] 当前 `clockDeltaSeconds_` 使用 `player_->position()`；QtAVPlayer 已另行保存 `lastFramePtsSeconds_`。同步策略应优先使用有效的最后送显帧 PTS，播放器位置只作为没有帧 PTS 时的兼容观测，二者需在日志中分开记录。
- [ ] 使用用户复现素材和 Audio 日志确认偏差属于固定起播落后、线性累积、卡顿后台阶或特定倍率问题；在此之前保持“高置信度根因假设”，不标记为实机确认。

### 实施清单

- [ ] 先建立同素材实时预览/导出对照，覆盖从头、中间、暂停恢复、seek 后播放及 0.5×/1×/1.5×。
- [ ] 分别记录歌曲权威时间、播放器位置、最后送显帧 PTS、时间线偏移、倍率、解码模式和播放事务。
- [x] 提取纯同步策略，以偏差观测、帧时长、播放资格状态和单调时间为输入；播放事务、倍率、seek/恢复/尾部状态由宿主负责重置或门控。
- [x] 策略输出限定为保持、累计确认、单次重锚；使用持续越界、迟滞和冷却，避免 seek 风暴。
- [x] 在起播准备、paused seek、恢复 seek、换源、倍率切换、事务变化、负偏移等待和 EndOfMedia 处理期间禁用稳态纠偏并重置策略状态。
- [x] 歌曲/BASS 保持唯一权威时钟；重锚目标继续遵守 `timelineOffsetSeconds_`。
- [x] 第一版只做离散重锚。仅当日志证明存在稳定线性速率差时，再单独评估临时速率微调。
- [x] 沿用 `Audio / preview/stage_media` 日志通道，低频记录采样，完整记录每次实际纠偏。
- [x] 不改变离线导出的逐帧时间线。

### 测试与验收

- [x] 新增纯策略测试：单帧抖动、持续落后/超前、冷却、再次纠偏和状态重置；负偏移及媒体尾部由宿主门控契约覆盖。
- [x] 扩展 QtAVPlayer 平台契约测试，确认稳态路径接入策略且不回退到每 tick seek。
- [x] 自动回归起播落点、paused seek、EndOfMedia、倍率切换、音频权威时钟和导出时间线相关测试；真实素材播放仍见下方实机项。
- [ ] 1× 连续播放时偏差不再持续增长；卡顿后的持续偏差能在一次受控重锚后收敛。
- [ ] 稳定播放窗口内以分位数衡量偏差：目标 P95 约不超过两个视频帧；不要求每个采样点都小于该值。
- [ ] 无连续 seek、画面闪回、首帧反复解码或媒体尾部恢复回归。

## E. 统一验证与完成条件

- [x] 审阅完整差异，确认每处修改都能追溯到以上三项之一。
- [x] 运行 `git diff --check`。
- [x] 使用 `powershell -ExecutionPolicy Bypass -File "C:\Users\kanago\Desktop\build_miacode_release.ps1"`，确认 Release 构建落在 `build-devtools`。
- [x] 运行新增测试及相关 preview、QtAVPlayer、EndOfMedia、视频导出、UI 文案测试，使用 `ctest -C Release`。
- [ ] 完成 Windows 资源管理器拖放、导出输入框和真实 PV 的手工验收。
- [x] 若功能归属或同步关系改变，同步更新 `feature-index.md`、`cross-chain-linkage.md` 和必要的诊断文档。
- [ ] 所有自动及手工验收通过后，才将本清单总体状态改为“完成”。

## 本轮自动验证记录（2026-09-22）

- Release：规定脚本构建成功，产物为 `build-devtools/Release/MiaCode.exe`。
- CTest：最终全量复跑 69/69 通过。
- 说明：首轮全量测试中既有 `preview_audio_worker_spec` 出现一次异步健康快照失败；单测复跑通过，随后全量复跑 69/69 通过，未修改音频 worker 代码。
- `git diff --check`：通过。
