# MainWindow 分片结构

`MainWindow.cpp` 保留主入口、事件主干和公共辅助函数，具体功能通过 `#include` 组合到不同分片中：

- `sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - 负责窗口骨架、菜单、工具栏、dock/splitter 以及基础控件装配。
- `sections/document/MainWindow.DocumentFlow.cpp`
  - 负责打开/保存流程、脏状态、字段切换、侧边栏与页面切换。
- `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - 负责时间轴数据刷新、光标同步、预览定位与播放位置流转。
- `sections/validation/MainWindow.ValidationFlow.cpp`
  - 负责语法检查执行、错误列表渲染、高亮装饰与跳转。
- `sections/editor/MainWindow.EditorDisplay.cpp`
  - 负责编辑器显示偏好，例如字体、行距及相关辅助逻辑。
- `sections/preferences/MainWindow.PreferencesDialog.cpp`
  - 负责首选项对话框与持久化。
- `sections/preview/MainWindow.PreviewSessionFlow.cpp`
  - 负责旧预览会话生命周期与 IPC。

## 现有页面布局

当前主窗口整体是左右分栏布局：

- 最左侧是字段侧栏，由 `outlineDock` 和其中的 `outlineList_` 组成。
  - 用于显示 `Metadata`、各个难度页以及“添加难度”入口。
  - 这部分实现位于 `sections/frame/MainWindow.BootstrapAndMenus.cpp`，仍属于 `mainwindow` 分片体系内。
- 左侧为 `previewLeftColumn_`，内部上下分为两块：
  - 上方是编辑区，包含顶部 `editorHeader_` 和中部 `editorStack_`。
  - 下方是 `bottomTabs_`，用于承载 `Timeline` 和语法检查错误列表。
- 右侧为 `previewPanel_`，内部手动计算布局，主要包含：
  - 上方预览画布 `previewCanvasFrame_` / `previewCanvasContainer_`。
  - 中部预览控制区 `previewControlCard_`。
  - 下方统计区 `previewStatsCard_`。

编辑区内部又分为两个页面：

- `metadataPage_`
  - 用于谱面信息设置。
  - 切换到该页时，`bottomTabs_` 会隐藏。
- `chartPage_`
  - 用于难度谱面编辑。
  - 切换到该页时，`bottomTabs_` 会显示，并默认切到 `Timeline` 标签页。

布局原则上应满足：

- 主窗口打开时或窗口尺寸变化时，`updatePreviewWorkspaceLayout()` / `updatePreviewPanelLayout()` 统一重算右侧预览区尺寸。
- 页面切换不应改变 splitter 的左右宽度分配。
- `bottomTabs_` 隐藏或重新显示后，应触发一次显式布局刷新，确保 timeline 与 preview 区域尺寸恢复正确。

## 分片规则

- `MainWindow.cpp` 和 `MainWindow.h` 保持在 `src/app/mainwindow/`。
- 新功能分片放在 `sections/<feature>/` 下。
- 如果跨分片声明开始变得嘈杂，优先补一层轻量桥接头文件，而不是把实现重新塞回 `MainWindow.cpp`。
