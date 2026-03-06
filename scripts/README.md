# Scripts

[中文](README.md) | [English](README_EN.md)

本目录包含构建与打包脚本。

## Windows

- `build-win.ps1`：自动安装 Qt、执行 CMake 构建，并调用 `package-win.ps1` 生成发布包
- `package-win.ps1`：对已有 Windows 构建结果执行 `windeployqt` 并输出 `dist/`

常用示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot>
```

输出：
- `dist/MiaCode-v<version>-win64`
- `dist/MiaCode-v<version>-win64.zip`

## macOS

- `build-macos.sh`：自动安装 Qt、执行构建，并调用 `package-mac.sh` 生成发布包
- `package-mac.sh`：对已有 macOS 构建结果执行 `macdeployqt`、签名并输出 `dist/`

常用示例：

```bash
bash scripts/build-macos.sh
```

```bash
QT_ROOT="$HOME/Qt/6.8.3/macos" bash scripts/package-mac.sh
```

可选环境变量：
- `CMAKE_OSX_ARCHITECTURES=arm64` 或 `x86_64`
- `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`
- `MACOS_CODESIGN_IDENTITY="-"`：使用 ad-hoc 签名；若有正式签名证书，可替换为实际 identity

输出：
- `dist/MiaCode-v<version>-macos`
- `dist/MiaCode-v<version>-macos.zip`
