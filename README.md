<p align="center">
  <img src="resources/icons/app.png" alt="MiaCode avatar" width="128">
</p>

# MiaCode

[中文](README.md) | [English](README_EN.md)

MiaCode 是一款全功能的跨平台 maimai 谱面创作工具。项目基于 Qt 6 / C++ / QML 打造，性能优异，在低配设备上也能流畅运行。
## 特性

### 原生全平台支持

原生支持 Windows / macOS / Linux 全部主流平台。

### 现代化的类 VSCode 工作台 UI

兼顾美观与实用，多组件宽度自由调节，编辑器与预览区面板可左右交换重排。

精心搭配的深、浅色两种主题，可跟随系统方案自动切换。

中 / 英 / 日三种语言支持，可跟随系统默认自动切换。

### 实时预览

预览画面与代码编辑双向联动，实时更新，键入修改即刻反映在预览画面上，无需处于播放模式，可随时拖拽进度条查看配置。

### 视频导出与封面创作工具

#### 导出

一键导出包含精致片头转场动画的谱面预览视频，片头可自定义字体、背景等样式。

支持多种 PV / BG 缩放显示模式，无需手动剪辑即可一键导出含有全景 PV 的预览视频。

支持自定义导出区间，可以指定起始时间戳，或直接在编辑器内选择谱面段落区间并套用。

#### 封面

一键导出用于发布谱面视频的平台封面。支持自定义字体、背景等样式，也可以叠加谱面帧截图，用于展示配置。

### 谱面校验与检测

完整支持 Simai 语法与所有社区主流扩展语法与音符种类。

支持谱面格式实时校验，可快速跳转到指定行，可用于检查是否存在无法被主流编辑器与游戏模拟器读取的内容。

内置由 MaiMuriDX 驱动的实时谱面无理配置检测工具与对应的预览模式，可快速跳转到指定行，可用于检查撞尾 / 多押 / 内无 / 外无，支持部分自定义参数调节。

### 更多实用小功能：

#### 个性化

- 自定义背景

#### 编辑

- 输入法禁止与全角字符转换
- 自动补全时值
- 谱面格式规范化
- 书签跳转段落
- 快捷编写 Touch 音符

## 构建

### 依赖

- CMake 3.21+
- C++20 编译器
- Qt 6.8+

更详细的打包说明见 [scripts/README.md](scripts/README.md)。

### Windows

使用一键脚本自动安装 Qt、准备依赖、构建并打包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

## 仓库结构

- [src](src)：应用源码、核心模型、预览、音频、导出和工具实现
- [assets](assets)：运行资源、皮肤、音效、背景素材与生成数据
- [resources](resources)：Qt resource collection 与应用图标
- [scripts](scripts)：构建、依赖准备、打包和维护脚本
- [third_party](third_party)：随仓库 vendored 或引用的第三方依赖
- [docs](docs)：架构、调试、导出、时间轴和开源准备文档
- [samples](samples)：示例谱面和验收材料

## 发布

当前 release 包由维护者在本地使用脚本打包。

## 许可证与鸣谢

MiaCode 自有源代码使用 MIT License，见 [LICENSE](LICENSE)。仓库整体、随仓库分发的资源、打包产物和发布包定位为非商业使用；具体边界见 [LICENSE_SCOPE.md](LICENSE_SCOPE.md)。第三方库、字体、音效、图片、FFmpeg、BASS、Qt 以及参考实现可能有各自的许可证或分发限制，请以 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 为准。

感谢 [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) 等项目提供的 simai 解析、预览和工程实现参考。感谢 [gfdfdxc/maimai-transition](https://github.com/gfdfdxc/maimai-transition) 提供片头参考。感谢 [Majdata Net](https://majdata.net/) 提供社区谱面下载，感谢 [MaiViewer](https://www.maiviewer.net/) 提供官方谱面 simai 抄谱参考。

特别感谢 hitomi 老师无偿提供 MiaCode logo 绘制。

感谢内部测试时期给出建议、复现问题和协助调试的朋友们，名单见 [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md)。

## 更新日志

历史更新记录已移至 [CHANGELOG.md](CHANGELOG.md)。

## 社群

QQ 群：1095435375

<p align="center">
  <img src="resources/community/qq-group.png" alt="MiaCode QQ 群二维码" width="360">
</p>
