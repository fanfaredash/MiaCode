# 阶段 0a：移除 v1 QuickShell 外壳与其入口 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 删除 v1 QuickShell 外壳、它的启动入口和原生表面再宿主机制，使 `MiaCode` 只剩一条 UI 启动路径。

**Architecture:** v1 外壳由 `QuickShellBootstrap` 启动，通过 `QuickShellNativeSurfaceHost` 把 Widgets 表面再宿主进 QML 场景。v2 从不使用这条路径：它构造 `QuickShellController` 时 `surfaceHost` 传 `nullptr`，控制器内 29 处 `surfaceHost_` 分支在 v2 下全是死代码。删除 v1 引导后这些分支随之消失。`QuickShellController` 本身**保留**（v2 仍在用，按设计第 8 节于阶段 2 退役）。

**Tech Stack:** C++20 / Qt 6.10.2 / CMake / CTest。构建与测试一律 Release。

**前置阅读:** [架构设计](../QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md) 第 8 节阶段 0。

---

## 事实底座（已核实，勿重新推导）

- v1 专属文件外部引用**只有注释**，无代码依赖。真实代码引用仅在 `src/app/main.cpp` 的皮肤分支。
- `QuickShellController` 构造点只有两处：`QuickShellBootstrap.cpp:275`（v1，传真 surfaceHost）与 `QmlUiBootstrap.cpp:89`（v2，传 `nullptr`）。
- `MIACODE_UI_SKIN` 已登记在 `docs/ops/DEBUG_INDEX.md`；`debug_flag_index_spec` 会在代码不再读取该变量而文档仍列出时**失败**。这是本计划的自动闸门。

## 文件结构

**删除**
- `src/app/quick_shell/QuickShellBootstrap.{h,cpp}` — v1 启动编排
- `src/app/quick_shell/QuickShellNativeSurfaceHost.{h,cpp}` — Widgets 表面再宿主
- `src/app/quick_shell/QuickShellStyleBridge.{h,cpp}` — v1 主题桥
- `src/app/quick_shell/QuickShellMacSurfaceSupport.{h,mm}` — v1 macOS 表面支持
- `src/app/quick_shell/qml/` 整个目录 — v1 QML
- `resources/quick_shell_qml.qrc`

**保留**（v2 在用，阶段 2 再处理）
- `QuickShellController.{h,cpp}`、`QuickShellContracts.h`、`QuickShellPreviewCompositeSurface.{h,cpp}`、`QuickShellPreviewSurfacePolicy.h`、`QuickShellPopupPosition.h`、`QuickShellKeyboardActivation.h`

**修改**
- `src/app/main.cpp` — 删除 `UiSkin`、`resolveUiSkin()`、v1 分支
- `src/app/quick_shell/QuickShellController.{h,cpp}` — 删除 `surfaceHost` 构造参数与全部 `surfaceHost_` 分支
- `src/app/qml_ui/QmlUiBootstrap.cpp` — 构造调用少一个参数
- `CMakeLists.txt` — 删除对应源文件与 qrc 条目
- `docs/ops/DEBUG_INDEX.md`、`.claude/skills/miacode-dev-guide/references/*` — 同步

**新建**
- `src/tools/qml_ui/V1ShellRemovalSpec.cpp` — 结构契约回归

---

## Task 1: 结构契约回归（先红）

删除类改动无法用行为测试驱动，但**结构契约**可以。仓库已有先例（`debug_flag_index_spec`、`ui_text_locale_spec` 都断言源码事实）。

**Files:**
- Create: `src/tools/qml_ui/V1ShellRemovalSpec.cpp`
- Modify: `CMakeLists.txt`（dev-tools 块内，紧邻 `qml_shortcut_binding_spec`）

- [ ] **Step 1: 写失败测试**

创建 `src/tools/qml_ui/V1ShellRemovalSpec.cpp`：

```cpp
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    if (!condition) ++*failed;
    return condition;
}

QString sourceRoot() { return QStringLiteral(MIACODE_SOURCE_ROOT); }

QString readSource(const QString& relativePath)
{
    QFile file(sourceRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    // v1 外壳的启动编排、原生表面再宿主、主题桥与 macOS 表面支持全部删除。
    // QuickShellController 不在此列：v2 仍在使用，按架构设计阶段 2 退役。
    const QStringList removedFiles{
        QStringLiteral("src/app/quick_shell/QuickShellBootstrap.h"),
        QStringLiteral("src/app/quick_shell/QuickShellBootstrap.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellNativeSurfaceHost.h"),
        QStringLiteral("src/app/quick_shell/QuickShellNativeSurfaceHost.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellStyleBridge.h"),
        QStringLiteral("src/app/quick_shell/QuickShellStyleBridge.cpp"),
        QStringLiteral("src/app/quick_shell/QuickShellMacSurfaceSupport.h"),
        QStringLiteral("src/app/quick_shell/QuickShellMacSurfaceSupport.mm"),
        QStringLiteral("resources/quick_shell_qml.qrc"),
    };
    QStringList survivors;
    for (const QString& path : removedFiles) {
        if (QFileInfo::exists(sourceRoot() + QLatin1Char('/') + path)) survivors.append(path);
    }
    if (!survivors.isEmpty()) out << "  still present: " << survivors.join(QStringLiteral(", ")) << '\n';
    expect(survivors.isEmpty(), QStringLiteral("v1 shell sources are gone"), out, &failed);

    expect(!QFileInfo::exists(sourceRoot() + QStringLiteral("/src/app/quick_shell/qml")),
           QStringLiteral("the v1 shell QML directory is gone"), out, &failed);

    // 只剩一条 UI 启动路径。
    const QString mainSource = readSource(QStringLiteral("src/app/main.cpp"));
    expect(!mainSource.isEmpty(), QStringLiteral("main.cpp is readable"), out, &failed);
    QStringList entryLeftovers;
    for (const QString& token : {QStringLiteral("UiSkin"), QStringLiteral("resolveUiSkin"),
                                 QStringLiteral("MIACODE_UI_SKIN"), QStringLiteral("--ui=")}) {
        if (mainSource.contains(token)) entryLeftovers.append(token);
    }
    if (!entryLeftovers.isEmpty()) {
        out << "  main.cpp still carries: " << entryLeftovers.join(QStringLiteral(", ")) << '\n';
    }
    expect(entryLeftovers.isEmpty(),
           QStringLiteral("main.cpp has a single UI entry with no skin switch"), out, &failed);

    // 表面再宿主分支随 v1 一起消失，否则阶段 2 会继承 29 处死分支。
    const QString controller = readSource(QStringLiteral("src/app/quick_shell/QuickShellController.cpp"));
    const QString controllerHeader = readSource(QStringLiteral("src/app/quick_shell/QuickShellController.h"));
    expect(!controller.isEmpty() && !controllerHeader.isEmpty(),
           QStringLiteral("QuickShellController is still present for v2"), out, &failed);
    expect(!controller.contains(QStringLiteral("surfaceHost_"))
               && !controllerHeader.contains(QStringLiteral("QuickShellNativeSurfaceHost")),
           QStringLiteral("QuickShellController no longer branches on a native surface host"),
           out, &failed);

    // 构建系统不得再引用已删除的源文件。
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));
    QStringList cmakeLeftovers;
    for (const QString& token : {QStringLiteral("QuickShellBootstrap"),
                                 QStringLiteral("QuickShellNativeSurfaceHost"),
                                 QStringLiteral("QuickShellStyleBridge"),
                                 QStringLiteral("QuickShellMacSurfaceSupport"),
                                 QStringLiteral("quick_shell_qml.qrc")}) {
        if (cmake.contains(token)) cmakeLeftovers.append(token);
    }
    if (!cmakeLeftovers.isEmpty()) {
        out << "  CMakeLists.txt still lists: " << cmakeLeftovers.join(QStringLiteral(", ")) << '\n';
    }
    expect(cmakeLeftovers.isEmpty(),
           QStringLiteral("the build no longer references removed v1 shell sources"), out, &failed);

    if (failed != 0) {
        out << "V1ShellRemoval spec failed: " << failed << '\n';
        return 1;
    }
    out << "V1ShellRemoval spec passed.\n";
    return 0;
}
```

在 `CMakeLists.txt` 中，`miacode_add_dev_tool(qml_shortcut_binding_spec TEST ...)` 那一段的**紧后面**插入：

```cmake
    miacode_add_dev_tool(v1_shell_removal_spec TEST
        SOURCES
            src/tools/qml_ui/V1ShellRemovalSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(v1_shell_removal_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
```

- [ ] **Step 2: 运行，确认它失败**

```bash
cmake -S . -B build-macos-spec && cmake --build build-macos-spec --target v1_shell_removal_spec --parallel 4 && ./build-macos-spec/v1_shell_removal_spec
```

预期：退出码 1，5 条断言中至少 4 条 `[FAIL]`，并打印 `still present: ...` 与 `main.cpp still carries: UiSkin, resolveUiSkin, MIACODE_UI_SKIN, --ui=`。

- [ ] **Step 3: 提交这个红测试**

```bash
git add src/tools/qml_ui/V1ShellRemovalSpec.cpp CMakeLists.txt
git commit -m "test(v2): pin the v1 shell removal contract"
```

---

## Task 2: 收敛为单一 UI 启动路径

**Files:**
- Modify: `src/app/main.cpp`

- [ ] **Step 1: 删除皮肤枚举与解析函数**

删除 `src/app/main.cpp` 中整个 `enum class UiSkin { ... };`（约 70–73 行）以及紧随其后的注释行 `// Default: v2. Opt into QuickShell with --ui=v1 or MIACODE_UI_SKIN=v1.` 与整个 `UiSkin resolveUiSkin(const QStringList& arguments) { ... }` 函数（约 76–93 行）。

- [ ] **Step 2: 展平启动分支**

把 `const UiSkin uiSkin = resolveUiSkin(app.arguments());` 那一行删除。将其后的

```cpp
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine(
            uiSkin == UiSkin::QmlUiV2
                ? "phase=before_qml_ui_bootstrap_start"
                : "phase=before_quick_shell_bootstrap_start");
#endif
        if (uiSkin == UiSkin::QmlUiV2) {
```

替换为

```cpp
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_qml_ui_bootstrap_start");
#endif
        {
```

然后删除与之配对的 `else { ... }` 分支整块（其中构造 `QuickShellBootstrap` 的部分），并删除 `#include` 里的 `"quick_shell/QuickShellBootstrap.h"`。

- [ ] **Step 3: 构建**

```bash
cmake --build build-macos-spec --target MiaCode --parallel 4
```

预期：编译通过，无 error。若报未使用变量或未定义符号，说明上一步有残留分支未删净。

- [ ] **Step 4: 提交**

```bash
git add src/app/main.cpp
git commit -m "refactor(app): collapse startup to the single QML UI entry"
```

---

## Task 3: 删除 v1 引导与表面再宿主，简化控制器

**Files:**
- Delete: `src/app/quick_shell/QuickShellBootstrap.{h,cpp}`、`QuickShellNativeSurfaceHost.{h,cpp}`、`QuickShellStyleBridge.{h,cpp}`、`QuickShellMacSurfaceSupport.{h,mm}`
- Modify: `src/app/quick_shell/QuickShellController.{h,cpp}`、`src/app/qml_ui/QmlUiBootstrap.cpp`、`CMakeLists.txt`

- [ ] **Step 1: 删除文件**

```bash
git rm src/app/quick_shell/QuickShellBootstrap.h src/app/quick_shell/QuickShellBootstrap.cpp \
       src/app/quick_shell/QuickShellNativeSurfaceHost.h src/app/quick_shell/QuickShellNativeSurfaceHost.cpp \
       src/app/quick_shell/QuickShellStyleBridge.h src/app/quick_shell/QuickShellStyleBridge.cpp \
       src/app/quick_shell/QuickShellMacSurfaceSupport.h src/app/quick_shell/QuickShellMacSurfaceSupport.mm
```

- [ ] **Step 2: 从 CMakeLists.txt 移除对应条目**

删除第 138–139、141–144、149–150 行附近这几条（保留 `QuickShellContracts.h`、`QuickShellPreviewCompositeSurface.*`、`QuickShellPreviewSurfacePolicy.h`、`QuickShellController.*`）：

```
    src/app/quick_shell/QuickShellBootstrap.h
    src/app/quick_shell/QuickShellBootstrap.cpp
    src/app/quick_shell/QuickShellNativeSurfaceHost.h
    src/app/quick_shell/QuickShellNativeSurfaceHost.cpp
    src/app/quick_shell/QuickShellMacSurfaceSupport.h
    src/app/quick_shell/QuickShellStyleBridge.h
    src/app/quick_shell/QuickShellStyleBridge.cpp
```

以及第 966 行附近 macOS 专属列表中的 `src/app/quick_shell/QuickShellMacSurfaceSupport.mm`。

- [ ] **Step 3: 从控制器移除 surfaceHost 参数**

在 `QuickShellController.h`：删除前置声明 `class QuickShellNativeSurfaceHost;`（第 11 行）、构造函数中的 `QuickShellNativeSurfaceHost* surfaceHost,` 参数（第 74 行）、成员 `QuickShellNativeSurfaceHost* surfaceHost_ = nullptr;`（第 188 行）。

在 `QuickShellController.cpp`：删除构造函数初始化列表里的 `, surfaceHost_(surfaceHost)`（第 344 行），并删除全部 `surfaceHost_` 分支。处理规则始终一致——**保留 `surfaceHost_ == nullptr` 时会走的那一侧，删除另一侧**。

用这条命令逐处定位（改前 29 处，改完应为 0）：

```bash
grep -n "surfaceHost_" src/app/quick_shell/QuickShellController.cpp
```

三种形态各举一例。

**形态 A —— 提前返回型**（第 1128 行附近）。整个函数体在 v2 下就是空操作，因此函数**整体删除**，并从头文件删除其声明；再删除 QML 侧对它的调用（`grep -rn "syncTopChromeSurfaceSize" src/app/qml_ui/`，若无命中则只删 C++ 侧）：

```cpp
void QuickShellController::syncTopChromeSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncTopChromeSurfaceSize(width, height);
    // ...
}
```

**形态 B —— 三元取值型**（第 448、541、546 行等）。`surfaceHost_` 恒为空，所以整个表达式恒为 `nullptr`：

```cpp
// 改前
return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().previewCompositeWindow : nullptr;
// 改后：该 getter 恒返回 nullptr，连同其调用方一并删除；
// 若调用方只是把结果与 nullptr 比较，则删除整个分支。
```

**形态 C —— 条件语句内的副作用**（如 `if (surfaceHost_ != nullptr) { surfaceHost_->showBottomTabsSpeedToast(...); }`）。直接删除整个 `if` 块。

每删一处后重新构建，编译器会指出因此变成未使用的成员函数与包含。

- [ ] **Step 4: 更新 v2 的构造调用**

在 `src/app/qml_ui/QmlUiBootstrap.cpp:87-91` 附近，把三参数构造改为两参数，并删除上方那条解释 `surfaceHost_` 空指针的注释（它描述的机制已不存在）。

- [ ] **Step 5: 构建**

```bash
cmake -S . -B build-macos-spec && cmake --build build-macos-spec --target MiaCode --parallel 4
```

预期：编译通过。

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "refactor(shell): delete the v1 bootstrap and native surface re-hosting"
```

---

## Task 4: 删除 v1 外壳 QML

**Files:**
- Delete: `src/app/quick_shell/qml/`、`resources/quick_shell_qml.qrc`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 删除**

```bash
git rm -r src/app/quick_shell/qml resources/quick_shell_qml.qrc
```

- [ ] **Step 2: 从 CMakeLists.txt 删除 qrc 条目**

删除第 618 行附近的 `resources/quick_shell_qml.qrc`。

- [ ] **Step 3: 构建**

```bash
cmake -S . -B build-macos-spec && cmake --build build-macos-spec --target MiaCode --parallel 4
```

预期：编译通过。若报缺少 qrc 资源，说明仍有代码用 `qrc:/quick_shell/...` 路径，用 `grep -rn "quick_shell" src/ --include=*.cpp` 定位后一并清理。

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "refactor(shell): delete the v1 shell QML"
```

---

## Task 5: 同步文档与漂移守卫

`MIACODE_UI_SKIN` 不再被代码读取，而 `docs/ops/DEBUG_INDEX.md` 仍列着它——`debug_flag_index_spec` 会因此失败。这是设计好的闸门，不是意外。

**Files:**
- Modify: `docs/ops/DEBUG_INDEX.md`、`.claude/skills/miacode-dev-guide/references/debug-and-logging.md`、`.claude/skills/miacode-dev-guide/references/architecture-and-layout.md`、`docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md`

- [ ] **Step 1: 确认守卫已变红**

```bash
cmake --build build-macos-spec --target debug_flag_index_spec --parallel 4 && ./build-macos-spec/debug_flag_index_spec
```

预期：失败，报 `MIACODE_UI_SKIN` 在文档中但代码已不再读取。

- [ ] **Step 2: 从 DEBUG_INDEX.md 删除该条**

删除 `## Misc / Platform` 一节中这两行：

```
- `MIACODE_UI_SKIN` — select the UI shell. Default is the QML v2 shell; set to `v1` (or pass
  `--ui=v1`) to launch the QuickShell v1 shell instead (`app/main.cpp`, `resolveUiSkin()`).
```

- [ ] **Step 3: 更新仓库指南**

在 `debug-and-logging.md` 中删除 `### UI shell selection` 一节里关于 `MIACODE_UI_SKIN` 的段落，保留 `MIACODE_QML_SPEC_IMPORT_ROOT` 的说明。

在 `architecture-and-layout.md` 中，把 `src/app/quick_shell/` 的描述改为：

```
- QuickShell 遗留：`src/app/quick_shell/` 仅剩 `QuickShellController` 与预览表面策略，v2 仍在使用；
  v1 外壳、原生表面再宿主与主题桥已于 2026-08-25 删除，`--ui=v1` 入口不再存在。控制器本身按
  `docs/specs/ui/QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md` 阶段 2 退役。
```

- [ ] **Step 4: 更新一阶段 TODO 的范围声明**

在 `docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md` 的「范围与契约」一节，删除这两条已不成立的条目：

```
- v1 / v2 共存，保留 QuickShell 再宿主路径：`NativeSurfaceHost`、`StyleBridge`、`QuickShellMain.qml`。
```
```
- v1 入口：`--ui=v1` / `MIACODE_UI_SKIN=v1` → QuickShell
```

- [ ] **Step 5: 确认守卫转绿，且结构契约通过**

```bash
cmake --build build-macos-spec --target debug_flag_index_spec v1_shell_removal_spec --parallel 4
./build-macos-spec/debug_flag_index_spec && ./build-macos-spec/v1_shell_removal_spec
```

预期：两者都退出码 0，`V1ShellRemoval spec passed.`

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "docs(v2): drop the v1 shell entry from the flag index and guide"
```

---

## Task 6: 全量验证

- [ ] **Step 1: Release 构建**

```bash
cmake --build build-macos-spec --target MiaCode --parallel 4
```

预期：`[100%] Built target MiaCode`，无 error。

- [ ] **Step 2: 全量定向 CTest**

```bash
ctest --test-dir build-macos-spec -C Release --output-on-failure
```

预期：全部通过。若有目标未构建（`Not Run`），先 `cmake --build build-macos-spec --parallel 4` 再重跑。

- [ ] **Step 3: 启动一次，确认 v2 正常**

```bash
./build-macos-spec/MiaCode.app/Contents/MacOS/MiaCode --debug
```

预期：v2 界面正常启动；打开一个谱面，编辑器、时间轴、预览均工作。传 `--ui=v1` 不再有任何特殊行为（作为普通未知参数被忽略）。

- [ ] **Step 4: 记录阶段完成**

在 `docs/specs/ui/QML_UI_V2_ARCHITECTURE_DESIGN_ZH.md` 第 8 节的阶段 0 行后追加一行完成标记，写明实际删除行数（用 `git diff --stat` 的净删除数）。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "docs(v2): record stage 0a completion"
```

---

## 完成判据

1. `v1_shell_removal_spec` 通过。
2. `debug_flag_index_spec` 通过（`MIACODE_UI_SKIN` 已从代码与文档双双消失）。
3. Release `MiaCode` 构建通过，全量 CTest 通过。
4. v2 桌面启动正常。
5. `QuickShellController` 仍在，且不再有任何 `surfaceHost_` 分支。

## 不在本计划范围内

- `QuickShellController` 本身的退役（阶段 2）。
- 扩展宿主删除、被舍弃的三组页面删除——它们是阶段 0 的另外两个独立单元，各自单独成计划。
- 任何 `ChartWorkspace` 相关工作（阶段 1）。
