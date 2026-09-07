# 同步机制重构计划

## 目标

重构播放器、时间轴与 QML 编辑器之间的同步机制，删除 v1 编辑器与时间轴关联路径。同步数据采用值对象传递，播放跟随与离散导航由单一控制器管理，程序化选区不在 JavaScript 调用栈内同步回调 C++。

## 执行规则

- 每个执行阶段开始前先更新本文档的状态和范围。
- 只维护本文档以及代码结构变化直接影响的项目指南。
- 删除 v1 同步代码及回退分支。
- 完成代码修改后执行 Release 增量编译。
- 验证操作由用户发出指令后执行。

## 执行顺序

1. **同步边界设计与现状清点：已完成**
   - 确定统一值对象、控制器职责、所有权与 QML 接口。
   - 清点播放器位置、时间轴跟随、离散导航、编辑器状态发布和 v1 关联入口。
   - 采用单一 `EditorSyncController`，由 MainWindow 持有并由 QML 应用上下文直接暴露。
   - 跟随状态使用文本位置值对象；离散导航使用序号、文档身份和完成确认。
   - 编辑器上下文与触控编辑请求纳入同一控制器，删除 MainWindow 回调字段。
2. **统一同步控制器：已完成**
   - 新增单一控制器和稳定值对象。
   - 迁移播放跟随、暂停投影、时间轴点击、书签与诊断导航。
   - 播放跟随已改为文本位置状态投影，已删除缓存导航和 PlainCodeEditor 跟随分支。
3. **QML 编辑器接入：已完成**
   - 使用状态投影和导航请求确认。
   - 隔离程序化选区期间的编辑器状态发布。
   - 应用上下文已传递控制器；SourceEditor 已接入 readiness、延迟上下文发布、跟随状态和离散导航确认。
4. **删除旧同步路径：已完成**
   - 删除 MainWindow handler 注册、QmlDocumentModel 播放导航队列、隐藏编辑器跟随与所有回退分支。
   - 已清理 QmlDocumentModel 的同步桥接、触控编辑旧分支及 PlainCodeEditor 跟随接口。
   - TimelineQuickModel 已改用不可变文本快照执行增量解析。
   - 已删除 MainWindow 中失去入口的 TimelineView 实例、reference mirror、事件分支和实现文件。
5. **项目指南同步：已完成**
   - 更新实际发生变化的模块归属和跨模块同步说明。
   - 已更新架构布局、功能索引、跨链同步和硬编码登记。
6. **Release 增量编译：已完成**
   - 使用当前工作区既有构建目录，并行任务数上限为 4。
   - `cmake --build build --target MiaCode --parallel 4` 已完成 200/200，目标已链接并部署。
   - 删除 `TimelineView.h` 后暴露的 `QShortcut` 传递声明依赖已改为 `MainWindow.h` 的直接前置声明。
7. **高压力交互监测：已完成**
   - 启动 Release 程序，由用户执行播放器、时间轴和编辑器高频交互。
   - 后台采集进程私有内存、工作集、CPU、句柄、线程和诊断日志。
   - 两轮高频 GC 会话均发生异常退出；第二轮持续 106.9 秒，退出码 3。
   - 第二轮私有内存峰值 986.3 MB、工作集峰值 975.4 MB，期间存在多次明显回落；句柄和线程未呈连续增长。
   - 触发条件已收敛为编辑器点击后真实光标跨行定位；播放、暂停和代码跟随开关均非必要条件。
8. **编辑器定位退出分析：已完成**
   - 追踪编辑器点击、延迟上下文发布、控制器接收、时间轴定位与预览定位链。
   - 对照异常退出前最后日志、进程退出码和 Windows 故障记录，定位同步重入或生命周期错误。
   - 捕获会话 PID 25348 在点击编辑器其他行后退出，异常码 `0xC0000005`，模块为 `Qt6Qml.dll + 0x169A39`，与历史 QV4 垃圾回收故障地址一致。
   - 实际调用链为 `SourceEditor.onCursorPositionChanged` → `editorContextTimer` → `EditorSyncController::setEditorContext` → 同步发出 `editorContextChanged` 和 `caretLocationPublished` → `MainWindow` → `TimelineQuickStateBridge::setCursorSeconds` → `renderStateChanged`。
   - QML Timer 仅延迟了入口；控制器信号到 MainWindow、时间轴桥和 QuickItem 的后续调用仍位于同一 JavaScript 调用栈，跨行点击稳定触发 QV4 GC 栈损坏。
   - 压力会话内存存在周期性回落，句柄和线程未呈连续增长；本次退出属于同步重入造成的内存安全故障，缺少内存耗尽证据。
9. **编辑器定位语义对齐：已完成**
   - 普通点击移动编辑 caret 并更新时间轴 caret，时间轴保持当前视口。
   - Ctrl/Command+点击在播放中先暂停，再 seek 播放位置、居中时间轴 playhead，并更新时间轴 caret。
   - 播放中的编辑器指针按下立即暂停。
   - 播放期间隐藏普通编辑 caret 并显示独立跟随光标，暂停后由编辑器焦点恢复普通 caret。
   - `EditorSyncController` 将 QML 发起的 caret、指针交互和预览 seek 合并到队列边界，按文档身份与位置去重后交付。
   - 删除普通 caret 到强制居中入口的错误连接，普通 caret、预览 seek、时间轴反向导航保持独立事件语义。
   - 触控编辑 Ctrl 状态与编辑后的预览锚点通过控制器队列交付；失焦、输入法组合和页面切换会清除 Ctrl 状态。
   - 程序化导航清除手动 caret 去重缓存，后续回到相同位置的真实手动定位仍会交付。
   - 已核对控制器声明与连接，修正定位日志和跟随光标显示条件，同步功能索引与跨链说明。
10. **定位语义回归修正与增量编译：已完成**
   - 固定普通 caret 更新时间轴时的非居中语义。
   - 将播放点击暂停和普通 caret 隐藏直接绑定播放状态。
   - 程序化导航开始时清除 QML 与控制器内挂起的手动 caret。
   - caret 高频路径使用既有文档身份并仅在编辑活动状态变化时刷新预览上下文。
   - `cmake --build build --target MiaCode --parallel 4` 已完成 191/191，Release 可执行文件已链接并部署。
11. **普通 caret 闪烁恢复：已完成**
   - 首次调整仅恢复了 delegate 可见性控制，运行结果仍保持常亮。
   - `cmake --build build --target MiaCode --parallel 4` 已完成 10/10，Release 可执行文件已重新链接并部署。
12. **普通 caret 系统周期闪烁：已完成**
   - 使用 `Application.styleHints.cursorFlashTime` 驱动自定义 caret 透明度。
   - caret 移动和重新获得焦点时立即恢复亮态。
   - 播放期间隐藏普通 caret。
   - `cmake --build build --target MiaCode --parallel 4` 已完成 10/10，Release 可执行文件已重新链接并部署。

## 完成标准

- 播放跟随通过只读状态投影更新 QML 编辑器。
- 离散导航具有序号、文档身份校验和应用确认。
- 程序化选区期间不发生同步 QML 到 C++ 编辑器状态回调。
- 同步主路径不引用 PlainCodeEditor、TimelineView 或 v1 回退状态。
- 播放器、时间轴和编辑器同步代码只有一条 v2 路径。
- Release 增量编译成功。
