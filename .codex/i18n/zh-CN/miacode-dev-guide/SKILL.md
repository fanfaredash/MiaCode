<!-- translation-source: .codex/skills/miacode-dev-guide/SKILL.md -->
<!-- translation-source-hash: 8be532c36c6b9ddedb5d0eaa574027b9f49fd66ac17a02a989f0c737484fc89b -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# MiaCode 开发指引

把这份文档当作 MiaCode 仓库的中文记忆层来用。先从用户视角的功能出发，定位到它的主入口，再按需打开对应的引用文档。

## 按任务路由

- 需要定位功能、责任边界或评估影响面时，读 `references/feature-index.md`。
- 需要追踪 parser、timeline、preview、audio、export 之间的联动时，读 `references/cross-chain-linkage.md`。
- 需要确认产品逻辑、交互预期，以及“哪些是硬约束、哪些还能调整”时，读 `references/design-ledger.md`。
- 需要梳理常量、阈值、调参数字和魔法数归属时，读 `references/hardcode-registry.md`。
- 需要查看资源文件、命名规则、构建/打包工具和辅助脚本时，读 `references/assets-and-tools.md`。
- 需要查看全仓通用调试开关、运行时计时日志、预览覆盖参数和导出诊断变量时，读 `references/debug-flags.md`。

## 建议按这个顺序工作

1. 先确定用户要改的是哪个能力，以及它的主入口在哪里。
2. 真正动手前先顺着下游消费者追一遍。MiaCode 里很多行为会在实时链路和导出链路里各有一份实现。
3. 如果一个行为同时存在于实时预览和导出路径里，默认把它当作需要同步审视的一组，除非引用文档明确说明不是。
4. 文档和代码不一致时，以代码为准；如果代码推翻了文档，要在同一个改动里把文档也修掉。

## 核心锚点

- 应用启动与 CLI：`src/app/main.cpp`
- 主窗口编排：`src/app/mainwindow/`
- 文档模型：`src/simai/document/`
- 解析器与校验基础：`src/simai/parser/`
- 时间轴数据与 UI：`src/timeline/`
- 预览视频：`src/preview/video/`
- 预览音频：`src/preview/audio/`
- 工具模块：`src/tools/latency/`、`src/tools/muri/`、`src/tools/video_export/`
- 公共配置头文件：`src/common/`

## 维护规则

- 让 `SKILL.md` 保持精简。仓库特有细节放进 `references/*.md`，这里负责导航。
- 发生重命名、目录迁移或架构拆分后，要同步更新 `references/feature-index.md` 里的文件路径、类名、函数名和归属说明。
- 改了跨模块联动行为，要在同一个改动里更新 `references/cross-chain-linkage.md`。
- 改了产品行为、默认值、交互预期或决策边界，要更新 `references/design-ledger.md`。
- 新增、删除、集中化或重划分常量后，要更新 `references/hardcode-registry.md`。
- 改了文件名、查找顺序、打包依赖、辅助脚本或调试工具后，要更新 `references/assets-and-tools.md`。
- 新增、删除、集中化或重划分调试开关、计时日志或诊断环境变量后，要更新 `references/debug-flags.md`。
- 改动这套 skill 或任一英文引用文档后，要在同一个改动里同步更新中文镜像 `.codex/i18n/zh-CN/miacode-dev-guide/`。
- 更新完中文镜像后，运行 `python .codex/tools/check_translation_sync.py --stamp`，刷新中文文件记录的源文档哈希。
- 引入新的硬编码时，先判断能不能并入已有的 `src/common/*.h` 配置头；如果必须留在本地实现里，也要记录它所在文件、含义、单位和影响面。
- 新增功能路径时，同时记录它的主归属点，以及所有必须联动检查的镜像路径和下游路径。
- 删除功能时，把过期的索引和提示一并删掉，不要留下陈旧面包屑。

## 高风险同步区域

- 实时 SFX 时间线和导出 SFX 时间线必须保持一致。
- 背景媒体解析规则同时存在于预览路径和导出路径中。
- 音轨路径解析分散在多个位置。
- `&first` 和时间偏移会影响 parser 输出、预览定位、导出时间轴和延迟检测。
- 导出存在 snapshot/worker 边界；序列化任务结构一旦变化，两边都要改。

## 引用文档

- `references/feature-index.md`
- `references/cross-chain-linkage.md`
- `references/design-ledger.md`
- `references/hardcode-registry.md`
- `references/assets-and-tools.md`
- `references/debug-flags.md`
