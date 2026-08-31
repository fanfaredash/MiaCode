# MiaCode 依赖 allowlist

> 归属：[QML_UI_V2_PHASE1_TODO_ZH.md](../specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md) 阶段 3.5 第 4 项。
>
> 本文登记 **`MiaCode` 主程序 target 链接的每一个库**：属于哪一层、在什么平台条件下存在、
> 代码里的直接使用点在哪、什么时候加载、怎么验证。目标不是把部署包里的 DLL 数量压到最低，
> 而是让每一条依赖都能说明它为谁存在——没有主人的依赖不许留在链接行上。
>
> **漂移守卫**：`dependency_allowlist_spec`（`src/tools/deps/DependencyAllowlistSpec.cpp`）
> 解析 `CMakeLists.txt` 里全部 `target_link_libraries(MiaCode …)` 调用并与下面三张表比对。
> 新增依赖没写进表、表里留着已删依赖、禁止表里的库被链接、Qt 版本没锁、QtAVPlayer 头文件
> 泄漏出媒体适配层——五种漂移都会让 `ctest -R dependency_allowlist_spec` 失败。
> **改依赖和改本文必须同一次提交。**

Qt 最低版本锁定：`6.8`

版本锁不是风格问题：`Qt6::MultimediaQuickPrivate` 是私有模块，没有跨版本兼容承诺，
所以 `CMakeLists.txt` 里**每一处** `find_package(Qt6 <ver> …)` 都必须写同一个版本号，
守卫会逐个比对。

## 分层

| 层 | 含义 |
| --- | --- |
| 宿主 | Qt Quick/QML 应用宿主本身：没有它进程起不来 |
| 渲染 | QSG 场景、着色器、矢量图标 |
| 媒体 | 音频后端与背景视频解码 |
| 导出 | 视频/封面/ZIP 导出管线 |
| 平台 | 只在某个操作系统上存在的系统库 |
| 遗留 | 已判定要移除、但当前仍被链接（各自注明退出阶段） |

## 允许链接进 `MiaCode`

| 依赖 | 分层 | 平台条件 | 直接使用点 | 加载时机 | 验证方式 |
| --- | --- | --- | --- | --- | --- |
| `Qt6::Core` | 宿主 | 全平台 | 全模块 | 进程启动 | 链接期；全量 CTest |
| `Qt6::Gui` | 宿主 | 全平台 | `QGuiApplication`、`QImage`/`QPainter`（封面与 HUD 合成）、字体 | 进程启动 | 链接期；`cover_composite_renderer_spec` |
| `Qt6::Qml` | 宿主 | 全平台 | `QQmlApplicationEngine`（`QmlUiBootstrap`）、全部 `Qml*` 模型 | 进程启动 | 链接期；`qml_*_spec` 组 |
| `Qt6::Quick` | 宿主 | 全平台 | `QQuickWindow`、`src/preview/quick_scene/`、`src/timeline/quick/` | 进程启动 | 链接期；`qml_*_spec` 组 |
| `Qt6::QuickControls2` | 宿主 | 全平台 | `src/app/qml_ui/` 全部 QML 页面与控件 | 首个 QML 组件实例化 | 链接期；`qml_main_menu_spec` 等 |
| `Qt6::Svg` | 渲染 | 全平台 | `makeSettingsGearIcon`（`MainWindowShared.cpp`）经 `QSvgRenderer` 渲染齿轮图标 | 首次构建工具栏图标 | 链接期；阶段 4 随 `MainWindowShared` 一并复核 |
| `Qt6::Multimedia` | 媒体 | 全平台 | `QtPreviewSfxRuntime*`（判定音效）、`QVideoFrame` 桥接 | 预览首次播放 / 首帧解码 | 链接期；`HAVE_QT_MULTIMEDIA=1`；预览手工回归 |
| `Qt6::MultimediaQuickPrivate` | 媒体 | `WIN32 OR APPLE` | **不由 `src/` 直接使用**；仅供 `third_party/QtAVPlayer` 的 `QT_AVPLAYER_MULTIMEDIA` 桥编译 `QAVVideoFrame -> QVideoFrame` | 背景视频首帧解码 | 链接期；`qtavplayer_platform_spec`；本文「QtAVPlayer 媒体适配层」表 |
| `${QtAVPlayer_LIBS}` | 媒体 | `WIN32 OR APPLE`（需 `MIACODE_FFMPEG_DEV_DIR`） | `PreviewStageMediaHost*`（PV/BG 解码）、`PreviewSharedD3D11Device`（D3D11VA 共享设备） | 背景视频首帧解码 | `qtavplayer_platform_spec`；macOS 打包契约 |
| `soundtouch` | 媒体 | 全平台 | 变速播放与音频处理（`src/audio/`、`src/tools/media/`） | 首次变速播放 / 音频处理作业 | 链接期；音频手工回归 |
| `bass` | 媒体 | 全平台（Win: `bass.lib`，macOS: `libbass.dylib`） | `BassPreviewAudioBackend`、`BassExportAudioBackend` | 预览音频后端初始化 | `MIACODE_HAS_BASS_AUDIO=1`；macOS 打包契约校验 dylib 已随包 |
| `bassmix` | 媒体 | 全平台 | 同上（混音总线） | 同上 | 同上 |
| `miniz` | 导出 | 全平台 | `ChartZipPackager`（ZIP 打包导出） | 触发 ZIP 导出 | `chart_zip_packager_spec` |
| `Qt6::Widgets` | 遗留 | 全平台 | 隐藏 `MainWindow` 及 `src/app/ui/` 的 widget 辅助件、`HudFontSettings` | 进程启动（隐藏窗口构造） | **阶段 4 退出**：删除 `MainWindow` 后从本表移入禁止表 |
| `-framework AppKit` | 平台 | `APPLE` | `UiNativeWindowThemeMac.mm`、`QmlUiWindowChrome.mm`（原生标题栏/外观） | 根窗口创建 | 链接期；macOS 冷启动走查 |
| `d3d11` | 平台 | `WIN32` | 共享预览设备、D3D11 导出会话、stage-media host（`src/preview/runtime/`） | 预览首次创建渲染设备 | 链接期；Windows 冷启动走查 |
| `dxgi` | 平台 | `WIN32` | `GpuDevicePolicy`、`ProcessDiagnostics`、`TimelineQuickItem`（适配器枚举与显存计量） | 启动诊断 / 预览创建 | 链接期；Windows 冷启动走查 |
| `d3dcompiler` | 平台 | `WIN32`（MinGW 必需；MSVC 走 `#pragma comment`） | QtAVPlayer 的 `D3DCompile` | 背景视频首帧解码 | 链接期（MinGW 缺失即链接失败） |
| `opengl32` | 平台 | `WIN32`（MinGW） | QtAVPlayer D3D11/OpenGL 纹理互操作 | 背景视频首帧解码 | 链接期 |
| `winmm` | 平台 | `WIN32` | 高精度多媒体计时器 | 播放/导出计时启动 | 链接期 |
| `ole32` | 平台 | `WIN32` | Windows Core Audio 端点通知 COM API | 音频设备枚举 | 链接期 |
| `avrt` | 平台 | `WIN32` | `AvSetMmThreadCharacteristicsW`（多媒体线程优先级） | 音频/渲染线程启动 | 链接期 |
| `User32` | 平台 | `WIN32` | `RegisterPowerSettingNotification` | 启动诊断注册 | 链接期 |
| `Wtsapi32` | 平台 | `WIN32` | 会话锁定/解锁通知（卡顿冻结诊断） | 启动诊断注册 | 链接期 |
| `version` | 平台 | `WIN32`（MinGW） | 启动诊断的文件版本查询 | 启动诊断 | 链接期 |
| `rstrtmgr` | 平台 | `WIN32`（MinGW） | 媒体工具的 Restart Manager 占用进程查找 | 媒体工具报「文件被占用」时 | 链接期 |
| `dwmapi` | 平台 | `WIN32` | `QmlUiWindowChrome` 的 `DwmExtendFrameIntoClientArea` | 根窗口创建 | 链接期；Windows 冷启动走查 |

## 构建期组件

`find_package(Qt6 … REQUIRED COMPONENTS …)` 里可以出现不链接进运行时的组件，但必须在这里
说明——否则一个「REQUIRED 但没人链接」的组件只会让别人的机器白白配置失败。守卫要求产品作用域
`find_package` 的每个组件要么在允许表里有对应的 `Qt6::<组件>`，要么出现在下表。

| 组件 | 用途 | 为什么不算运行时依赖 |
| --- | --- | --- |
| `ShaderTools` | `qt6_add_shaders` 把 `src/intro/shaders/*.frag`、`*.vert` 编译成 `.qsb` 资源 | 只在构建期运行 `qsb` 工具；产物是资源文件，`MiaCode` 不链接 `Qt6::ShaderTools` |

> 2026-09-01 本次同时删掉了 `OpenGL` 组件：它被写成 `REQUIRED` 但没有任何 target 链接
> `Qt6::OpenGL`。运行时的 `QtOpenGL` 框架是 `Qt6::Quick` 的传递依赖，与这个组件声明无关。

## 禁止链接进 `MiaCode`

| 依赖 | 原因 | 归属 |
| --- | --- | --- |
| `Qt6::Network` | Net 页面已从 v2 产品运行时移除（见架构文档第 10 节）。引擎代码保留但**不进产品**，否则一个没有用户入口的依赖会被误当成产品依赖。 | `net_client_spec`（`src/tools/net/`，仅 `MIACODE_BUILD_DEV_TOOLS=ON` 时编译）。恢复 Net 页面时把本行移回允许表，并在允许表里写明入口与加载时机。 |
| `Qt6::Test` | 只属于 dev-tools spec 可执行文件，不得进入产品进程。 | `MIACODE_BUILD_DEV_TOOLS` 分支下的各 spec target |

## 传递依赖：本文管不到、也不假装管得到的部分

上面三张表管的是**直接链接边**。可执行文件实际加载的框架比它多，因为 Qt 模块之间自己有依赖。
把这一点写清楚，是为了避免「从链接行删掉 X」被误读成「部署包里没有 X 了」：

- **`QtNetwork` 仍会被加载**，它是 `Qt6::Qml` 的传递依赖（`QtQml`、`QtQuick`、`QtQuickControls2`、
  `QtQmlModels`、`QtQmlMeta`、`QtMultimedia` 全部依赖它）。本次把 `Qt6::Network` 从
  `MiaCode` 的链接行删除，改变的是**产品是否自己使用网络**，不是部署包里少一个框架。
  只要 UI 由 Qt Quick 承载，`QtNetwork` 就一定在包里——这不是可以「清掉」的东西。
- **`QtOpenGL` 同理**，由 `Qt6::Quick` 带入。
- 复核方法（macOS）：

```bash
otool -L build-macos/MiaCode.app/Contents/MacOS/MiaCode | grep Qt
```

  Windows 用 `dumpbin /dependents`。守卫不跑这一步：它依赖已构建产物和平台工具链，属于
  发布前的人工/CI 检查，记在阶段 4 的「依赖记录」验收里。

## QtAVPlayer 媒体适配层

`Qt6::MultimediaQuickPrivate` 是本仓库唯一的 Qt 私有模块依赖，它存在的唯一理由是
`third_party/QtAVPlayer` 的 `QT_AVPLAYER_MULTIMEDIA` 帧桥。为了让这条私有依赖可控，
**只有下面这些文件允许 `#include <QtAVPlayer/…>`**；守卫会扫描整棵 `src/` 树逐一比对。

| 文件 | 职责 |
| --- | --- |
| `src/preview/runtime/PreviewStageMediaHost.cpp` | 适配层入口：PV/BG 播放器生命周期 |
| `src/preview/runtime/PreviewStageMediaHost_Backend.cpp` | 后端选择与帧回调 |
| `src/preview/runtime/PreviewStageMediaHost_Media.cpp` | 媒体装载与轨道选择 |
| `src/preview/runtime/PreviewStageMediaHost_Playback.cpp` | 播放/暂停/seek |
| `src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp` | 解码诊断计数 |
| `src/preview/runtime/PreviewStageMediaHost_Timeout.cpp` | 解码超时与 EOF 归因 |
| `src/preview/runtime/PreviewSharedD3D11Device.cpp` | Windows D3D11VA 共享设备发布 |

**后续项（阶段 4 之后，未排期）**：改用公共 `QtMultimedia` / `QVideoSink` API，去掉
`Qt6::MultimediaQuickPrivate`。前置条件是 QtAVPlayer 的帧桥不再需要 `qsgvideonode` 私有头，
或本仓库自带一份公共 API 的帧转换。在那之前，Qt 版本锁与本表就是这条私有依赖的全部约束。
