<!-- translation-source: .codex/skills/miacode-dev-guide/references/design-ledger.md -->
<!-- translation-source-hash: 67bb235bcb35f0bf4dbc2e885149e32b47a4320b3886dda9cd2bbdef97b27d12 -->
<!-- translation-source-hash: e2ccfbcebf0f595668025fa8a9af3b90a1a7cdddf5a5ca5a0143933e8697b728 -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 设计账本

这份文件用于区分哪些是必须保持的契约，哪些只是当前实现选择、仍可调整。

## 1. 真正的事实来源

- 代码才是事实来源。
- `DEVELOPMENT_PLAN.md`、`MURI_INTEGRATION_PLAN.md` 和其他笔记，属于指导材料和记忆辅助。
- 文档与代码冲突时，优先信代码，再回写文档。

## 2. 必须保持的契约

- `MainWindow` 是编排层，不应成为所有功能实现的长期堆放点。
  - 新窗口功能通常应落在 `src/app/mainwindow/sections/<feature>/`。
- `SimaiDocument` 是元数据和难度文本的可编辑存储模型。
- parser 输出是这些模块共享的中间表示：
  - timeline
  - preview
  - Muri analysis
  - export reconstruction
- 实时 SFX 与导出 SFX 必须采用同一套“物件到音效”的语义。
- `&first` 以原始文档数据形式存储；时间语义通过 getter 与 marker 平移实现，而不是在各处散落取反逻辑。
- 导出通过 snapshot/worker 边界执行，而不是让 worker 直接篡改 UI 状态。
- 资源查找采用文件约定驱动，而不是数据库驱动。

## 3. 当前默认值，但可以调整

- 预览画布默认是正方形比例，除非导出 UI 临时改了它。
- 预览 note 流速默认值来自 `PreviewGameplayConfig.h`。
- Native“谱面确认”预览当前会在旧六边形判定 effect 上方额外叠一层 maimuri 风格判定 overlay，覆盖 tap、hold、slide 和 wifi 的正常时机；它由 `MuriRenderOptions::showChartReviewJudgeOverlay` 控制，且在加入 UI 开关前默认开启。
- 在 `RenderMode::MaimuriDxStyle` 下，wifi 轨道擦除当前跟随运行时三轨进度而不是静态 area checkpoint：共享轨道按三轨里最慢的一轨裁切，在进度数组缺失时回退到已判定 area，并且在运行时完成擦除后不再回放整条轨道的 full-track flash；如果 `MuriRenderOptions::wifiNeedC` 开启，最后一个 area 会一直保留到 `C` 真正抬起。
- 自动导出编码器选择当前更偏向保守的 H.264 路径，再逐级 fallback。
- 背景媒体命名当前限制在 `bg.*` 或 `pv.mp4` 一类约定。
- 预览面板当前采用卡片式布局，分成 preview、controls、stats 三块。
- 时间轴拖拽/拖动当前默认让播放头尽量保持在视口中线附近，并在两端补动态留白，保证中线仍能对齐到 0 秒和当前预览末尾。
- 校验 UI 当前会在底部标签页展示问题，并在编辑器头部用 summary chips 汇总。

这些都还能调整，但改完后要同步更新这里以及相关索引/联动文档。

## 4. 明确留有弹性的区域

- UI 微调细节，如间距、卡片比例、控件排列
- 视觉效果调参，只要不破坏相关的预览/导出联动假设
- 开发态辅助可执行文件的打包策略
- 校验与诊断 UI 的具体文案和严重级呈现方式
- 导出启发式策略，如码率、编码器优先级和进度表现，只要 worker 契约不破

## 5. 需要加护栏的区域

- 一些路径解析规则仍然有重复实现，尤其是媒体和音轨文件。
- 大量视觉调参仍留在 `PreviewCanvas.cpp` 的本地常量里。
- 延迟检测有一套很重要但仍较本地化的算法参数。
- `DEVELOPMENT_PLAN.md` 很有用，但其中部分路径与结构描述已经开始漂移。

这些区域即使仍可调整，也应视为高敏感区域。

## 6. 明确仍开放或有风险的点

- 是否要继续集中化那些重复的路径解析逻辑
- 是否要把更多 preview/export 配置从本地 `.cpp` 常量搬到共享配置头
- 当前 chart-directory 文件命名约定是否应该变成可配置
- 仓库是否应该增加自动刷新代码索引和硬编码登记的脚本，而不是完全手工维护

## 7. 记录决策的规则

某个原本灵活的区域一旦升级为硬契约，就把它写进第 2 节。

某个硬契约如果被有意放宽，不要在旧规则上叠加相互矛盾的新描述，而是直接改写或移除旧规则。

某个设计点如果仍在讨论中，但当前代码已经依赖它，就把它写成“当前默认”或“开放/有风险”，不要写成永久规则。
