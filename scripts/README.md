# Scripts

[中文](README.md) | [English](README_EN.md)

本目录只保留可公开复现的构建、发布、资产和诊断脚本。一次性分析脚本和旧 A/B 诊断入口保留为维护者本地工具，不进入公开跟踪。

## 目录结构

| 目录 | 内容 |
|---|---|
| `build/` | Windows/macOS 构建与打包入口 |
| `debug/` | 当前仍保留的 Windows 调试/诊断启动入口 |
| `ffmpeg/` | FFmpeg 运行时、开发 SDK 获取脚本，以及 decode-only 裁剪工具链 |
| `assets/` | 资产生成和字体裁剪辅助脚本 |

## 构建与打包

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

`package-win.ps1` 默认使用 `build/`，会检查版本头和 `MiaCode.exe` 是否需要刷新；若可执行文件缺失、版本过期或时间戳落后，会自动构建 `MiaCode`。

macOS:

```bash
bash scripts/build/build-macos.sh
```

```bash
QT_ROOT="$HOME/Qt/6.8.3/macos" bash scripts/build/package-mac.sh
```

输出位于 `dist/`。

## 其他脚本

- `debug/Start_MiaCode_Debug.bat`：发布包内唯一 Windows 调试启动入口。
- `debug/Start_MiaCode_SoftwareVideoDecode.bat`、`debug/Start_MiaCode_QtPluginDiag.bat`：公开保留的支持诊断入口，不随 Windows 发布包分发。
- `ffmpeg/ensure-windows-ffmpeg.ps1`、`ffmpeg/ensure-macos-ffmpeg.sh`：获取导出用独立 `ffmpeg`。
- `ffmpeg/ensure-windows-ffmpeg-dev.ps1`：获取 Windows QtAVPlayer 预览解码开发 SDK。
- `ffmpeg/trim/`：构建 Windows decode-only FFmpeg dev SDK 的裁剪工具链。
- `assets/subset_hud_font.py`：HUD 字体子集生成，详见 `assets/README_font_subset.md`。
- `assets/gen_same_lane_v_slides.py`：生成同轨 V 型 slide 参考数据。
