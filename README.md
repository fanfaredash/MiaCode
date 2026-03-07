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

打包说明已移动到 [scripts/README.md](scripts/README.md)。

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
  - 优化视频与音频预览链路，明显改善卡顿问题。
  - 时间轴与预览联动增强，拖动与定位行为更稳定。
- **其它**
  - 完成中英文界面适配。
