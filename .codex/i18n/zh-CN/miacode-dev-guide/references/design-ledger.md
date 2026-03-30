<!-- translation-source: .codex/skills/miacode-dev-guide/references/design-ledger.md -->
<!-- translation-source-hash: b5cb0cc1b137b4840761c0da49471bdca35a37af784372a36c05de06aa4b6a9d -->

# 设计账本

这个文件用于区分哪些是必须保持的硬约束，哪些只是当前实现选择、仍可调整。

## 1. 事实来源规则

- 代码才是事实来源。
- `DEVELOPMENT_PLAN.md`、`MURI_INTEGRATION_PLAN.md` 和其他笔记只是指导材料与记忆辅助。
- 当文档与代码不一致时，先信任代码，再回写文档。

## 2. 必须保持的约束

- `MainWindow` 是编排层，不应长期承载所有功能实现。
  - 新的窗口功能通常应放在 `src/app/mainwindow/sections/<feature>/`。
- `SimaiDocument` 是元数据与难度文本的可编辑存储模型。
- parser 输出是这些模块共享的中间表示：
  - timeline
  - preview
  - Muri analysis
  - export reconstruction
- 运行时 SFX 与导出 SFX 必须使用同一套“物件到音效”的语义。
- `&first` 以原始文档数据形式存储；时间语义通过 getter 与 marker 偏移应用，而不是在代码各处散落临时反转逻辑。
- 导出通过 snapshot/worker 边界完成，而不是让 worker 直接修改 UI 状态。
- 资源查找是基于文件和约定驱动的，不是基于数据库驱动的。

## 3. 当前默认值，可调整

- Preview canvas 默认使用正方形比例，除非导出 UI 临时改写。
- Preview note flow speed 默认取自 `PreviewGameplayConfig.h`。
- Native“谱面确认”预览当前会在旧的六边形判定 effect 之上额外叠一层 maimuri 风格判定 overlay。`MuriRenderOptions::showChartReviewSlideJudgeOverlay` 控制 slide/wifi 类 overlay，默认开启；`MuriRenderOptions::showChartReviewSimpleJudgeOverlay` 控制 tap/hold/break 文本 overlay 与 slide 头部文本，默认关闭，直到有 UI 开关接入。
- 在 `RenderMode::MaimuriDxStyle` 下，wifi 轨道擦除当前跟随运行时三轨进度，而不是静态 area checkpoint：共享轨道按三轨里最慢的一轨裁切；当进度数组不可用时回退到已判定 area；运行时完成后保持擦除状态，不再回放整条轨道的 full-track flash。若 `MuriRenderOptions::wifiNeedC` 开启，最后一个 area 会一直保留到 `C` 真正抬起。
- 自动导出编码器选择当前优先采用更保守的 H.264 风格路径，再逐级回退。
- 部分导出当前保持完整导出的 lead-in 行为不变，但当请求不是 full-range 时，会额外插入 `1.5s` 预加载，并预先按 `marker.second` 是否落在 `[L, R]` 内来过滤导出物件；slide 与其轨迹仍以头部时间戳耦合，因为整个 `TimelineNoteMarker` 作为一个整体保留或丢弃。
- 导出输出命名当前会在缺少后缀时自动补上 `.mp4`，并在重名时依次选择 `name(1).mp4`、`name(2).mp4` 等名称后再启动 worker。
- 背景媒体命名当前限制为 `bg.*` 或 `pv.mp4` 一类约定。
- 预览面板当前采用卡片式布局，分成 preview、controls、stats 三块。
- Preview 全屏当前复用与嵌入式预览相同的 transport controls，额外显示一个圆角半透明黑色 `Esc` 提示气泡，并且仅当光标移动到底部热区时才显示接近全宽的底部 overlay 控制条；控制条会先执行淡入/淡出透明度动画，再自动隐藏。
- 当图表文本编辑器拥有焦点时，`Ctrl+Enter` 当前会转发到现有的 preview 播放/暂停控制，而不是留给文本控件自身处理。
- Timeline 拖拽/拖动当前修改的是运行时 `R`，不是编辑器锚点 `C`：拖拽或滚轮平移在需要时会先把 `R` 重新绑定到视口中线，头部点击与 `Ctrl+点 Timeline` 则按点击时刻执行规格里的 `R -> C` 动作，编辑器侧的光标变更只会改 `C`。
- Timeline 缩放当前提供 `25%..150%`、步进为 `5%` 的预设，默认起始于 `50%`，头部缩放按钮按 `25/50/75/100/125/150` 这些粗档位循环。
- Follow 当前只在播放中生效：开启后会把 `C` 绑定到 `R` 之前最近的逗号，而暂停态切换 Follow 不会自动改动 `L / R / C`。
- Preview 播放当前会把预览视频 / SFX / 物件统计冻结在“点击播放”时的快照上，直到播放停止；播放期间的实时文本修改仍会重绘 Timeline 并刷新 validation / Muri 输入，但不会回写已经在播放中的内容。
- Validation UI 当前重点强调底部标签页中的问题列表，以及编辑器头部的 summary chips。

这些默认值都可以调整，但如果改了，需要同步更新这里以及相关索引/联动文档。

## 4. 明确保留弹性的区域

- UI 微调细节，例如间距、卡片比例、控件排列
- 视觉效果调参，只要不破坏相关预览/导出联动假设
- 仅用于开发的辅助可执行文件打包行为
- 校验与诊断 UI 的具体文案和严重级展示方式
- 导出启发式策略，例如码率、编码器优先级和进度呈现方式，只要 worker 契约不变

## 5. 需要加护栏的区域

- 路径解析规则仍在少数位置重复实现，尤其是媒体与音轨文件
- 大量视觉调参仍保留在 `PreviewCanvas.cpp` 的本地常量中
- 延迟检测带有一组意义明确但仍较局部的算法常量
- `DEVELOPMENT_PLAN.md` 很有价值，但其中部分路径与结构描述已经偏离当前树结构

即使这些区域仍可调整，也应按高敏感区域对待。

## 6. 明确开放或仍有风险的区域

- 是否要继续进一步集中化那些重复的路径解析逻辑
- 是否要把更多 preview/export 配置从本地实现常量搬到共享配置头
- 某些当前 chart 目录文件命名约定是否应变为可配置
- 仓库是否应新增脚本来自动刷新代码索引与硬编码注册表，而不是完全依赖手工维护

## 7. 决策记录规则

当一个原本灵活的区域变成硬约束时，把它加入第 2 节。

当一个硬约束被有意放宽时，不要在旧规则上叠加互相矛盾的新表述，而是直接删除或改写旧规则。

当某个设计选择仍在讨论中，但当前代码已经依赖它时，把它记为“当前默认值”或“开放/有风险区域”，不要写成永久规则。
