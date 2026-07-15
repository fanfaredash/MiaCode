# MiaCode Extensions / MiaCode 扩展

MiaCode extensions are local folders that add commands, editor tools, UI panels, preview helpers, diagnostics, timeline controls, and optional experimental host access.

MiaCode 扩展是放在本地的功能文件夹，可以为 MiaCode 增加命令、编辑器工具、界面面板、预览辅助、诊断、时间轴控制，以及可选的实验性宿主访问能力。

**Summary / 总结:** One extension is one folder; declare permissions in the manifest and call `miacode.*` from JavaScript. / 一个扩展就是一个文件夹；在 manifest 里声明权限，然后从 JavaScript 调用 `miacode.*`。

---

## 1. Install / 安装

Copy the extension folder into:

将扩展文件夹复制到：

```text
<MiaCode install root>\extensions
```

Open **Preferences > Extensions**, then click **Refresh Extensions**. You can enable, disable, inspect, or open logs from the same page.

打开 **首选项 > 扩展**，点击 **刷新扩展**。你可以在同一页启用、禁用、检查扩展，或打开日志目录。

**Summary / 总结:** Users only need the MiaCode app and an extension folder placed under `extensions`. / 普通用户只需要 MiaCode 程序和放进 `extensions` 的扩展文件夹。

---

## 2. Folder Layout / 目录结构

```text
hello-tools/
  miacode-extension.json
  extension.js
  assets/
    pet-1.png
    pet-2.png
```

Use `miacode-extension.json` for MiaCode-only extensions. `extension.js` is required only when the extension runs JavaScript. Assets loaded by UI APIs must stay inside the extension folder.

推荐使用 `miacode-extension.json`。只有需要运行 JavaScript 时才需要 `extension.js`。UI API 加载的资源必须位于扩展目录内。

**Summary / 总结:** Keep code, manifest, and assets together; MiaCode does not load files outside the extension folder for controlled UI resources. / 代码、manifest 和资源放在一起；受控 UI 资源不会加载扩展目录外的文件。

---

## 3. Manifest / 清单文件

```json
{
  "id": "hello-tools",
  "name": "Hello Tools",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "main": "./extension.js",
  "activationEvents": ["onStartupFinished"],
  "permissions": ["ui.message", "workspace.read"],
  "contributes": {
    "commands": [
      {
        "command": "hello-tools.say",
        "title": "Say Hello",
        "category": "Hello Tools"
      }
    ],
    "menus": {
      "tools/menu": [{ "command": "hello-tools.say" }]
    }
  }
}
```

Required fields are `id`, `name`, `version`, `publisher`, and `engines.miacode`. Add `main` only when JavaScript should run. Add every permission your code calls.

必填字段是 `id`、`name`、`version`、`publisher` 和 `engines.miacode`。只有需要运行 JavaScript 时才添加 `main`。代码调用什么能力，就声明对应权限。

**Summary / 总结:** The manifest is the contract for identity, entry file, commands, menus, and permissions. / manifest 是身份、入口文件、命令、菜单和权限的契约。

---

## 4. Runtime / 运行时

MiaCode uses Qt `QJSEngine`, not Node.js. There is no `require`, npm package loading, `fs`, `path`, `process`, `Buffer`, or `child_process`. Use `miacode.*` APIs instead.

MiaCode 使用 Qt `QJSEngine`，不是 Node.js。不能使用 `require`、npm 包加载、`fs`、`path`、`process`、`Buffer` 或 `child_process`。请使用 `miacode.*` API。

```js
function activate(context) {
  context.log("hello-tools activated");

  miacode.commands.registerCommand("hello-tools.say", () => {
    const doc = miacode.workspace.getActiveDocument();
    miacode.window.showInformationMessage(
      `Active document has ${doc.text.length} characters.`
    );
  });
}

function deactivate() {}

module.exports = { activate, deactivate };
```

**Summary / 总结:** Write browser-like JavaScript against the MiaCode facade; do not depend on Node APIs. / 按浏览器式 JavaScript 编写扩展，依赖 MiaCode facade，不依赖 Node API。

---

## 5. Common APIs / 常用 API

| Area / 范围 | APIs / API |
| --- | --- |
| Commands / 命令 | `miacode.commands.registerCommand`, `executeCommand`, `getCommands`, `getInternalCommands`, `executeInternal` |
| Window / 窗口 | `showInformationMessage`, `showWarningMessage`, `showErrorMessage`, `showInputBox`, `showQuickPick`, `createStatusBarItem` |
| Workspace / 工作区 | `getActiveDocument`, `applyDocumentEdit`, `getChartMetadata`, `updateChartMetadata`, `save`, `saveAs` |
| Document / 文档 | `query`, `edit`, `getDifficulties`, `getActiveDifficulty`, `replaceActiveDifficultyText`, `format` |
| Editor / 编辑器 | `getText`, `getSelection`, `getVisibleRange`, `revealRange`, `getParsedSnapshot`, `getCurrentToken`, `insertText`, `replaceSelection`, `replaceRange`, `undo`, `redo`, `showHover`, `showCompletions`, `showCodeActions` |
| Providers / 提供器 | `miacode.providers.registerHoverProvider`, `registerCompletionProvider`, `registerCodeActionProvider`, `collectHover`, `collectCompletions`, `collectCodeActions`, `showHover`, `showCompletions`, `showCodeActions` |
| Timeline / 时间轴 | `getSnapshot`, `getZoomState`, `getVisibleRange`, `getMarkersAtSecond`, `seek`, `zoomIn`, `zoomOut`, `stepZoomPreset`, `setZoomScale`, `scrollToSecond`, `setFollowPreview`, `setFollowProgress` |
| Preview / 预览 | `getState`, `getRenderState`, `play`, `pause`, `stop`, `seek`, `setSpeed`, `addOverlay`, `clearOverlays` |
| Input / 输入 | `miacode.input.registerWheelGesture`, `registerKeyGesture`, `registerMouseGesture`, `getGestures` |
| Shortcuts / 快捷键 | `getEditableShortcuts`, `getKeybinding`, `registerShortcut`, `registerCommandShortcut` |
| UI / 界面 | `registerToolbarButton`, `registerBottomTabView`, `registerSidebarView`, `registerPreferencesPage`, `registerFloatingPanel`, `registerPetOverlay`, `registerSceneOverlay`, `renderWebView`, `renderCanvasView`, `getViews` |
| Diagnostics / 诊断 | `miacode.validation.run`, `addDiagnostics`, `clearDiagnostics`, `miacode.diagnostics.validateDocument` |
| Logs / 日志 | `miacode.logs.append`, `getPath`, `open` |
| Open Bridge / 开放桥 | `miacode.open.list`, `describe`, `call` |

For the full live list, call `miacode.api.list()` or open **Preferences > Extensions > DevTools Panel**.

完整实时列表可以调用 `miacode.api.list()`，或打开 **首选项 > 扩展 > DevTools 面板**。

**Summary / 总结:** Prefer stable `miacode.*` facade APIs; use DevTools to inspect the exact current capability surface. / 优先使用稳定的 `miacode.*` facade；用 DevTools 查看当前真实能力面。

---

## 6. Permissions / 权限

If an API requires a permission, the extension manifest must declare it. Missing permission means the call fails. Risk labels such as `low`, `medium`, and `high` are descriptions, not extra switches.

如果 API 需要权限，扩展 manifest 必须声明。缺少权限时调用会失败。`low`、`medium`、`high` 是风险说明，不是额外开关。

Common permissions include:

常用权限包括：

```json
{
  "permissions": [
    "ui.message",
    "ui.contribute",
    "input.listen",
    "providers.read",
    "providers.register",
    "workspace.read",
    "document.edit",
    "editor.read",
    "editor.edit",
    "timeline.read",
    "timeline.control",
    "preview.control",
    "settings.read",
    "settings.write",
    "open.inspect",
    "open.call"
  ]
}
```

Experimental raw APIs require explicit raw or experimental permissions, such as `experimental.invoke`, `renderer.raw`, `internal.raw`, `shell.execute`, or `process.manage`.

实验性 raw API 需要显式声明 raw 或 experimental 权限，例如 `experimental.invoke`、`renderer.raw`、`internal.raw`、`shell.execute` 或 `process.manage`。

**Summary / 总结:** Permissions are declared up front; installing an extension is the trust decision. / 权限提前声明；安装扩展本身就是信任决策。

---

## 7. Real Interaction / 真实交互

Events and providers are not only descriptors. Event callbacks are stored in the runtime and triggered by host actions such as text edits, timeline seek, and preview state changes. Provider descriptors can be collected by the host through provider broker APIs.

事件和 provider 不只是描述符。事件 callback 会保存在运行时，并由文本编辑、时间轴跳转、预览状态变化等宿主动作触发。provider 描述符可以通过 provider broker API 被宿主收集和消费。

```js
function activate(context) {
  context.subscriptions.push(
    miacode.events.onDidChangeText((event) => {
      context.log(`changed by ${event.sourceMethod}`);
    })
  );

  miacode.editor.registerHoverProvider({
    id: "hello-hover",
    pattern: "tap",
    markdown: "Tap note helper"
  });

  miacode.providers.showHover({});
}
```

Completion and code-action providers can be collected programmatically or shown through host UI with `miacode.providers.showCompletions()` and `miacode.providers.showCodeActions()`. In the editor, Ctrl+Space opens registered completions and Ctrl+. or Alt+Enter opens registered code actions.

Completion 和 code action 既可以通过 `collectCompletions`、`collectCodeActions` 程序化收集，也可以通过 `miacode.providers.showCompletions()`、`miacode.providers.showCodeActions()` 交给宿主 UI 展示。在编辑器里，Ctrl+Space 会打开已注册补全，Ctrl+. 或 Alt+Enter 会打开已注册 code action。

**Summary / 总结:** Registration creates runtime state that the host can trigger or collect; it is no longer only a planned placeholder. / 注册会产生宿主可触发、可收集的运行时状态，不再只是 planned 占位。

---

## 8. Input And Shortcuts / 输入与快捷键

Use controlled input gestures when an extension needs host-level input such as Ctrl+wheel. A gesture invokes an extension command.

当扩展需要 Ctrl+滚轮这类宿主输入时，使用受控 input gesture。gesture 会触发一个扩展命令。

```js
function activate(context) {
  miacode.commands.registerCommand("hello-tools.zoomIn", () => {
    miacode.timeline.zoomIn();
  });

  miacode.input.registerWheelGesture({
    id: "hello-tools.ctrlWheelUp",
    target: "timeline",
    modifiers: ["ctrl"],
    direction: "up",
    command: "hello-tools.zoomIn"
  });

  miacode.input.registerKeyGesture({
    id: "hello-tools.ctrlSpace",
    target: "editor",
    modifiers: ["ctrl"],
    key: "Space",
    phase: "press",
    command: "hello-tools.zoomIn"
  });

  miacode.input.registerMouseGesture({
    id: "hello-tools.previewClick",
    target: "preview",
    button: "left",
    phase: "press",
    command: "hello-tools.zoomIn"
  });

  miacode.shortcuts.registerCommandShortcut({
    command: "hello-tools.zoomIn",
    label: "Timeline Zoom In",
    keybinding: "Ctrl+="
  });
}
```

Shortcuts registered this way appear in **Preferences > Shortcuts** under the extension category and can be edited by the user.

这样注册的快捷键会出现在 **首选项 > 快捷键** 的扩展分类中，用户可以修改。

**Summary / 总结:** Use `input.registerWheelGesture`, `registerKeyGesture`, and `registerMouseGesture` for host gestures; use `shortcuts.registerCommandShortcut` for editable user shortcuts. / 用 `input.registerWheelGesture`、`registerKeyGesture`、`registerMouseGesture` 接宿主手势，用 `shortcuts.registerCommandShortcut` 注册可编辑快捷键。

---

## 9. Controlled UI / 受控 UI

Extensions submit declarative UI data. MiaCode owns rendering, lifetime, resource checks, and event dispatch. This avoids exposing raw renderer, raw `QWidget`, or raw `QQuickItem` objects to extension code.

扩展提交声明式 UI 数据。MiaCode 负责渲染、生命周期、资源检查和事件分发。这样无需把 raw renderer、raw `QWidget` 或 raw `QQuickItem` 暴露给扩展代码。

```js
miacode.ui.registerFloatingPanel({
  id: "hello-tools.panel",
  title: "Hello Tools",
  width: 360,
  height: 240,
  body: [
    { type: "text", text: "Hello from an extension panel." },
    { type: "button", text: "Run", command: "hello-tools.say" }
  ]
});

miacode.ui.registerPetOverlay({
  id: "hello-tools.pet",
  frames: ["assets/pet-1.png", "assets/pet-2.png"],
  position: { anchor: "bottomRight", x: 16, y: 16 },
  draggable: true,
  opacity: 0.92,
  onClickCommand: "hello-tools.say"
});

miacode.ui.renderWebView({
  id: "hello-tools.web",
  title: "HTML Lite",
  html: "<h3>Hello Tools</h3><p>Static extension HTML.</p>"
});

miacode.ui.renderCanvasView({
  id: "hello-tools.canvas",
  title: "Canvas",
  type: "canvas",
  nodes: [
    { type: "rect", x: 12, y: 12, width: 120, height: 56, color: "#2f80ed" },
    { type: "text", x: 24, y: 46, text: "Canvas view", color: "#ffffff" }
  ]
});

miacode.ui.registerSceneOverlay({
  id: "hello-tools.scene",
  width: 220,
  height: 120,
  nodes: [{ type: "text", x: 16, y: 48, text: "Preview scene overlay" }]
});
```

Supported controlled hosts include toolbar buttons, bottom tab views, real sidebar dock views, modeless preferences pages, floating panels, preview overlays, pet overlays, HTML-lite views, declarative canvas views, and preview scene overlays. HTML-lite is rendered by the host without exposing raw WebEngine, raw renderer, raw `QWidget`, or raw `QQuickItem`.

受控宿主包括工具栏按钮、底部标签页、真正的侧边栏 dock 视图、独立首选项页、浮动面板、预览浮层、pet overlay、HTML-lite 视图、声明式 canvas 视图和预览 scene overlay。HTML-lite 由宿主渲染，不会向扩展暴露 raw WebEngine、raw renderer、raw `QWidget` 或 raw `QQuickItem`。

**Summary / 总结:** UI is open through a facade, not by handing internal renderer objects to JavaScript. / UI 通过 facade 开放，而不是把内部 renderer 对象交给 JavaScript。

---

## 10. Open Bridge And Experimental Raw / Open Bridge 与实验 raw

Open Bridge exposes registered facade objects through `miacode.open`. These objects are MiaCode-controlled bridge objects, not real internal C++ instances.

Open Bridge 通过 `miacode.open` 暴露已注册的 facade 对象。这些对象是 MiaCode 控制的桥对象，不是真实内部 C++ 实例。

```js
const timeline = miacode.open.describe("timeline");
const zoom = miacode.open.call("timeline", "getZoomState");
```

Experimental raw targets are marked as `experimentalRaw: true`. The marker is a warning, not an automatic block. Calls still require explicit permissions and may return an accepted diagnostic result instead of a stable native binding.

实验 raw 目标会标记 `experimentalRaw: true`。这个标记是风险提示，不是自动 block。调用仍需要显式权限，并且可能返回 accepted diagnostic，而不是稳定原生绑定。

**Summary / 总结:** Open Bridge is for controlled internal-facing facades; raw access is explicit, unstable, and diagnostic-friendly. / Open Bridge 面向受控内部 facade；raw 访问是显式、非稳定、便于诊断的能力。

---

## 11. Logs And DevTools / 日志与 DevTools

Extension errors, permission failures, runtime exceptions, API call history, UI contributions, provider registrations, event callbacks, experimental raw calls, and diagnostics can be inspected from **Preferences > Extensions**.

扩展错误、权限失败、运行时异常、API 调用历史、UI 贡献、provider 注册、事件 callback、实验 raw 调用和诊断信息，都可以在 **首选项 > 扩展** 查看。

Use **Open Logs Folder** when reporting a bug. Use **DevTools Panel** when checking whether an API exists, which permission it needs, and whether recent calls succeeded.

反馈问题时使用 **打开日志目录**。检查 API 是否存在、需要什么权限、最近调用是否成功时，使用 **DevTools 面板**。

**Summary / 总结:** Logs are for bug reports; DevTools is for capability and runtime inspection. / 日志用于报错反馈；DevTools 用于检查能力面和运行时状态。
