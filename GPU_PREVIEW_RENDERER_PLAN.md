# maicode GPU Preview Renderer Plan

## 目标

把 `PreviewCanvas` 从当前的 `QWidget + QPainter + QImage` CPU 光栅化路径，分阶段迁移到 `QOpenGLWidget + 自定义 2D Sprite Renderer`。

迁移目标不是一次性重写，而是先建立稳定的 GL-backed 画布，再逐步把高频小图渲染从 CPU 变换改成 GPU 批量绘制。

## 当前瓶颈

- `PreviewCanvas` 每帧执行大量 `QPainter` 绘制和 `QImage` 变换。
- 高频小图的缩放、旋转、混合主要发生在 CPU 侧。
- 所有预览绘制都在 UI 主线程完成。
- 视频帧当前仍走 CPU 侧缩放和合成路径。

## 分阶段计划

### Phase 1: GL-backed 过渡层

状态: `completed`

目标:

- 把 `PreviewCanvas` 从 `QWidget` 切到 `QOpenGLWidget`。
- 保留现有 `QPainter` 绘制逻辑，不立刻重写 note 渲染。
- 让预览先运行在 GL framebuffer 上，验证帧稳定性和 Qt/OpenGL 生命周期。

执行项:

- `CMakeLists.txt` 增加 `Qt6::OpenGLWidgets` 依赖。
- `PreviewCanvas` 改为继承 `QOpenGLWidget`。
- 把 `paintEvent()` 迁移为 `paintGL()`，继续用 `QPainter` 输出。
- 把尺寸更新逻辑切到 `resizeGL()`。

验收:

- 工程可重新配置并编译。
- 预览窗口仍正常显示。
- 现有交互接口保持不变。

本轮已完成:

- `CMakeLists.txt` 已接入 `Qt6::OpenGLWidgets`。
- `PreviewCanvas` 已切换为 `QOpenGLWidget`。
- 主绘制入口已从 `paintEvent()` 迁移到 `paintGL()`。
- `resizeGL()` 已接管尺寸变化后的媒体帧适配。

### Phase 2: 渲染入口拆分

状态: `in_progress`

目标:

- 从 `PreviewCanvas` 中拆出更清晰的渲染阶段。
- 为后续 GPU sprite 提交准备独立的“渲染命令收集层”。

执行项:

- 先把 `paintGL()` 中的整段场景绘制整理成可复用的私有渲染入口。
- 区分背景层、轨道层、guide 层、note sprite 层、HUD 层。
- 尽量减少“绘制时顺手做资源变换”的耦合。

验收:

- `PreviewCanvas` 内部出现清晰的分层渲染入口。
- 后续可以在不改 UI 交互接口的前提下替换 note 渲染实现。

本轮已完成:

- 已新增 `renderCanvas(QPainter&)`，把 GL 生命周期与场景绘制主体分离。
- 已把场景绘制拆为背景、playfield backdrop、touch、track、guide、hold、tap、HUD 分层入口。

下一步:

- 把 sprite 候选对象的参数收集从这些分层入口中分离出来。

### Phase 3: Sprite Renderer 骨架

状态: `in_progress`

目标:

- 新增一个专门的 OpenGL 2D 渲染器类，负责纹理和 quad 批量提交。

执行项:

- 新建 `PreviewGLRenderer.h/.cpp`。
- 建立最小能力集:
  - shader 初始化
  - 纹理上传
  - 单批次 quad 提交
  - alpha 混合配置
- 先支持“画一个纹理 quad”，再扩展到批量 sprite。

验收:

- `PreviewCanvas` 可以调用独立 GL renderer 完成最基础的纹理绘制。

本轮已完成:

- 已新增 `PreviewGLRenderer.h/.cpp`。
- 已在 `PreviewCanvas::initializeGL()` 中初始化 renderer。
- 已在 `PreviewCanvas::paintGL()` 开始阶段接入每帧 GL state 准备。
- 已打通最小 textured quad 绘制路径。
- `outline` 已优先尝试走 GL 绘制，失败时自动回退到原有 `QPainter` 路径。
- 背景视频帧已优先尝试走同一条 GL quad 绘制路径，失败时自动回退到原有路径。
- `note guide` 已优先尝试走同一条 GL quad 路径，并由 GL 动态处理旋转。
- `eachline` 已重新接回同一条 GL quad 路径。
- `slide / wifi` 轨道 area 在 GPU 路径上已改为逐箭头动态 sprite 绘制，不再依赖整块缓存图。
- `hold` 条带的最终旋转绘制已接入同一条 GL sprite 通路。
- HUD 已增加当前帧的渲染路径标记，方便观察 GPU/CPU 回退状态。
- `setNoteMarkers()` 已取消启动期的整批预热与轨道预构建，改为按需缓存失效。

下一步:

- 复用这条 quad 通路，逐步接入更多静态贴图或视频纹理。

### Phase 4: 首批 Sprite 化对象

状态: `pending`

目标:

- 优先迁移最能受益的高频小图对象。

执行顺序:

- `tap`
- `slide/wifi` 头星与移动星
- `noteguide`
- `touch / touch_hold`

保留旧路径:

- `hold` 条带
- slide/wifi 轨道裁切区域

验收:

- 上述对象不再依赖每帧 CPU 旋转/缩放后的 `QImage` 输出。
- CPU 侧主要只保留位置、角度、尺寸和透明度计算。

### Phase 5: 视频帧与剩余轨道迁移

状态: `pending`

目标:

- 把视频帧改成纹理更新。
- 再逐步迁移 hold 和轨道绘制。

执行项:

- `mediaFrame_` 改为上传/更新 GL texture。
- 避免每帧 CPU 缩放后再 `drawImage`。
- 评估是否保留部分缓存，或统一改为纹理 atlas / strip texture。

## 技术约束

- 统一角度定义，避免继续在多处临时取反修补。
- 处理好 premultiplied alpha、采样过滤和 atlas 留白，避免边缘串色。
- 视频帧上传频率高，后续需要关注纹理更新成本。
- 阶段迁移期间必须保留现有 `PreviewCanvas` 对外接口，避免影响 `MainWindow` 与预览控制链路。

## 本次起步范围

本轮先完成:

1. 新建本计划文档。
2. 完成 Phase 1。
3. 在 Phase 1 基础上顺手整理渲染入口，为 Phase 2 做准备。
