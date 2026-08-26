# QML 运行期意外退出排查记录

## 1. 文档目的

本文记录 2026 年 8 月 24 日后出现的 Windows/Linux 运行期意外退出问题，供后续开发者和 agents 直接接手。

当前证据已经把触发范围收敛到“预览播放期间的代码跟随导航链”。故障最终发生在 Qt QML JavaScript 引擎的垃圾回收阶段。语句级根因仍需通过代码 A/B 验证继续拆分。

## 2. 用户侧现象

- Windows/Linux 会在正常播放期间突然退出。
- 退出与鼠标、键盘或编辑操作无稳定关系；保持零交互同样会发生。
- macOS 大概率无法复现。
- 2026 年 8 月 24 日大规模 QML UI 重构前，用户未观察到同类运行期退出。
- 退出前没有界面错误提示。

## 3. 已确认的两类故障

日志中存在两种地址稳定、阶段不同的访问冲突。后续分析时需要分别处理。

### 3.1 本文关注的播放期 QML 故障

五次历史运行期故障与本次压力复现均满足：

- Windows 异常码：`0xc0000005`
- 模块：`Qt6Qml.dll`
- 模块内偏移：`0x169A39`
- 线程：主 GUI 线程
- 访问类型：读取空地址
- 随后日志：`crash/signal_handler sig=11`

已确认的历史样本：

| PID | 本地时间 | 模块偏移 |
| --- | --- | --- |
| 86548 | 2026-08-24 16:37 | `Qt6Qml.dll + 0x169A39` |
| 65324 | 2026-08-24 18:14 | `Qt6Qml.dll + 0x169A39` |
| 73976 | 2026-08-24 21:20 | `Qt6Qml.dll + 0x169A39` |
| 4692 | 2026-08-25 16:37 | `Qt6Qml.dll + 0x169A39` |
| 47692 | 2026-08-26 15:20 | `Qt6Qml.dll + 0x169A39` |

本次压力复现样本：

| PID | 条件 | 结果 |
| --- | --- | --- |
| 40408 | 高频 GC、代码跟随开启、开始播放 | 立即退出，同一偏移 |
| 47772 | 高频 GC、代码跟随开启、开始播放 | 立即退出，同一偏移 |
| 2516 | 高频 GC、代码跟随关闭、连续播放 | 约 2 分钟持续正常 |
| 2516 | 播放中重新开启代码跟随 | 立即退出，同一偏移 |

### 3.2 独立的退出阶段 QtCore 故障

部分正常关闭会话在析构完成后出现：

- 模块：`Qt6Core.dll`
- 模块内偏移：`0x26988C`
- 访问：读取空对象的 `+0x50`
- 常见位置：工作线程清理阶段

该故障发生在应用析构体主体完成之后，与本文的播放期 `Qt6Qml.dll` 故障地址、线程和生命周期阶段均不同，应建立独立排查项。

## 4. 崩溃地址解析

对当前 `Qt6Qml.dll` 执行反汇编后，`+0x169A39` 对应指令为：

```asm
mov rcx, qword ptr [r8]
```

异常上下文中 `r8 = 0`，因此该指令读取空地址。

最近的导出符号把该地址定位到：

```text
QV4::MemoryManager::collectFromJSStack(QV4::MarkStack*) + 0x89
```

这表示故障发生在 QV4 垃圾回收器扫描 JavaScript 栈并标记存活对象时。当前证据支持以下结论：

1. 故障属于 QML/JavaScript 执行上下文或对象生命周期问题。
2. GPU/QSG 渲染故障无法解释固定的 QV4 GC 调用点。
3. 音频初始化日志只是播放开始前最后一批已写入事件，异常线程和故障模块均指向 QML 主线程。
4. macOS 的复现差异可以由 Qt 构建、JIT/解释器实现、垃圾回收时机和事件循环调度差异解释；当前没有 macOS 样本可进一步确认。

## 5. 稳定复现方法

### 5.1 环境

- 构建类型：Release
- 编译器：MinGW 13.1
- 构建系统：Ninja
- Qt：工作区当前随程序部署的 Qt 6.11.1
- 工程：`ミッドナイト・リフレクション`
- 谱面：`Master 13+`

### 5.2 压力条件

启动前设置：

```powershell
$env:QV4_MM_AGGRESSIVE_GC='1'
& 'C:\Users\KaedeKR\Workspace\MiaCode\build\MiaCode.exe' --debug
```

该变量提高 QV4 垃圾回收频率，使原本依赖时序的故障变为稳定复现。

### 5.3 对照结果

1. 打开相同工程。
2. 保持“代码跟随”开启。
3. 点击播放。
4. 程序立即在 `Qt6Qml.dll + 0x169A39` 退出。
5. 重新启动压力会话。
6. 关闭“代码跟随”。
7. 播放一轮以上，程序持续正常。
8. 播放中重新开启“代码跟随”。
9. 程序立即在相同地址退出。

该对照把必要触发条件收敛到“播放状态下启用代码跟随”，并排除了纯播放渲染、纯音频播放和常驻 QML UI 本身。

## 6. 代码调用链

### 6.1 播放跟随分支

入口位于：

```text
src/app/mainwindow/sections/timeline/MainWindow.TimelinePreviewFollowSync.cpp
```

关键分支：

- `previewFollowEnabled_ == false`：更新只读跟随装饰，不移动真实光标。
- `previewFollowEnabled_ == true && qtPreviewPlaying_ == true`：调用 `requestQmlEditorNavigation(...)`，推动可见 QML 编辑器移动选区和视口。

当前版本已包含重复目标抑制，但压力测试仍能在首次或后续有效导航时触发故障。

### 6.2 C++ 到 QML 的同步信号

`MainWindow::requestQmlEditorNavigation(...)` 调用注册在 `QmlDocumentModel` 中的处理器：

```text
src/app/qml_ui/QmlDocumentModel.cpp:36
```

处理器在同一调用栈中同步发出：

```cpp
emit qmlEditorNavigationRequested(...);
```

这会直接进入 QML `Connections` 处理函数。该层没有事件队列边界。

### 6.3 QML 编辑器选区与延迟滚动

信号由以下位置处理：

```text
src/app/qml_ui/editor/SourceEditor.qml
```

调用过程：

```text
onQmlEditorNavigationRequested
  -> selectBackendNavigation(...)
     -> sourceArea.select(start, end)
        -> onCursorPositionChanged
           -> updateCursorPosition()
           -> documentSession.setQmlEditorInteraction(...)
     -> centerCursorInView()
        -> Qt.callLater(() => { ... })
```

这里同时存在三种 QV4 压力源：

1. C++ 同步信号进入 JavaScript。
2. `TextArea.select()` 同步触发光标相关属性和处理函数。
3. `Qt.callLater()` 创建延迟 JavaScript 闭包，并捕获 `root`、`sourceArea`、`editorScroll` 等 QML 对象。

在高频 GC 条件下，垃圾回收会在这条嵌套链附近立即运行，随后在扫描 JavaScript 栈时读取空指针。

### 6.4 完成弹窗的联动监听

`CompletionPopup.qml` 在 2026 年 8 月 24 日 16:01 的提交 `5ee23d38` 中重写了锚定和尺寸逻辑。当前实现连接到 `TextArea`：

```qml
Connections {
    target: root.editor
    function onCursorRectangleChanged() { root.updateAnchor() }
    function onXChanged() { root.updateAnchor() }
    function onYChanged() { root.updateAnchor() }
    function onWidthChanged() { root.updateAnchor() }
    function onHeightChanged() { root.updateAnchor() }
}
```

代码跟随移动真实光标时，`cursorRectangleChanged` 会同步进入该监听器。弹窗隐藏时 `updateAnchor()` 会提前返回，但 JavaScript 调用本身仍发生。

因此，完成弹窗监听属于已确认触发链的一部分。它是否为产生损坏的语句，仍需 A/B 构建验证。

## 7. 提交范围

### 7.1 主要引入提交

`88bee25a`，2026-08-24 12:04:35：

```text
fix(qml-ui): restore editor navigation and authoring
```

该提交引入或接通：

- `QmlEditorNavigationBridge`
- `MainWindow` 到 `QmlDocumentModel` 的导航处理器
- `qmlEditorNavigationRequested`
- `SourceEditor.qml` 的 `selectBackendNavigation()`
- 播放跟随对可见 QML 编辑器的反向导航
- `centerCursorInView()` 的延迟滚动

这条链在“代码跟随关闭”时不会走移动真实光标的分支，与压力对照结果一致。

### 7.2 同期放大因素

`5ee23d38`，2026-08-24 16:01:57：

```text
fix(qml-ui): restore completion popup anchoring, sizing and selection
```

该提交强化了完成弹窗对光标矩形和布局变化的 JavaScript 监听。首次已知运行期故障发生于 16:37，其进程在 16:31 启动，使用的构建包含此提交。

`f451ae09`，2026-08-24 16:25:08：

```text
fix(qml-ui): seek the preview from a Ctrl/Command click in the editor
```

该提交新增 `PointHandler` 与另一个 `Qt.callLater()` 路径。用户保持零交互时该手势处理器不会主动执行，因此它的直接嫌疑较低。该提交仍在首次故障进程使用的构建范围内。

### 7.3 后续改动

后续提交已经增加导航目标去重并降低播放状态广播扇出。2026 年 8 月 26 日当前 HEAD 仍可通过高频 GC 稳定复现，说明目标去重仅降低触发频率，未修复生命周期或重入问题。

## 8. 当前结论边界

### 8.1 已证实

- Windows 的多次随机退出是同一个固定 QV4 GC 故障。
- 故障发生在主 GUI 线程。
- 高频 GC 可以稳定复现。
- “播放 + 代码跟随开启”是稳定触发组合。
- 关闭代码跟随后，高频 GC 下可持续播放。
- 播放中重新开启代码跟随会立即恢复同一故障。
- 引入范围位于 8 月 24 日接通的 QML 编辑器反向导航链。

### 8.2 尚待确认

- 首个无效 QV4 栈值由哪条具体 QML 语句产生。
- `Qt.callLater()` 闭包、`TextArea.select()` 的同步属性重入、完成弹窗监听三者中的主因。
- Linux 是否落在同一个 Qt QML 内部函数；目前只有用户侧相同症状，没有 Linux 地址日志。
- macOS 的复现差异属于 JIT、GC 调度、Qt 构建还是窗口事件时序。

## 9. 推荐的代码 A/B 顺序

后续 agent 应保持 `QV4_MM_AGGRESSIVE_GC=1`，每项使用相同工程和操作流程。

### A：合并延迟滚动

将 `centerCursorInView()` 从“每次调用创建一个 `Qt.callLater` 闭包”改为单一、可合并的待处理刷新：

- 同一事件循环只保留最新目标。
- 回调执行前检查编辑器、文档 revision 和当前难度。
- 保持滚动结果与现有行为一致。

若该项使代码跟随压力播放稳定，主因即可定位到延迟闭包或其重入时机。

### B：隔离完成弹窗光标监听

让 `CompletionPopup` 的光标和布局 `Connections` 仅在弹窗可见时启用，或由单一合并刷新处理。

若 A 仍崩溃而 B 稳定，主因位于隐藏弹窗仍响应程序化光标移动的路径。

### C：建立 C++/QML 队列边界

把 `qmlEditorNavigationRequested` 的交付改为只保存最新值，再由一个排队刷新应用到 QML。目标是让以下步骤不处于同一个嵌套调用栈：

```text
播放 tick -> C++ handler -> QML signal -> TextArea.select -> C++ interaction publish
```

### D：保留压力回归

修复完成后至少验证：

- 代码跟随开启，完整播放 10 轮。
- 播放期间跨多个谱面 token。
- 代码跟随开关反复切换。
- 暂停、继续、停止、重新播放。
- 完成弹窗打开和关闭两种状态。
- 普通 GC 与高频 GC 两种环境。
- Windows Release 构建。
- Linux Release 构建。

验收要求为高频 GC 下无 `Qt6Qml.dll + 0x169A39`，普通环境持续运行无意外退出，编辑器跟随行为与现状一致。

## 10. 日志与诊断状态

历史日志目录：

```text
C:\Users\KaedeKR\Downloads\ミッドナイト・リフレクション\.miacode\logs
```

本次创建的诊断目录：

```text
C:\Users\KaedeKR\Workspace\MiaCode\diagnostics\logs
C:\Users\KaedeKR\Workspace\MiaCode\diagnostics\dumps
```

Windows 用户级 LocalDumps 已为 `MiaCode.exe` 配置完整转储：

```text
HKCU\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\MiaCode.exe
DumpType  = 2
DumpCount = 10
DumpFolder = C:\Users\KaedeKR\Workspace\MiaCode\diagnostics\dumps
```

当前崩溃恢复实现会在访问冲突后进入 `SIGSEGV` 信号处理器并结束进程，本次复现没有生成 WER 转储。后续若需要完整调用栈，可在调试构建中提供关闭应用内崩溃处理器的诊断开关，或在能够捕获首个访问冲突的调试器下启动进程。

## 11. 接手摘要

最短接手路径：

1. 阅读 `MainWindow.TimelinePreviewFollowSync.cpp` 的代码跟随开启分支。
2. 阅读 `QmlDocumentModel.cpp` 构造函数中的导航处理器。
3. 阅读 `SourceEditor.qml` 的 `selectBackendNavigation()`、`centerCursorInView()`、`onCursorPositionChanged`。
4. 阅读 `CompletionPopup.qml` 的光标矩形监听。
5. 使用 `QV4_MM_AGGRESSIVE_GC=1` 运行 Release 程序。
6. 以“代码跟随关闭稳定、重新开启立即退出”为基准执行 A/B。

当前最强结论：播放代码跟随链中的同步 QML 导航与延迟 JavaScript 工作共同触发 QV4 GC 栈损坏；修复应先消除该链的重入和重复闭包创建，再验证完成弹窗监听的贡献。
