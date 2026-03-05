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
- Qt 6.1+，需要 `Core`、`Gui`、`Widgets`、`OpenGLWidgets`
- 可选 `Qt6::Multimedia`
- 可选 QScintilla for Qt 6

使用仓库内的 [CMakePresets.json](CMakePresets.json) 配置并构建：

```powershell
cmake --preset vs2022-qt6
cmake --build --preset release
```

运行：

```powershell
.\build\Release\MiaCode.exe
```

## 仓库结构

- [src](src)：源码
- [assets](assets)：运行资源与生成数据
- [resources](resources)：Qt 资源文件
- [scripts](scripts)：打包脚本
- [third_party](third_party)：第三方依赖

## Windows 打包

如果系统 `PATH` 里找不到 `windeployqt`，需要手动指定 `-QtRoot`：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot D:\Qt\6.8.3\msvc2022_64
```

如果 `Qt\bin` 已在 `PATH` 中，也可以直接运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1
```

输出：
- `dist/MiaCode-v<version>-portable-win64`
- `dist/MiaCode-v<version>-portable-win64.zip`

---

## 更新日志

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
