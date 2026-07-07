# MiaCode Extensions / MiaCode 扩展

MiaCode 扩展是本地扩展系统。扩展是一个普通文件夹，通过 `miacode-extension.json` 或 `package.json#miacodeExtension` 声明 manifest、贡献点、权限和可选的 JavaScript 入口。MiaCode 使用内嵌 Qt JavaScript runtime 运行 JS 扩展，不需要 Node.js。

MiaCode extensions are local packages. An extension is a normal folder that declares a manifest, contribution points, permissions, and an optional JavaScript entry through `miacode-extension.json` or `package.json#miacodeExtension`. MiaCode runs JavaScript with its embedded Qt runtime; Node.js is not required.

## Install Location / 安装位置

用户扩展放在顶层启动器旁边：

```text
<install root>\extensions
```

不要把用户扩展放进 `app\extensions`。一个扩展使用一个独立文件夹。

User extensions belong beside the top-level launcher:

```text
<install root>\extensions
```

Do not install user extensions into `app\extensions`. Use one folder per extension.

## Extension Package Structure / 扩展包结构

```text
my-extension\
  miacode-extension.json
  extension.js
  data\
  i18n\
```

`main` 只在需要 JS 时填写。纯数据扩展可以没有 JS。扩展包的所有路径都相对扩展根目录。

`main` is only needed for JavaScript extensions. Data-only extensions can omit JavaScript. All package paths are relative to the extension root.

## Manifest Format / Manifest 编写规范

```json
{
  "id": "hello-world",
  "name": "Hello World",
  "version": "0.0.1",
  "publisher": "local",
  "main": "./extension.js",
  "permissions": ["ui.message", "workspace.read"],
  "engines": { "miacode": ">=1.0.0" },
  "activationEvents": ["onStartupFinished"],
  "contributes": {
    "commands": [
      { "command": "hello-world.say-hello", "title": "Say Hello", "category": "Hello" }
    ],
    "menus": {
      "tools/menu": [
        { "command": "hello-world.say-hello" }
      ]
    }
  }
}
```

`id/name/version/publisher/engines` 是必填字段。`id`、`publisher` 和命令 id 使用小写字母、数字、`.`、`_`、`-`。`permissions` 必须声明 JS 要调用的权限；未声明权限的 API 调用会被拒绝，并显示在扩展诊断中。

`id/name/version/publisher/engines` are required. `id`, `publisher`, and command ids use lowercase letters, numbers, `.`, `_`, and `-`. `permissions` must declare the API permissions used by JavaScript; undeclared API calls are denied and reported in extension diagnostics.

## Permissions / 权限

低风险权限主要用于读取信息或显示轻量 UI：

- `app.read`, `ui.message`, `ui.status`, `workspace.read`, `editor.read`, `commands.read`
- `timeline.read`, `preview.read`, `resources.read`, `export.read`, `settings.read`
- `events.subscribe`, `logs.read`

中风险权限会影响编辑器、分析、预览控制或注册扩展能力：

- `ui.prompt`, `ui.contribute`, `providers.register`, `document.edit`, `editor.edit`
- `commands.execute`, `diagnostics.run`, `analysis.run`, `timeline.control`, `preview.control`, `tasks.run`

高风险权限会被醒目标注，首次使用时可能要求用户确认：

- `workspace.write`, `resources.write`, `filesystem.read`, `filesystem.write`
- `network.fetch`, `settings.write`, `extensions.manage`, `export.write`, `logs.write`

Low-risk permissions read state or show lightweight UI:

- `app.read`, `ui.message`, `ui.status`, `workspace.read`, `editor.read`, `commands.read`
- `timeline.read`, `preview.read`, `resources.read`, `export.read`, `settings.read`
- `events.subscribe`, `logs.read`

Medium-risk permissions affect editing, analysis, preview control, or extension registration:

- `ui.prompt`, `ui.contribute`, `providers.register`, `document.edit`, `editor.edit`
- `commands.execute`, `diagnostics.run`, `analysis.run`, `timeline.control`, `preview.control`, `tasks.run`

High-risk permissions are highlighted and may ask for first-use confirmation:

- `workspace.write`, `resources.write`, `filesystem.read`, `filesystem.write`
- `network.fetch`, `settings.write`, `extensions.manage`, `export.write`, `logs.write`

## Contribution Points / 贡献点

`commands` 声明命令 id、标题和分类。JS 使用 `miacode.commands.registerCommand` 注册回调后，命令才会真正执行。

`menus` 把命令挂载到界面。当前稳定位置是 `tools/menu`，显示在 Tools -> Extensions。

`languages` 声明界面语言和 UTF-8 JSON 翻译文件。语言包只是普通贡献点之一；禁用扩展后，该扩展贡献的命令、菜单、语言和其他能力都会消失。

`commands` declares command ids, titles, and categories. JavaScript must call `miacode.commands.registerCommand` before the command can execute.

`menus` places commands in the UI. The stable location is `tools/menu`, shown under Tools -> Extensions.

`languages` declares UI languages and UTF-8 JSON translation files. Language packs are normal contribution points; when an extension is disabled, all of its commands, menus, languages, and other contributions disappear.

## Writing JavaScript Extensions / 编写 JS 扩展

入口文件使用 CommonJS 子集：

```js
"use strict";

function activate(context) {
  context.log("activated");
  miacode.commands.registerCommand("hello-world.say-hello", () => {
    const document = miacode.workspace.getActiveDocument();
    miacode.window.showInformationMessage("Length: " + document.text.length);
  });
}

function deactivate() {}

module.exports = { activate, deactivate };
```

不支持 Node built-ins：`require`, `fs`, `path`, `process`, `Buffer`, `child_process`。需要文件、网络、设置、编辑器或内部对象能力时，使用 `miacode.*` API 并声明权限。编写扩展包时不要修改 MiaCode 本体源码，也不要要求用户重新编译 MiaCode；扩展必须只通过 manifest、贡献点、数据文件和 `miacode.*` API 实现。

The entry file uses a CommonJS subset. Node built-ins are not available: `require`, `fs`, `path`, `process`, `Buffer`, `child_process`. Use `miacode.*` APIs and declare permissions for file, network, settings, editor, or internal-object access. Do not modify MiaCode application source code and do not ask users to rebuild MiaCode when creating an extension package; extensions must work only through the manifest, contribution points, data files, and `miacode.*` APIs.

## Available API / 可用 API

```text
miacode.app.getInfo()
miacode.app.openPreferences()
miacode.app.reloadExtensions()

miacode.commands.registerCommand(command, callback)
miacode.commands.executeCommand(command, args)
miacode.commands.getCommands()

miacode.window.showInformationMessage(message)
miacode.window.showWarningMessage(message)
miacode.window.showErrorMessage(message)
miacode.window.showInputBox(options)
miacode.window.showQuickPick(items, options)
miacode.window.createStatusBarItem(options)

miacode.workspace.getActiveDocument()
miacode.workspace.applyDocumentEdit({ text })
miacode.workspace.getChartMetadata()
miacode.workspace.updateChartMetadata(patch)
miacode.workspace.getChartFolder()
miacode.workspace.getMediaFiles()
miacode.workspace.getRecentFiles()
miacode.workspace.getProjectData(key)
miacode.workspace.setProjectData(key, value)
miacode.workspace.scanChartFolders(rootPath)
miacode.workspace.save()
miacode.workspace.saveAs(path)
miacode.workspace.onDidOpenDocument(callback)
miacode.workspace.onDidSaveDocument(callback)

miacode.document.getDifficulties()
miacode.document.getActiveDifficulty()
miacode.document.setActiveDifficulty(id)
miacode.document.replaceActiveDifficultyText(text)
miacode.document.getParsedNoteMarkers()
miacode.document.getTimingMetadata()
miacode.document.applyTextEdits(edits)
miacode.document.format()
miacode.document.createDifficulty(options)
miacode.document.deleteDifficulty(id)
miacode.document.renameDifficulty(id, label)
miacode.document.onDidChangeText(callback)

miacode.editor.getSelection()
miacode.editor.getCursor()
miacode.editor.getLine(line)
miacode.editor.getCurrentLine()
miacode.editor.getCurrentToken()
miacode.editor.insertText(text)
miacode.editor.replaceSelection(text)
miacode.editor.replaceRange(range, text)
miacode.editor.setSelection(range)
miacode.editor.addDecoration(range, options)
miacode.editor.clearDecorations(ownerId)
miacode.editor.showHover(range, markdown)
miacode.editor.addGutterIcon(options)
miacode.editor.clearGutterIcons(ownerId)
miacode.editor.fold(range)
miacode.editor.unfold(range)
miacode.editor.registerHoverProvider(provider)
miacode.editor.registerCompletionProvider(provider)
miacode.editor.registerCodeActionProvider(provider)
miacode.editor.onDidChangeSelection(callback)

miacode.validation.run()
miacode.validation.getLastResult()
miacode.validation.addDiagnostics(ownerId, diagnostics)
miacode.validation.clearDiagnostics(ownerId)
miacode.diagnostics.validateDocument()

miacode.analysis.runMuriAnalysis()
miacode.analysis.getLastMuriResult()

miacode.timeline.getSnapshot()
miacode.timeline.getCurrentSecond()
miacode.timeline.seek(second)
miacode.timeline.addMarker(marker)
miacode.timeline.clearMarkers(ownerId)
miacode.timeline.addBand(band)
miacode.timeline.addVerticalLine(line)
miacode.timeline.clearVisuals(ownerId)
miacode.timeline.registerMarkerClickCommand(command)
miacode.timeline.onDidSeek(callback)

miacode.preview.play()
miacode.preview.pause()
miacode.preview.stop()
miacode.preview.seek(second)
miacode.preview.getState()
miacode.preview.setSpeed(value)
miacode.preview.addOverlay(overlay)
miacode.preview.clearOverlays(ownerId)
miacode.preview.onDidChangeState(callback)
miacode.preview.onFrame(callback)

miacode.export.getPresets()
miacode.export.registerPreset(preset)
miacode.export.startVideoExport(options)
miacode.export.startCoverExport(options)
miacode.export.registerBeforeExportHook(hook)
miacode.export.registerAfterExportHook(hook)
miacode.export.registerCoverTemplate(templateSpec)
miacode.export.registerBatchJobProvider(provider)

miacode.resources.getMediaInfo()
miacode.resources.getAssetPath(id)
miacode.resources.setAssetPath(id, path)

miacode.fs.readText(path)
miacode.fs.writeText(path, text)
miacode.fs.exists(path)
miacode.fs.listDir(path)

miacode.net.fetch(url, options)
miacode.net.download(url, targetPath)

miacode.settings.get(key)
miacode.settings.set(key, value)

miacode.extensions.all()
miacode.extensions.get(id)
miacode.extensions.enable(id)
miacode.extensions.disable(id)
miacode.extensions.installFromFolder(path)
miacode.extensions.remove(id)

miacode.ui.registerSidebarView(view)
miacode.ui.registerBottomTabView(view)
miacode.ui.registerPreferencesPage(page)
miacode.ui.registerToolbarButton(button)
miacode.ui.getContributions()

miacode.tasks.withProgress(options, callback)
miacode.tasks.registerTask(task)
miacode.tasks.reportProgress(taskId, percent, message)

miacode.logs.append(channel, message)
miacode.logs.getPath(channel)
miacode.logs.open(channel)
```

多数修改类 API 返回 `{ ok, value?, error? }`。读取类 API 返回当前快照。事件、provider、UI 面板、导出 hook 和任务 provider 属于注册型能力：扩展可以注册和查询这些贡献，MiaCode 会清理禁用扩展留下的注册项；具体 UI 消费点会随着对应功能逐步接入。

Most modifying APIs return `{ ok, value?, error? }`. Read APIs return current snapshots. Events, providers, UI panels, export hooks, and task providers are registration-style capabilities: extensions can register and query these contributions, and MiaCode cleans them up when an extension is disabled; concrete UI consumption points can attach to those registrations as the matching surfaces evolve.

## Examples / 示例

Command example:

```js
function activate() {
  miacode.commands.registerCommand("sample.show-file", () => {
    miacode.window.showInformationMessage(miacode.workspace.getChartFolder().value || "No chart folder");
  });
}
module.exports = { activate };
```

Menu contribution:

```json
{
  "contributes": {
    "commands": [{ "command": "sample.show-file", "title": "Show Folder" }],
    "menus": { "tools/menu": [{ "command": "sample.show-file" }] }
  }
}
```

Language contribution:

```json
{
  "contributes": {
    "languages": [
      { "id": "sample", "label": "Sample", "translations": "./i18n/sample.json" }
    ]
  }
}
```

## Validation And Testing / 校验与测试

安装前运行：

```powershell
node tools/extensions/validate-extension.mjs templates/extensions/hello-world
```

安装后在首选项刷新扩展，检查权限摘要、启用/禁用、诊断、高风险权限首次确认，以及当前谱面文本、dirty 状态、文件路径和当前难度是否保持正确。

Run before installing:

```powershell
node tools/extensions/validate-extension.mjs templates/extensions/hello-world
```

After installing, refresh extensions in Preferences and check permission summaries, enable/disable behavior, diagnostics, high-risk first-use confirmation, and whether the current chart text, dirty state, file path, and active difficulty remain correct.

## Prompt For AI / 给 AI 的提示词

中文提示词：

```text
请为 MiaCode 创建一个本地扩展包。输出完整文件夹结构、miacode-extension.json、所有 JS 或数据文件。不要修改 MiaCode 本体源码，也不要要求用户重新编译 MiaCode；扩展必须只通过 manifest、贡献点、数据文件和 miacode.* API 实现。根据需求选择 commands、menus、languages、editor、timeline、preview、export、resources、ui、tasks 等能力。若使用 JS，只能使用 CommonJS 子集和 miacode.* API，不要使用 require、fs、path、process、Buffer、child_process。请为每个 API 调用声明 permissions，并标注这一项的风险权限。说明如何安装到 <install root>\extensions、刷新扩展、启用/禁用、测试权限和查看诊断。
```

English prompt:

```text
Create a local MiaCode extension package. Output the full folder structure, miacode-extension.json, and all JS or data files. Do not modify MiaCode application source code and do not ask the user to rebuild MiaCode; the extension must work only through the manifest, contribution points, data files, and miacode.* APIs. Choose the needed capabilities such as commands, menus, languages, editor, timeline, preview, export, resources, ui, and tasks. If JavaScript is used, use only the CommonJS subset and miacode.* APIs; do not use require, fs, path, process, Buffer, or child_process. Declare permissions for every API call and mark risk permissions.(Please declare permissions for each API call, and mark the risk permissions for this item.) Explain how to install into <install root>\extensions, refresh extensions, enable/disable it, test permissions, and inspect diagnostics.
```
