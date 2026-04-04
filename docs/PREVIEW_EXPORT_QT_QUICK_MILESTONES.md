# 预览/导出 Qt Quick 重构里程碑

## 1. 目标与边界

本文件用于把“预览与导出迁到 Qt Quick 路线”整理成可执行的 milestone，并明确哪些东西可以直接替换、哪些必须先拆掉强 GL 绑定、哪些需要补新基础设施。

本重构的硬约束不是“看起来差不多”，而是下面这些语义必须保持不变：

- 预览运行时语义：见 `docs/PREVIEW_RUNTIME_WORKFLOW.md`
- 当前 GL 路径与离屏导出事实：见 `docs/PREVIEW_GL_RENDER_PATHS.md`
- 导出 worker / snapshot 边界：见 `docs/VIDEO_EXPORT_SUBPROCESS_ISOLATION.md`
- parser / preview / export / Muri 联动：见 `.codex/skills/miacode-dev-guide/references/cross-chain-linkage.md`

非目标：

- 不改变 preview snapshot、play-start snapshot、paused preview 的语义
- 不改变 MainWindow 与 timeline / validation / Muri / latency detector 的联动语义
- 不借机把媒体播放、SFX、Muri、export worker 边界重写成另一套产品语义

推荐路线：

- 运行时预览：`QQuickView` / `QQuickWindow` + `QWidget::createWindowContainer(...)`
- 导出离屏：`QQuickRenderControl`
- 主窗口仍保持 Widgets 外壳
- 第一阶段避免引入 `Qt Quick Controls`

这样能最小化主界面改动，同时把“窗口承载替换”和“离屏导出替换”拆成两个风险面。

## 2. 当前实现锚点

当前链路中需要被替换或保留的核心锚点如下：

- 预览承载：`src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - 当前通过 `QWidget::createWindowContainer(previewCanvas_, ...)` 挂载 `PreviewCanvas`
- 运行时预览核心：`src/preview/video/PreviewCanvas.*`
  - 当前是 `QOpenGLWindow`
  - 当前同时承担运行时渲染、导出 CPU 路径、导出离屏 GL 路径
- 低层渲染帮助：`src/preview/video/PreviewGLRenderer.*`
  - 当前承载纹理上传、YUV 视频上传、shader、batch draw
- 媒体控制：`src/preview/video/PreviewMediaController.*`
  - 当前固定运行在专用 `QThread`
  - 通过 `QVideoSink` / `QMediaPlayer` / `QAudioOutput` 产出媒体帧与媒体时钟
- 播放协调：`src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - 当前是 preview 语义真正的 owner
- 导出 worker 边界：`src/tools/video_export/VideoExportSnapshot.*`、`src/app/main.cpp`
  - `--export-video-worker` 已存在
  - worker 已经基于 snapshot 中的 chart text 重建 export task
- 导出渲染：`src/tools/video_export/VideoExportController.cpp`
  - 当前直接消费 `PreviewCanvas`
  - 当前会 `copyRenderStateFrom(*sourceCanvas)` 并尝试共享 GL context

## 3. 哪些可以用相似 API 直接替换

下面这些接口或概念可以保留“同名或近似同名 façade”，先不把 MainWindow 改得面目全非：

| 当前入口                                                                | Qt Quick 路线建议                                                         | 风险  |
| ------------------------------------------------------------------- | --------------------------------------------------------------------- | --- |
| `PreviewCanvas::setPlayheadSeconds`                                 | 保留成 `PreviewSceneController::setPlayheadSeconds`，驱动 Quick scene state | 低   |
| `setNoteMarkers` / `setMuriAnalysisReport` / `setMuriRenderOptions` | 保留同类 setter，底层从 `PreviewCanvas` 改成 Quick scene state                  | 低   |
| 背景亮度、布局缩放、flow speed、HUD 开关 setter                                  | 保持现有参数面，不让 MainWindow 重学一套 API                                        | 低   |
| `QOpenGLWindow::frameSwapped` 节拍                                    | 替换为 `QQuickWindow::frameSwapped`                                      | 低   |
| `QWidget::createWindowContainer(previewCanvas_)`                    | 替换为 `QWidget::createWindowContainer(qquickView)`                      | 低   |
| `VideoExportSnapshot` / `buildVideoExportTaskFromSnapshot`          | 继续保持为 export 真边界                                                      | 低   |
| `PreviewMediaController` 的线程归属与播放控制入口                               | 先保持不变，只替换它的帧消费端                                                       | 中   |

建议保留一个过渡层，例如：

- `PreviewSceneController`
- 或保留 `PreviewCanvas` 这个类名，但内部不再是 `QOpenGLWindow`，而是持有 `QQuickView` / scene state

这样可以把 MainWindow、导出对话框、validation/Muri 回灌逻辑的改动压到最小。

## 4. 哪些必须先脱离强 GL 后端绑定

下面这些点不能只做“API 对应替换”，必须先拆耦：

| 当前强绑定点                                                    | 当前文件                                                            | 必要动作                                                        |
| --------------------------------------------------------- | --------------------------------------------------------------- | ----------------------------------------------------------- |
| `QOpenGLWindow` 作为预览宿主                                    | `PreviewCanvas.h/.Runtime.cpp`                                  | 改成 `QQuickView` / `QQuickWindow` 持有关系                       |
| `PreviewGLRenderer`                                       | `PreviewGLRenderer.*`                                           | 彻底退出主渲染路径，改由 scene graph node / texture provider 承担         |
| `QPainter + beginNativePainting()` 混合渲染                   | `PreviewCanvas.Render.cpp`、`PreviewCanvas.Objects.cpp`          | 改成 scene graph 层次；不要再混合 CPU painter + native GL             |
| `QOpenGLContext` / `QOpenGLDebugLogger` 生命周期              | `PreviewCanvas.GLAndTransforms.cpp`、`PreviewCanvas.Runtime.cpp` | 改成 Qt Quick / RHI 级别的诊断信息                                   |
| `QOpenGLFramebufferObject` / `QOffscreenSurface` / PBO 读回 | `PreviewCanvas.Runtime.cpp`                                     | 改成 `QQuickRenderControl` 离屏渲染                               |
| `copyRenderStateFrom(*sourceCanvas)`                      | `PreviewCanvas.Runtime.cpp`、`VideoExportController.cpp`         | 改成“导出场景按 task 重建”，不再拷贝 live canvas                          |
| `sourceCanvas->context()` 共享上下文                           | `VideoExportController.cpp`                                     | 删除；worker 不再依赖主窗口 GL context                                |
| `glReadPixels` / FBO readback fallback 链                  | `PreviewCanvas.Runtime.cpp`、`PreviewGLRenderer.cpp`             | 改成 Quick 离屏 render target readback；保留 ffmpeg 管线，不保留 GL 回读实现 |

一句话概括：Qt Quick 路线不是“把 GL helper 挪个地方”，而是把渲染后端所有权从手写 GL 切到 scene graph / RHI。

## 5. 需要额外补充的基础设施

下面这些不是“替换现有类”就会自动出现，必须补：

1. 后端无关的 scene state 层
   
   - 把 `noteMarkers`、Muri 分析、布局参数、HUD 开关、stage media 状态，从 `PreviewCanvas` 内部状态拆成独立 payload
   - 目标是让 MainWindow 面对“状态模型”，不是“某种具体渲染窗口”

2. Quick scene graph 物件层
   
   - stage background
   - outline / playfield backdrop
   - guide / track / slide motion / wifi / tap / hold / touch / judge effect / HUD
   - 必须能严格复现当前 `renderCanvas` 的层顺序

3. 媒体帧桥接层
   
   - 第一阶段不建议直接把 `PreviewMediaController` 改成 QML `VideoOutput` owner
   - 更稳妥的做法是继续让 `PreviewMediaController` 持有 `QMediaPlayer/QVideoSink`
   - Quick scene 侧补一个能消费 `QImage/QVideoFrame` 的 item / texture bridge
   - 这样能保住当前“专用线程 + 主窗口代理 + playhead 强同步”的语义

4. 导出专用 Quick 离屏会话
   
   - 运行时 preview 可用 `QQuickView`
   - export worker 必须独立使用 `QQuickRenderControl`
   - 不能把 on-screen view 直接拿来做 worker 导出

5. 一致性验证基线
   
   - 预览 golden frame 对比
   - 导出帧对比
   - timeline / Muri / object stats / timestamp / partial export 样例集
   - 可以复用现有 `present_compare` / export compare 诊断思路，但需要迁移到 Quick 路线下

6. 新的部署脚本规则
   
   - 当前 `scripts/package-win.ps1` 只做 `windeployqt`，没有 `--qmldir`
   - Qt Quick 路线必须补 QML import 扫描或显式拷贝 QML 模块

## 6. 难以确保行为完全一致的点

下面这些是文档里必须提前标红的高风险点：

1. 图层顺序与混合
   
   - 当前 `renderCanvas` 层顺序是产品语义的一部分
   - Qt Quick 默认的 item 树与材质混合顺序不能“看起来差不多就算了”

2. 视频背景的裁切、缩放和 fallback 语义
   
   - 当前存在 direct video upload、`QVideoFrame::toImage()` fallback、retained fallback image
   - Quick 路线若直接换成另一套视频 item，很容易改变黑帧、首帧、seek 后的可见行为

3. render loop 与节拍
   
   - 当前 preview 可用 `frameSwapped` 参与节拍
   - Qt Quick 在不同平台/配置下可能采用不同 render loop
   - 如果 scene graph 的提交时序改变，可能影响“起播同拍”“seek 后立刻到位”的观感

4. play-start snapshot 冻结语义
   
   - 当前播放期间，preview 内容冻结在 play-start snapshot
   - Quick scene 不能偷偷订阅最新编辑态或使用 QML animation 自己推进逻辑时间

5. 导出与预览的一致性
   
   - 当前 export worker 已经用 snapshot 重建 task
   - 但渲染后端切到 Quick 后，导出离屏路径和运行时 scene graph 必须继续消费同一套 scene state

6. 采样与滤波
   
   - 当前 atlas、纹理 cache、mipmap、平滑缩放是手写控制
   - Quick scene graph 的纹理采样差异可能让 note 边缘、outline、judge effect 出现轻微漂移

7. touchhold / paused preview 语义
   
   - paused preview 必须保持 touchhold 静音
   - 这里虽然主要是音频语义，但 Quick scene 如果自行做视觉状态推进，也可能出现“画面像在播、声音没播”的错配

8. 部分导出与 preload
   
   - 当前 partial export 过滤 marker、1.5 秒 preload、Muri overlay、SFX timeline 是联动设计
   - Quick export 路线不能只对画面做迁移，必须继续复用同一套 task 输入

## 7. 推荐里程碑

### Milestone 0: 语义冻结与对比基线

目标：

- 把现有 preview/export 语义当成迁移合同固定下来
- 选 5 到 10 份代表性谱面做 golden baseline

内容：

- 从 `PREVIEW_RUNTIME_WORKFLOW.md`、`PREVIEW_GL_RENDER_PATHS.md`、`VIDEO_EXPORT_SUBPROCESS_ISOLATION.md` 抽出迁移核对清单
- 为 preview / export 保存对比帧、对比视频、关键日志
- 明确“不允许回归”的行为项

完成标志：

- 有一份固定样例集
- 有 frame diff / export diff 的验收脚本或手工流程

### Milestone 1: 抽出后端无关的 Preview Scene State

目标：

- 不换渲染后端，先把 MainWindow 到 Preview 的状态接口固化

内容：

- 从 `PreviewCanvas` 内部状态中抽出 scene state / controller
- 让 MainWindow 继续调近似原 API
- 导出 task 也开始消费同一套后端无关状态

完成标志：

- MainWindow 不再直接依赖 `QOpenGLWindow` 特性，除了宿主创建那一层
- `PreviewCanvas` 内部开始变成“渲染实现”，而不是“状态 owner”

### Milestone 2: 运行时预览切到 Qt Quick

目标：

- on-screen preview 先迁走
- 不同时改 export 离屏

内容：

- 用 `QQuickView/QQuickWindow` 取代当前 `PreviewCanvas` 窗口承载
- 继续保留 `QWidget::createWindowContainer(...)`
- 保留 `PreviewMediaController`、`QtPreviewSfxRuntime`、`MainWindow` 的语义 owner 地位
- Quick scene 先严格对齐当前 render layer 顺序

完成标志：

- 预览播放、暂停、seek、resume、follow preview、Muri overlay、HUD 行为与旧版一致
- `frameSwapped` 节拍仍可用，或有等价替代且验收通过

### Milestone 3: 导出离屏切到 Qt Quick

目标：

- 用 worker 内部的 Quick scene 完成导出，不再依赖 live canvas

内容：

- `VideoExportController` 从“消费 `PreviewCanvas`”改为“消费 Quick export scene/session”
- worker 使用 `QQuickRenderControl`
- 移除 `copyRenderStateFrom(*sourceCanvas)`、共享 context、FBO/PBO 读回链
- ffmpeg 编码管线、snapshot、task 重建、SFX timeline、Muri 分析保持不变

完成标志：

- `exportPreparedTask(task, nullptr, ...)` 仍成立
- worker 导出结果与运行时 preview 保持对齐

### Milestone 4: 迁移诊断、部署与文档

目标：

- 让 Quick 路线可维护、可打包、可诊断

内容：

- 新增 Quick/RHI 后端日志
- 更新 `scripts/package-win.ps1`，补 `--qmldir` 或显式 QML 模块拷贝
- 更新 build 依赖和环境文档
- 补充 Quick 路线下的黑屏、backend fallback、离屏失败诊断说明

完成标志：

- Release 包可独立运行
- 文档能说明“为什么是这个 backend”和“出问题看哪里”

### Milestone 5: 删除旧 GL 路径

目标：

- 只在 Quick 路线稳定后，才删除旧 GL 专用实现

内容：

- 删除 `PreviewGLRenderer`
- 删除离屏 GL/FBO/PBO 代码与相关 env flag
- 清理 `Qt6::OpenGLWidgets` / 旧 GL 文档

完成标志：

- 代码库里不存在“双后端长期并行维护”的核心债务

## 8. 环境需求

### 开发构建环境

当前仓库本地构建环境是：

- Qt `6.8.3` `msvc2022_64`
- CMake `3.21+`
- MSVC 2022 Build Tools

Qt Quick 路线至少需要额外保证：

- Qt 安装中包含 `qtdeclarative`
- CMake `find_package(Qt6 ...)` 增加 `Quick`、`Qml`
- 若最终选 `QQuickWidget`，还要加 `QuickWidgets`

推荐约束：

- 第一阶段不依赖 `Qt Quick Controls`
- QML 尽量只用 `QtQuick`、`QtQuick.Window`、`QtQml.Models`
- 不把播放时钟交给 QML animation / Timer

### 运行环境

建议继续按 Windows x64 发行，并保留当前的图形后端兜底思路。

预估要求：

- 正常机器：优先走 Qt Quick 默认图形后端
- 兜底机器：继续保留软件/兼容 fallback 发行物
- export worker 仍然独立于主 UI 进程运行

## 9. Windows 分发包大小预估

### 当前包体基线

基于仓库现有 `dist/MiaCode-v0.3.8-dev17-win64`：

- 解压后目录总大小：约 `204.03 MB`
- zip 包大小：约 `85.94 MB`

### 推荐路线的新增项

按“`QQuickView/QQuickWindow` + `QQuickRenderControl` + 不引入 Quick Controls”估算，至少会新增这些 DLL：

| 文件                 | 大小        |
| ------------------ | ---------:|
| `Qt6Qml.dll`       | `5.01 MB` |
| `Qt6QmlModels.dll` | `0.71 MB` |
| `Qt6QmlMeta.dll`   | `0.14 MB` |
| `Qt6Quick.dll`     | `5.99 MB` |

合计新增核心 DLL：约 `11.85 MB`

同时还需要额外部署最小 QML import 模块，按本机 Qt 目录粗略估算：

| 路径                   | 大小        |
| -------------------- | ---------:|
| `qml/QtQuick` 直接文件   | `0.63 MB` |
| `qml/QtQuick/Window` | `0.12 MB` |
| `qml/QtQml` 直接文件     | `0.12 MB` |
| `qml/QtQml/Models`   | `0.16 MB` |
| `qml/QML`            | `0.13 MB` |

最小 QML 模块增量：约 `1.16 MB`

### 可能减少的项

如果 GL 路线彻底删除，并且最终链接与部署不再需要 Qt OpenGL 模块，则有机会少：

| 文件              | 大小        | 说明                             |
| --------------- | ---------:| ------------------------------ |
| `Qt6OpenGL.dll` | `1.88 MB` | 仅在完全脱离当前 OpenGL helper 后才有机会去掉 |

注意：

- 当前 release 包里本来就没有 `Qt6OpenGLWidgets.dll`，所以它不是“运行时可见的减项”
- `dxcompiler.dll`、`dxil.dll`、`D3Dcompiler_47.dll`、`opengl32sw.dll` 预计仍需保留，不应先假设能减

### 净增量估算

按推荐路线粗估：

- 粗增量：`11.85 + 1.16 = 13.01 MB`
- 若 `Qt6OpenGL.dll` 最终可以移除：净增量约 `11.13 MB`

因此包体大致会从：

- 解压目录 `204 MB` -> `215 MB` 到 `217 MB`
- zip `85.94 MB` -> 约 `91 MB` 到 `92 MB`

### 明确不推荐的包体放大项

如果首版就引入 `Qt Quick Controls`，包体会明显再涨一截：

- `qml/QtQuick/Controls`：约 `13.22 MB`
- `qml/QtQuick/NativeStyle`：约 `3.14 MB`
- 以及多组 `Qt6QuickControls2*.dll`

因此首版重构不建议使用 Quick Controls 来承载 preview/export 主视图。

## 10. 结论

最稳妥的落地顺序不是“直接把 `PreviewCanvas` 换成 Qt Quick”，而是：

1. 先冻结语义与验收基线。
2. 先抽出后端无关的 scene state。
3. 先迁 on-screen preview。
4. 再迁 export worker 的离屏渲染。
5. 最后删除旧 GL 路径。

只要遵守这个顺序，Qt Quick 路线可以在不改变 preview/export 语义的前提下推进；如果跳过 scene state 抽象、媒体桥接层、和离屏专用 Quick export session，这次重构很容易变成“画面换了，语义漂了”。
