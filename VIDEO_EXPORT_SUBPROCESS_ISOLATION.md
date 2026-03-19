# 导出子进程隔离方案（内存快照版）

## 目标

本方案用于把视频导出从主 UI 进程中拆出去，并满足下面两个前提：

- 导出时，用户仍然可以继续编辑谱面并继续实时预览。
- 谱面快照不落盘；导出时直接把当前内存中的谱面副本发送给子进程。

这里默认接受一个现实前提：导出子进程与实时预览会产生双份渲染/解码压力，但当前阶段不优先优化这一点。

## 现状与问题

当前导出链路本质上仍然依赖主窗口里的 live 状态：

- `MainWindow::exportPreviewVideoFromCli()` 会把 `previewStatsNoteMarkers_`、`previewAudioSettings_`、`previewCanvas_` 等状态直接塞给导出任务。
- `VideoExportController::exportFullPreview()` 会基于 `sourceCanvas` 执行 `copyRenderStateFrom(*sourceCanvas)`，并尝试共享 OpenGL context。

这意味着当前导出不是“基于某一时刻快照的独立渲染”，而是“从主窗口当前的预览状态继续延伸出来的一次导出”。如果用户在导出期间继续编辑、切难度、改预览参数、seek、暂停/播放，就会出现两个问题：

- 导出输入不是稳定的，不满足“导出基于导出按钮按下瞬间的内容”。
- 导出和实时预览共享了部分渲染状态与缓存，进程内隔离不彻底。

## 结论

推荐方案是：

- 保留“同一个 `MiaCode.exe` 启动子进程”的思路。
- 不再复用当前 `--export-video` 文件输入模式做 GUI 导出。
- 新增一个专用 worker 模式，例如 `--export-video-worker`。
- 主进程在点击导出后，把当前导出所需的最小不可变快照直接序列化到内存，并通过 `QProcess` 的标准输入发给 worker。
- worker 只基于这份内存快照重建本地导出环境，不读取磁盘上的谱面文件，不依赖主窗口对象。

一句话概括边界：

- 谱面内容走内存。
- 媒体资源走显式磁盘路径。
- 渲染状态在 worker 内部重建。

## 为什么这里不建议用落盘 snapshot

本需求下，谱面文件通常很小，导出所需的控制参数也很小，因此没必要把 snapshot 写成临时 `maidata.txt` 或临时 job 目录文件。直接走进程间内存传输更合适，理由如下：

- 数据量小：谱面文本、导出参数、音量/渲染设置通常只有 KB 级到几十 KB。
- 生命周期短：快照只需要覆盖“点击导出”到“worker 完成初始化”这一小段时间。
- 清理简单：不需要回收残留临时 snapshot 文件。
- 语义更清晰：可以硬性保证 worker 不会再回头读取磁盘上的旧谱面。

在这个前提下，首选 IPC 不是共享内存，而是 `QProcess` 的 stdin/stdout：

- 实现复杂度最低。
- 现有代码里已经有 `QProcess + JSON line` 的通信习惯，可复用思路。
- 不需要处理 `QSharedMemory` 的命名、清理、权限和跨实例冲突。

只有在未来 snapshot 明显膨胀，或者需要高频双向大对象传输时，再考虑切到共享内存或本地 socket。

## 方案总览

### 主进程负责

主进程只做四件事：

1. 收集导出参数。
2. 把当前编辑状态同步进内存 document，但不保存到原文件。
3. 生成不可变的 `ExportJobSnapshot`，通过 stdin 发给 worker。
4. 展示进度、处理取消、汇总结果。

主进程不再负责：

- 直接执行导出渲染。
- 把 `previewCanvas_` 传进导出控制器。
- 把 `previewStatsNoteMarkers_` 当作导出输入复用给 worker。

### Worker 负责

worker 只做五件事：

1. 从 stdin 接收并解析 snapshot。
2. 基于 snapshot 中的谱面文本重建本地 `SimaiDocument`。
3. 自己重新解析并生成 `noteMarkers` / timeline 数据。
4. 自己创建并初始化 `PreviewCanvas`、皮肤、媒体、音效、导出控制器。
5. 输出进度、结果、日志后退出。

worker 不允许做的事情：

- 读取 snapshot 对应 chart 的磁盘内容作为真实导出源。
- 引用主进程内存对象。
- 依赖主进程的 `PreviewCanvas` 或共享 GL 上下文。

## 快照边界

### 快照必须包含的内容

建议定义一个逻辑上的 `ExportJobSnapshot`：

```json
{
  "schema": "miacode_export_snapshot_v1",
  "job_id": "uuid",
  "created_at_utc": "2026-03-18T06:00:00Z",
  "chart": {
    "text_utf8": "...完整 document_.toText() 结果...",
    "difficulty_id": 5,
    "difficulty_name": "MAS",
    "original_chart_path": "D:/Charts/foo/maidata.txt",
    "project_dir": "D:/Charts/foo"
  },
  "resources": {
    "track_path": "D:/Charts/foo/track.mp3",
    "background_media_path": "D:/Charts/foo/pv.mp4",
    "skin_dir": "D:/Desktop/maimuri/MaiCode/assets/skin"
  },
  "render": {
    "background_brightness_outer": 0.6,
    "background_brightness_inner": 0.6,
    "layout_square_scale": 1.0,
    "smooth_brightness": true,
    "background_scale_mode": "fill",
    "note_flow_speed": 1.0,
    "show_timestamp": true
  },
  "audio": {
    "bgm_volume": 0.6,
    "answer_volume": 0.15,
    "judge_volume": 0.1,
    "slide_volume": 0.05,
    "break_volume": 0.1,
    "break_slide_volume": 0.05,
    "ex_volume": 0.1,
    "touch_volume": 0.1,
    "touchhold_volume": 0.1,
    "firework_volume": 0.1
  },
  "export": {
    "start_seconds": 0.0,
    "duration_seconds": 120.0,
    "output_width": 1024,
    "output_height": 1024,
    "fps": 60,
    "output_path": "D:/Charts/foo/out.mp4"
  }
}
```

### 快照不应该包含的内容

下面这些不建议从主进程直接搬过去：

- `previewCanvas_` 内部图像、atlas、FBO、PBO、OpenGL context。
- `previewStatsNoteMarkers_`。
- `previewMediaController_` 的运行中状态。
- `previewSfxRuntime_` 的运行中状态。
- 任何“已经渲染到一半”的缓存。

原因很简单：这些都是“live 渲染态”，不是“导出输入”。真正稳定的输入应该是：

- 谱面文本。
- 明确的难度。
- 明确的导出参数。
- 明确的资源路径。
- 明确的音频/渲染设置。

## 为什么不把 `noteMarkers` 也作为 snapshot 直接发送

虽然 `noteMarkers` 也可以序列化，但这里更推荐 worker 自己根据谱面文本重新生成，理由如下：

- `noteMarkers` 是派生数据，不是源数据。
- 这样可以把“导出输入”的唯一真源固定为谱面文本和导出设置。
- 可以避免未来 timeline 结构变化时，主进程和 worker 对 `noteMarkers` 版本耦合。
- 可以彻底切断导出对 `refreshTimelineMetadata()` 产物的直接依赖。

因此推荐边界是：

- 主进程发送 `document_.toText()`。
- worker 自己执行解析、平移 `first`、生成 duration、生成 `noteMarkers`。

## 快照生成时机

导出按钮点击后，主进程按这个顺序处理：

1. 读取导出对话框中的参数。
2. 调用 `applyCurrentFieldToDocument()`，把当前编辑框内容同步回 `document_`。
3. 从 `document_` 生成完整文本快照：`document_.toText()`。
4. 同时抓取当前的音频设置、渲染设置、输出设置、路径解析结果。
5. 组装 `ExportJobSnapshot`。
6. 启动 worker，并把 snapshot 发过去。

这里有一个关键约束：

- `applyCurrentFieldToDocument()` 只是同步到内存 document。
- 不触发保存到原 chart 文件。

这样就满足“导出基于最新编辑状态”与“磁盘文件保持未保存”的同时成立。

## Worker 启动方式

推荐新增一个单独模式：

```text
MiaCode.exe --export-video-worker
```

而不是继续让 GUI 导出走现在的 `--export-video --chart <path>` 文件模式。

原因：

- `--export-video` 的输入模型是“磁盘 chart 路径”。
- 这与“chart snapshot 不落盘”的需求直接冲突。
- 如果硬把 `--export-video` 改造成同时支持磁盘/内存两种入口，职责会变混乱。

因此建议把两个模式分开：

- `--export-video`：保留现有脚本化/命令行用途，继续从磁盘 chart 导出。
- `--export-video-worker`：专供 GUI 发内存 snapshot 做后台导出。

## IPC 设计

### 选择

推荐采用：

- 主进程 -> worker：stdin，发送 JSON line 命令。
- worker -> 主进程：stdout，发送 JSON line 事件。
- stderr：仅做诊断日志兜底，不作为主协议。

因为 snapshot 很小，可以直接把 `chart.text_utf8` 放进 JSON 字符串，不必额外做二进制分块或临时文件。

### 主进程发给 worker 的命令

#### 启动导出

```json
{"cmd":"start_export","protocol":1,"snapshot":{...}}
```

#### 取消导出

```json
{"cmd":"cancel","job_id":"uuid"}
```

#### 可选：关闭 worker

```json
{"cmd":"shutdown"}
```

### worker 回给主进程的事件

#### worker 就绪

```json
{"event":"worker_ready","protocol":1}
```

#### 已接受任务

```json
{"event":"accepted","job_id":"uuid"}
```

#### 进度

```json
{"event":"progress","job_id":"uuid","stage":"render","percent":42,"message":"Rendering frames"}
```

#### 普通日志

```json
{"event":"log","job_id":"uuid","level":"info","message":"skin loaded"}
```

#### 成功结束

```json
{"event":"finished","job_id":"uuid","success":true,"output_path":"D:/Charts/foo/out.mp4"}
```

#### 失败结束

```json
{"event":"finished","job_id":"uuid","success":false,"error":"Unable to generate SFX mix track.","details":"..."}
```

### 为什么这里用 JSON line 足够

因为当前 snapshot 只有三类数据：

- 完整 chart 文本。
- 少量标量设置。
- 少量资源路径。

这类数据天然适合一次性 JSON 传输。只要规定：

- 编码统一 UTF-8。
- 每条消息单行。
- chart 文本放在 JSON string 中。

就已经够稳定了。

如果后面真的遇到超长 chart 或要传大块二进制数据，再升级到：

- length-prefixed JSON，
- 或 JSON header + binary payload。

但第一版没有必要。

## 导出时主进程与 worker 的对象边界

### 主进程可继续持有

- `document_`
- `previewCanvas_`
- `previewMediaController_`
- `previewSfxRuntime_`
- 编辑器文本框
- timeline / 播放状态

这些对象继续服务“用户正在编辑的当前会话”。

### worker 自己新建

- worker 内部 `SimaiDocument`
- worker 内部解析得到的 `noteMarkers`
- worker 内部 `PreviewCanvas`
- worker 内部媒体解码/混音上下文
- worker 内部导出控制器

### 两边绝不共享

- 内存对象指针
- Qt widget
- OpenGL context
- atlas / FBO / PBO / readback buffer
- 运行中的音效/媒体 runtime

共享边界只能是：

- stdin/stdout 消息
- 输出文件
- ffmpeg 临时文件
- 资源文件路径

## Worker 内部渲染流程

worker 收到 snapshot 后，建议按下面顺序执行：

1. 解析 `chart.text_utf8` 为 `SimaiDocument`。
2. 切到 `difficulty_id`。
3. 重新运行 timeline 解析，得到本地 `noteMarkers`、duration、cursor/timing 数据。
4. 创建本地 `PreviewCanvas`。
5. 设置 `skin_dir`，等待 worker 自己的皮肤资源就绪。
6. 设置本地 render/audio 参数。
7. 根据 `background_media_path`、`track_path` 初始化导出上下文。
8. 开始导出。

这里最重要的一条是：

- worker 不再从 `sourceCanvas` 拷贝渲染状态。

换句话说，现有 `VideoExportController::exportFullPreview(task, sourceCanvas, ...)` 这种依赖主进程 canvas 的接口，不适合作为最终 worker 边界。最终应当改成“worker 自己准备好 export scene，再调用导出控制器”。

## 对现有代码结构的建议落点

### 主进程侧

建议新增一个“导出任务启动器”层，职责只有：

- 从 UI 收集参数。
- 生成 `ExportJobSnapshot`。
- 管理 `QProcess`。
- 处理 stdout/stderr。

它不应该直接进入 `VideoExportController`。

### worker 侧

建议新增一个“导出会话构建器”层，职责是：

- 从 snapshot 恢复本地状态。
- 创建本地 `PreviewCanvas`。
- 解析 chart 并生成 `noteMarkers`。
- 调用导出控制器。

### 导出控制器侧

建议导出控制器最终消费的是：

- 已经准备好的本地 canvas。
- 已经准备好的本地 `noteMarkers`。
- 已经固定的导出配置。

而不是“主窗口 live preview 的延伸态”。

## 资源路径策略

在“不落盘 snapshot”的前提下，资源路径不再适合靠“临时 chart 文件同目录约定”来推导。推荐策略如下：

### 主路径

snapshot 里直接显式带上：

- `track_path`
- `background_media_path`
- `skin_dir`

### 兼容回退

如果这些字段为空，worker 才允许基于 `project_dir` 做默认推导，例如：

- `track.mp3`
- `bg.mp4`
- `pv.mp4`
- `bg.jpg/png/jpeg`

这样做的好处：

- 和“不落盘 chart snapshot”不冲突。
- 不强迫 worker 去读取磁盘 chart。
- 仍然兼容当前项目目录约定。

## 取消与失败处理

### 取消

建议流程：

1. 主进程发送 `cancel` 命令。
2. worker 在渲染循环、ffmpeg 等等待点检查取消状态。
3. worker 正常回发 `finished success=false error=canceled`。
4. 如果超时无响应，主进程再 `terminate()`，最后才 `kill()`。

### 失败

worker 应把错误分成三层返回：

- `error`：给 UI 展示的主错误。
- `details`：技术细节。
- `log_path`：如有落盘日志，返回路径。

## UI 形态建议

如果目标是“导出期间仍然可以继续编辑和预览”，那主界面层面也需要改边界：

- 不再使用窗口模态的 `QProgressDialog` 作为导出生命周期承载。
- 改成非模态任务面板，或者最少改成非阻塞的导出进度窗口。

否则即便底层已经进程隔离，UI 仍然会因为模态框而阻塞用户操作，目标只完成了一半。

## 推荐实施顺序

### 第一阶段

先把最小闭环跑通：

- 新增 `--export-video-worker`。
- 主进程生成内存 snapshot 并通过 stdin 发给 worker。
- worker 重新解析 chart、重建 canvas、独立导出。
- 先只支持单任务导出。
- 进度先用简单 JSON line 回传。

### 第二阶段

再补体验与稳态：

- 非模态 Export Jobs 面板。
- 更细的阶段性进度。
- 取消、重试、查看日志。
- 父进程退出时的 worker 回收。

### 第三阶段

最后再考虑增强项：

- 多任务队列。
- 导出优先级/CPU 限流。
- 更强的资源快照策略。

## 明确的非目标

本方案当前不解决下面这些问题：

- 导出期间媒体文件本身被用户替换。
- 导出期间皮肤资源文件被用户替换。
- 主进程与 worker 的 CPU/GPU 资源竞争。
- 多个导出任务并发时的整体调度策略。

也就是说，本方案保证的是：

- 谱面内容和导出设置在导出开始瞬间被冻结。

但它不保证：

- 所有磁盘媒体资源也被冻结。

如果后面需要“完全可复现导出”，那就要继续扩展到媒体层 snapshot；那是下一层问题，不应和当前“chart 不落盘快照”混在一起。

## 最终建议

对于当前需求，建议直接按下面的边界实施：

- 新增 `--export-video-worker` 专用模式。
- 主进程导出时调用 `applyCurrentFieldToDocument()`，但不保存文件。
- 使用 `document_.toText()` 作为谱面真源，通过 stdin 传给 worker。
- 音频/渲染/输出参数一起随 snapshot 发送。
- `track_path` / `background_media_path` / `skin_dir` 走显式路径。
- worker 自己重建 `SimaiDocument`、`noteMarkers`、`PreviewCanvas` 和导出上下文。
- 主进程与 worker 之间只共享消息和文件路径，不共享任何 live 对象。

这是最贴合当前仓库结构、同时又真正满足“导出时仍可编辑与预览”的方案。
