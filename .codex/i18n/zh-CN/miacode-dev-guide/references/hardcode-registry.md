<!-- translation-source: .codex/skills/miacode-dev-guide/references/hardcode-registry.md -->
<!-- translation-source-hash: 2bf7cdc84f3242d19608eaecf1918ef42c77784f6661390ff3927c106b3f5561 -->
<!-- 说明：这是中文镜像，不作为 Codex skill 入口加载。 -->

# 硬编码登记

这份文件用于记录重要常量在哪里、它们代表什么、以及它们应该继续留在本地还是提升到共享配置头。

## 1. 共享配置头

- `src/common/AssetPaths.h`
  - 负责：资源根目录发现与 `assetPath(...)`
  - 作用域：共享资源查找
  - 规则：新的共享路径规则优先并入这里，而不是再次复制
- `src/common/PreviewGameplayConfig.h`
  - 负责：逻辑画布大小、车道距离几何、预览流速归一化、tap 生命周期时序、slide 预显现时序、judge effect 时长
  - 作用域：预览与导出的共同时间假设
- `src/common/PreviewVideoGeometryConfig.h`
  - 负责：背景亮度默认值、layout square 缩放（当前默认 `0.95`）、压暗几何、平滑亮度混合
  - 作用域：预览与导出的共同视觉几何
- `src/common/PreviewSfxAssets.h`
  - 负责：SFX kind 到文件名的映射，以及 SFX 目录查找
  - 作用域：音效资源约定
- `src/common/VideoExportConfig.h`
  - 负责：导出 lead-in 常量
  - 作用域：导出时间轴对齐
- `src/tools/muri/MuriStaticChecker.h`
  - 负责：静态 tap-on-slide 阈值的最小/最大/默认值
  - 作用域：静态 Muri 碰撞解释

## 2. 当前实现里的热点区域

- `src/preview/video/PreviewCanvas.cpp`
  - 负责：大量渲染调参常量
  - 例子：
    - lane 角度基准与步长
    - sprite 缩放比例
    - touch/touchhold 收拢曲线参数
    - judge effect 曲线时序与几何
    - firework 视觉调参
    - cache 限制与 atlas 打包参数
  - 规则：如果这些值只服务于本地渲染内部，可留本地；一旦外部依赖，就应提升
- `src/tools/latency/LatencyDetectorDialog.cpp`
  - 负责：检测窗口大小、hop size、BPM 扫描范围、offset 惩罚、snap 阈值
  - 规则：如果这些值只属于延迟检测工具，可保留本地；但涉及用户感知范围变化时要登记
- `src/simai/parser/SimaiNativeParser.cpp`
  - 负责：用于派生 marker 行为的 parser 默认几何与时序假设
  - 规则：parser 级常量变化可能是全仓级影响，要按跨链路改动处理
- `src/tools/video_export/VideoExportController.cpp`
  - 负责：混音采样率、编码器 probe 超时、码率启发式、逐帧诊断阈值、ffmpeg fallback 行为
  - 规则：导出启发式可以先留本地，但凡影响输出兼容性或打包假设的变化都要记录
- `src/tools/video_export/VideoExportDialog.cpp`
  - 负责：导出对话框 UI 尺寸与预览控制常量
  - 规则：纯本地 UI 常量通常留本地，除非开始跨对话框复用

## 3. 提升为共享常量的规则

当出现以下任一情况时，应考虑把常量从 `.cpp` 提升出去：

- 超过一个子系统依赖它
- 预览和导出都必须与它保持一致
- 测试、脚本或文档需要按名字引用它
- 设计者或维护者预计会长期主动调它

## 4. 可以继续留本地的规则

以下情况保留在本地是合理的：

- 它只是某一段渲染或某个控件的实现细节
- 搬出去并不能减少重复
- 暴露出去反而会让归属更模糊

## 5. 这些变化要立刻登记

- 常量在文件间迁移
- 名字不变但单位或语义变化
- 把魔法数正式命名为常量
- 删除了此前被外部文档或工具引用过的常量

## 6. 当前高关注区域

- `PreviewCanvas.cpp` 里的预览特效调参
- 延迟检测扫描参数
- 导出编码器与码率启发式
- parser 的几何/时序假设
- 所有在 `src/common/` 之外重复出现的文件名或资源查找字面量
