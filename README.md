# MiaCode

[中文](README.md) | [English](README_EN.md)

`MiaCode` 是一个基于 Qt 6 + OpenGL 实现的 simai 编辑器与预览器。

## 功能

- 文本编辑器
- 校验错误列表并支持跳转定位
- 原生时间轴视图
  - 波形背景
  - 缩放等级
  - 播放线与光标线
  - tap、hold、slide、wifi、touch、touch-hold 预览
- 原生 OpenGL 预览
- 本地渲染设置，包含音频与视频

## 构建

依赖：
- CMake 3.21+
- Qt 6.8+，需要 `Core`、`Gui`、`Widgets`、`OpenGLWidgets`
- `Qt6::Multimedia`

打包的详细说明在 [scripts/README.md](scripts/README.md)。

### Windows 构建

推荐直接使用脚本自动安装 Qt、构建并打包：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

如果你已经手动安装好 Qt，也可以使用仓库内的 [CMakePresets.json](CMakePresets.json)：

```powershell
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

### macOS 构建

推荐直接使用脚本自动安装 Qt、构建并打包：

```bash
bash scripts/build-macos.sh
```

如果你已经手动安装好 Qt，也可以显式传入 `QT_ROOT` 调用打包脚本：

```bash
QT_ROOT="$HOME/Qt/6.8.3/macos" bash scripts/package-mac.sh
```

## 仓库结构

- [src](src)：源码
- [assets](assets)：运行资源与生成数据
- [resources](resources)：Qt 资源文件
- [scripts](scripts)：打包脚本
- [third_party](third_party)：第三方依赖

---

## 致谢

感谢 [Minepig/MaiMuriDX](https://github.com/Minepig/MaiMuriDX) 提供的 simai 语法解析与渲染逻辑参考。

## 更新日志

### 0.2.1

- 修复导出链路中 ffmpeg 合成节拍异常，消除周期性重复帧卡顿。
- 修复"timeline 跟随预览"在非播放状态下锁定编辑光标的问题。

### 0.2.0

- **Simai文本编辑器**
  - 支持 Ctrl+F 文本查找与替换
  - 能够调整字体与行距
  - 增加 simai 代码高亮
- **BPM与偏移检测**
  - 完全自动的 BPM 与延迟检测
  - 谱面创作者无需手动测量 BPM 和延迟
- **谱面视频导出**
  - 不仅能够导出完整谱面视频
  - 还支持谱面小片段的导出
  - 便于快速分享你的创作片段
- **语法检测**
  - 现在能够检测谱面语法错误
  - 且能对潜在问题给出警告
  - 避免创作谱面在非 Maj 系平台上无法正常解析
- **功能补完**
  - 补充了各种 note 的判定效果动画
  - 补充了 firework 的渲染效果

### 0.1.1

- UI 美化
- 增加 icon

### 0.1.0

- **Simai 编辑**
  - 完整支持 simai 文本解析与编辑流程。
  - 支持多难度字段管理，包含新增、删除与自动切换。
  - 支持镜像与旋转批量操作。
  - 支持多难度和谱面信息设置。
- **渲染与预览**
  - 预览区升级为播放器式控制，支持时间条与倍速播放。
  - 支持文本区 Timeline 与谱面预览联动播放。
- **其它**
  - 完成中英文界面适配。
