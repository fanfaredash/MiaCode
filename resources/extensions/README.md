# MiaCode Extensions / MiaCode 扩展

MiaCode extensions are local folders that add commands, UI contributions, document tools, preview helpers, diagnostics, and optional experimental host access to MiaCode.

MiaCode 扩展是放在本地文件夹中的插件，可以为 MiaCode 增加命令、界面入口、谱面工具、预览辅助、诊断能力，以及可选的实验性宿主访问能力。

**Summary / 总结:** Put one extension in one folder, declare what it needs in the manifest, then use `miacode.*` APIs from JavaScript.

---

## 1. Install And Manage / 安装与管理

Copy each extension folder into:

将每个扩展文件夹复制到：

```text
<MiaCode install root>\extensions
```

Then open **Preferences > Extensions** and click **Refresh Extensions**. You can enable or disable each extension from the same page. Logs are available from **Open Logs Folder**.

然后打开 **首选项 > 扩展**，点击 **刷新扩展**。你可以在同一页面启用或禁用扩展，也可以通过 **打开日志位置** 查看日志。

**Summary / 总结:** Install by copying a folder; manage it from **Preferences > Extensions**.

---

## 2. Extension Folder / 扩展文件夹

A typical extension looks like this:

一个典型扩展目录如下：

```text
hello-tools/
  miacode-extension.json
  extension.js
  assets/
    pet-1.png
    pet-2.png
```

Use `miacode-extension.json` for MiaCode-only extensions. You may also use `package.json` with a `miacodeExtension` object if you prefer an npm-style folder, but MiaCode does not run Node.js.

推荐使用 `miacode-extension.json`。如果你更喜欢 npm 风格目录，也可以使用 `package.json` 并在其中放 `miacodeExtension` 对象，但 MiaCode 不会运行 Node.js。

**Summary / 总结:** The manifest describes the extension; `extension.js` is optional unless you need JavaScript.

---

## 3. Manifest / 清单文件

Minimum JavaScript extension:

最小 JavaScript 扩展：

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
      "tools/menu": [
        { "command": "hello-tools.say" }
      ]
    }
  }
}
```

Required fields are `id`, `name`, `version`, `publisher`, and `engines.miacode`. Add `main` only when the extension runs JavaScript. Add `permissions` for every capability your code calls.

必填字段是 `id`、`name`、`version`、`publisher` 和 `engines.miacode`。只有需要运行 JavaScript 时才需要 `main`。代码调用什么能力，就在 `permissions` 中声明对应权限。

Data-only language pack:

纯数据语言包：

```json
{
  "id": "sample-language",
  "name": "Sample Language",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "contributes": {
    "languages": [
      {
        "id": "sample",
        "label": "Sample",
        "translations": "./i18n/sample.json"
      }
    ]
  }
}
```

**Summary / 总结:** The manifest is the contract: identity, entry file, contributions, and permissions.

---

## 4. JavaScript Runtime / JavaScript 运行环境

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

Register disposables in `context.subscriptions` when an API returns one, especially event listeners.

当 API 返回 disposable 时，尤其是事件监听器，请放进 `context.subscriptions`，方便扩展停用时自动清理。

**Summary / 总结:** Write browser-like JavaScript against `miacode.*`; do not expect Node.js APIs.

---

## 5. Common APIs / 常用 API

Most extensions should use the stable facade methods below. These are safer and easier than raw access.

大多数扩展应该优先使用下面这些稳定 facade 方法。它们比 raw 访问更安全，也更容易维护。

| Area / 范围 | Common APIs / 常用 API |
| --- | --- |
| Commands / 命令 | `miacode.commands.registerCommand`, `executeCommand`, `getCommands` |
| Window / 窗口 | `miacode.window.showInformationMessage`, `showWarningMessage`, `showErrorMessage`, `showInputBox`, `showQuickPick`, `createStatusBarItem` |
| Workspace / 工作区 | `miacode.workspace.getActiveDocument`, `applyDocumentEdit`, `getChartMetadata`, `updateChartMetadata`, `save`, `saveAs` |
| Document / 文档 | `miacode.document.query`, `edit`, `getDifficulties`, `getActiveDifficulty`, `replaceActiveDifficultyText`, `getParsedNoteMarkers`, `format` |
| Editor / 编辑器 | `miacode.editor.getSelection`, `insertText`, `replaceSelection`, `replaceRange`, `undo`, `redo`, `copy`, `paste` |
| Timeline / 时间线 | `miacode.timeline.getSnapshot`, `getCurrentSecond`, `seek`, `addMarker`, `clearMarkers`, `addBand`, `clearVisuals` |
| Preview / 预览 | `miacode.preview.getState`, `play`, `pause`, `stop`, `seek`, `setSpeed`, `addOverlay`, `clearOverlays` |
| UI / 界面 | `miacode.ui.registerPetOverlay`, `registerToolbarButton`, `registerSidebarView`, `registerPreferencesPage`, `getContributions` |
| Diagnostics / 诊断 | `miacode.validation.run`, `miacode.validation.addDiagnostics`, `miacode.diagnostics.validateDocument` |
| Logs / 日志 | `miacode.logs.append`, `getPath`, `open` |
| Management / 管理 | `miacode.extensions.all`, `get`, `enable`, `disable`, `installFromFolder`, `remove` |

For the full live list, call `miacode.api.list()` or open **Preferences > Extensions > DevTools Panel**.

完整实时列表可以通过 `miacode.api.list()` 查看，也可以打开 **首选项 > 扩展 > DevTools 面板**。

**Summary / 总结:** Use stable `miacode.*` APIs first; use DevTools when you need the full current surface.

---

## 6. Permissions / 权限

If an API requires a permission, the manifest must declare it. Missing permissions cause the call to fail. Risk labels such as `low`, `medium`, and `high` are explanations, not separate allow/deny switches.

如果 API 需要权限，manifest 必须声明。缺少权限时调用会失败。`low`、`medium`、`high` 这类风险标签只是说明影响范围，不是额外的允许/拒绝开关。

Common examples:

常见示例：

```json
{
  "permissions": [
    "ui.message",
    "workspace.read",
    "document.edit",
    "preview.control",
    "ui.contribute",
    "open.inspect"
  ]
}
```

Experimental raw APIs require explicit raw or experimental permissions, such as `open.call`, `experimental.invoke`, `renderer.raw`, `internal.raw`, `shell.execute`, or `process.manage`.

实验性 raw API 需要显式 raw 或 experimental 权限，例如 `open.call`、`experimental.invoke`、`renderer.raw`、`internal.raw`、`shell.execute` 或 `process.manage`。

**Summary / 总结:** Permissions are declared up front in the manifest; installing an extension is the trust decision.

---

## 7. Events / 事件

Events are real callbacks stored in the extension runtime. They are not placeholder descriptors.

事件是真实保存在扩展运行时中的 callback，不是只写在文档里的占位描述。

```js
function activate(context) {
  context.subscriptions.push(
    miacode.events.onDidChangeText((event) => {
      context.log(`changed by ${event.sourceMethod}: ${event.textLength}`);
    }),
    miacode.timeline.onDidSeek((event) => {
      context.log(`seeked to ${event.second}`);
    }),
    miacode.preview.onDidChangeState((event) => {
      context.log(`preview changed by ${event.sourceMethod}`);
    })
  );
}
```

Callbacks run as the extension that registered them. Any API call inside the callback still uses that extension's manifest permissions.

callback 会以注册它的扩展身份运行。callback 内再次调用 API 时，仍然按该扩展 manifest 中的权限声明校验。

**Summary / 总结:** Events are interactive and permission-checked; remember to dispose them through `context.subscriptions`.

---

## 8. Controlled UI / 受控 UI

Use controlled UI APIs when you need visual features. Do not ask for raw renderer, raw QWidget, or raw QML objects for normal extensions.

需要可视化能力时，请使用受控 UI API。普通扩展不应该依赖 raw renderer、raw QWidget 或 raw QML 对象。

Example pet overlay:

Pet 浮层示例：

```js
miacode.ui.registerPetOverlay({
  id: "sample-pet",
  frames: ["assets/pet-1.png", "assets/pet-2.png"],
  sprite: { fps: 6 },
  anchor: "bottomRight",
  size: 96,
  opacity: 0.92,
  draggable: true,
  onClickCommand: "sample.petClicked"
});
```

Supported fields include `image`/`src`, `frames`, `sprite.fps`, `sprite.frameDurationMs`, `position`, `anchor`, `width`, `height`, `size`, `margin`, `opacity`, `draggable`, `onClickCommand`, and `onDragEndCommand`.

支持字段包括 `image`/`src`、`frames`、`sprite.fps`、`sprite.frameDurationMs`、`position`、`anchor`、`width`、`height`、`size`、`margin`、`opacity`、`draggable`、`onClickCommand` 和 `onDragEndCommand`。

Image and sprite paths must resolve inside the extension folder. MiaCode rejects missing files, directories, and path traversal.

图片和 sprite 路径必须位于扩展目录内。MiaCode 会拒绝不存在的文件、目录路径和路径穿越。

**Summary / 总结:** Use declarative UI data; MiaCode owns the actual rendering and resource safety checks.

---

## 9. Open Bridge / 开放桥

`miacode.open` exposes controlled internal facade objects. It is more flexible than stable wrapper methods, but it still does not pass raw C++ or Qt objects into JavaScript.

`miacode.open` 暴露的是受控的内部 facade 对象。它比稳定包装 API 更灵活，但仍然不会把真实 C++ 或 Qt 对象直接交给 JavaScript。

```js
const objects = miacode.open.list().value;
const preview = miacode.open.describe("preview").value;
const state = miacode.open.call("preview", "getState").value;

miacode.open.call("document", "edit", {
  ops: [{ path: "/metadata/title", value: "New Title" }]
});
```

Use `miacode.open.describe(objectId)` before calling. A method must report `status: "implemented"` to be callable. Discovery uses `open.inspect`; calling methods uses `open.call` plus any object or method permission.

调用前先用 `miacode.open.describe(objectId)` 查看描述。只有 `status: "implemented"` 的方法可以调用。发现对象需要 `open.inspect`；调用方法需要 `open.call`，并可能需要对象或方法自身的权限。

**Summary / 总结:** Open Bridge is dynamic and discoverable, but still permission-checked and host-owned.

---

## 10. Experimental Raw / 实验性 Raw

Experimental raw targets are visible for local and trusted development. They are marked as unstable and may change without compatibility guarantees.

实验性 raw 目标用于本地和可信开发。它们会被明确标记为不稳定，并且不保证兼容性。

Common raw namespaces:

常见 raw 命名空间：

```text
shell.execute
process.spawn
native.*
internal.raw
renderer.raw
export.raw
security.*
updates.*
```

Discover them through Open Bridge:

通过 Open Bridge 发现它们：

```js
const rawTargets = miacode.open.list().value.filter((item) => item.experimentalRaw);
for (const target of rawTargets) {
  context.log(`${target.id}: ${target.stability}`);
}
```

An experimental marker is a warning, not automatically a block. Calls still require the declared permissions and may return an accepted diagnostic result instead of a stable native binding.

实验性标记是风险提示，不等于自动拒绝。调用仍然需要声明权限，并且可能返回“已接收”的诊断结果，而不是稳定的 native 绑定。

**Summary / 总结:** Raw access is open for trusted experiments, but unstable; prefer stable APIs for distributable extensions.

---

## 11. DevTools / 诊断工具

Use DevTools when an extension does not behave as expected.

扩展行为不符合预期时，请使用 DevTools 诊断。

In JavaScript:

在 JavaScript 中：

```js
const snapshot = miacode.devtools.snapshot().value;
const diagnosis = miacode.devtools.diagnose("preview.seek").value;
const recentCalls = miacode.devtools.recentCalls().value;
```

In the app, open **Preferences > Extensions > DevTools Panel**. The panel shows API Registry, Open Bridge, Recent Calls, Extensions, UI Contributions, Diagnostics, and Raw JSON.

在应用内，打开 **首选项 > 扩展 > DevTools 面板**。面板会展示 API 注册表、Open Bridge、最近调用、扩展、UI 贡献、诊断和原始 JSON。

`recentCalls` keeps a compact parameter preview only. It does not store raw native pointers or full large payloads.

`recentCalls` 只保存压缩后的参数预览，不保存 raw native pointer，也不会保存完整的大型 payload。

**Summary / 总结:** DevTools is read-only inspection; use it to see what exists, what was called, and why a call failed.

---

## 12. Validate Before Shipping / 发布前校验

Run the validator before installing or sharing an extension:

安装或分享扩展前，请运行校验器：

```powershell
node tools/extensions/validate-extension.mjs path\to\extension
```

Then test these flows:

然后测试这些流程：

1. Copy the extension folder into `<MiaCode install root>\extensions`.
2. Refresh **Preferences > Extensions**.
3. Enable and disable the extension.
4. Run its commands.
5. Open logs and DevTools if anything fails.

中文流程：

1. 将扩展文件夹复制到 `<MiaCode install root>\extensions`。
2. 在 **首选项 > 扩展** 中刷新。
3. 启用和禁用扩展。
4. 执行扩展命令。
5. 如果失败，查看日志和 DevTools。

Refreshing extensions should not change the current chart text, dirty state, file path, or active difficulty unless the extension explicitly calls an editing API.

刷新扩展不应该改变当前谱面文本、dirty 状态、文件路径或当前难度，除非扩展明确调用了编辑 API。

**Summary / 总结:** Validate the manifest, refresh in MiaCode, test enable/disable, then inspect logs and DevTools.
