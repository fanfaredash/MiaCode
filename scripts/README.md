# Scripts

[中文](README.md) | [English](README_EN.md)

本目录包含构建与打包脚本。

## Windows

- `build-win.ps1`：自动安装 Qt、执行 CMake 构建，并调用 `package-win.ps1` 生成发布包
- `package-win.ps1`：对已有 Windows 构建结果执行 `windeployqt` 并输出 `dist/`

    - `package-win.ps1` 默认使用 `build/` 作为构建目录。
    - 打包前会自动预检查 `build/generated/AppVersion.h` 与 `build/<Config>/MiaCode.exe` 是否和当前 `CMakeLists.txt` 一致；若可执行文件缺失、版本过期或时间戳落后，会自动执行 `cmake --build build --target MiaCode --config <Config> --parallel 8`。
    - 如果不指定 `-QtRoot`，脚本会尝试从当前 `PATH` 中查找 `windeployqt`。
    - `-BuildJobs` 默认是 `8`，可按机器情况覆盖。
    - 如果希望自动安装 Qt 并完成构建打包，请使用 `build-win.ps1`。

常用示例：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-win.ps1
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot>
```

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-win.ps1 -QtRoot <QtRoot> -BuildJobs 8
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
- `dist/MiaCode-v<version>-macos-apple-silicon` 或 `dist/MiaCode-v<version>-macos-intel`
- `dist/MiaCode-v<version>-macos-apple-silicon.zip` 或 `dist/MiaCode-v<version>-macos-intel.zip`
