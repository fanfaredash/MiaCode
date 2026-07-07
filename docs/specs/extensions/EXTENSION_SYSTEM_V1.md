# MiaCode Extension System v1

MiaCode v1 extensions are local packages loaded from the install-root `extensions` directory or development paths. Extensions are normal folders with a manifest and optional data or JavaScript files. MiaCode does not ship test capabilities as hidden built-ins; visible commands, menus, languages, and other extension-provided features must come from installed and enabled extension packages.

## Runtime

- JavaScript runtime: embedded Qt `QJSEngine` owned by MiaCode.
- Node.js is not required. The runtime does not search external JavaScript executables or run a separate host script.
- CommonJS subset entry:

```js
function activate(context) {}
function deactivate() {}
module.exports = { activate, deactivate };
```

- Unsupported Node built-ins: `require`, `fs`, `path`, `process`, `Buffer`, `child_process`.
- Discovery paths:
  - User installation: `<install-root>/extensions`, beside the top-level `MiaCode.exe` launcher in release packages.
  - Development: `MIACODE_EXTENSION_DEV_PATHS`, separated with the platform path-list separator. Each entry may point directly at an extension root or at a parent directory containing extension roots.
  - Local build helper: `<app-dir>/extensions-dev`.
- User management: Preferences > Extensions lists discovered extensions, diagnostics, contribution summaries, and an enable/disable checkbox.
- Refresh behavior: the user extension directory is watched and rescanned after a debounce; the Preferences page also provides a manual refresh button.
- Failure boundary: JavaScript exceptions are logged as extension diagnostics and must not crash MiaCode.

## Manifest

Extensions may use `miacode-extension.json`, or `package.json` with a `miacodeExtension` object. Required fields are `id`, `name`, `version`, `publisher`, and `engines.miacode`. The `main` entry is required only for JavaScript extensions.

```json
{
  "id": "hello-world",
  "name": "Hello World",
  "version": "0.0.1",
  "publisher": "local",
  "main": "./extension.js",
  "permissions": ["ui.message", "workspace.read"],
  "engines": {
    "miacode": ">=1.0.0"
  },
  "activationEvents": ["onStartupFinished"],
  "contributes": {
    "commands": [
      {
        "command": "hello-world.say-hello",
        "title": "Say Hello",
        "category": "Hello World"
      }
    ],
    "menus": {
      "tools/menu": [
        { "command": "hello-world.say-hello" }
      ]
    }
  }
}
```

Data-only extension:

```json
{
  "id": "sample-data",
  "name": "Sample Data Extension",
  "version": "0.0.1",
  "publisher": "local",
  "engines": {
    "miacode": ">=1.0.0"
  },
  "contributes": {
    "languages": [
      {
        "id": "sample",
        "label": "Sample Language",
        "translations": "./i18n/sample.json"
      }
    ]
  }
}
```

Translation files are UTF-8 JSON objects keyed by `UiText` ids:

```json
{
  "dialog.preferences.title": "Preferences",
  "dialog.preferences.language": "Language"
}
```

If the current language belongs to an extension that is deleted or disabled, MiaCode falls back to `Follow System` and warns the user that the language pack is unavailable.

## Permissions

JavaScript extensions must declare the permissions they use. The manifest loader rejects unknown permission ids, and runtime calls without a matching declared permission return `{ "ok": false, "error": "..." }` and add an extension diagnostic.

Low-risk permissions are shown plainly:

- `app.read`
- `ui.message`
- `ui.status`
- `events.subscribe`
- `workspace.read`
- `editor.read`
- `commands.read`
- `timeline.read`
- `preview.read`
- `resources.read`
- `export.read`
- `settings.read`
- `logs.read`

Medium-risk permissions are shown with explanatory text:

- `ui.prompt`
- `document.edit`
- `editor.edit`
- `commands.execute`
- `diagnostics.run`
- `analysis.run`
- `timeline.control`
- `preview.control`
- `ui.contribute`
- `providers.register`
- `tasks.run`

High-risk permissions are highlighted in Preferences > Extensions and require first-use confirmation. The user's grant is stored under `extensions.permissions` and can be revoked from the extension page.

- `workspace.write`
- `resources.write`
- `filesystem.read`
- `filesystem.write`
- `network.fetch`
- `settings.write`
- `extensions.manage`
- `export.write`
- `logs.write`

Blocked by default for v1:

- external shell/process execution
- arbitrary DLL/native module loading
- Node.js built-ins and npm package loading
- unattended extension auto-update

Supported v1 menu contribution points:

- `tools/menu`: adds commands under `Tools -> Extensions`.

Reserved but not yet wired contribution points:

- `editor/title`
- `editor/context`
- `timeline/context`

## API

The embedded runtime exposes global `miacode`:

- `miacode.app.getInfo()`
- `miacode.app.openPreferences()`
- `miacode.app.reloadExtensions()`
- `miacode.commands.registerCommand(command, callback)`
- `miacode.commands.executeCommand(command, args?)`
- `miacode.commands.getCommands()`
- `miacode.window.showInformationMessage(message)`
- `miacode.window.showWarningMessage(message)`
- `miacode.window.showErrorMessage(message)`
- `miacode.window.showInputBox(options?)`
- `miacode.window.showQuickPick(items, options?)`
- `miacode.window.createStatusBarItem(options)`
- `miacode.workspace.getActiveDocument()`
- `miacode.workspace.applyDocumentEdit({ text })`
- `miacode.workspace.getChartMetadata()`
- `miacode.workspace.updateChartMetadata(patch)`
- `miacode.workspace.getChartFolder()`
- `miacode.workspace.getMediaFiles()`
- `miacode.workspace.getRecentFiles()`
- `miacode.workspace.getProjectData(key?)`
- `miacode.workspace.setProjectData(key, value)`
- `miacode.workspace.scanChartFolders(rootPath)`
- `miacode.workspace.save()`
- `miacode.workspace.saveAs(path)`
- `miacode.workspace.onDidOpenDocument(callback)`
- `miacode.workspace.onDidSaveDocument(callback)`
- `miacode.document.getDifficulties()`
- `miacode.document.getActiveDifficulty()`
- `miacode.document.setActiveDifficulty(id)`
- `miacode.document.replaceActiveDifficultyText(text)`
- `miacode.document.getParsedNoteMarkers()`
- `miacode.document.getTimingMetadata()`
- `miacode.document.applyTextEdits(edits)`
- `miacode.document.format()`
- `miacode.document.createDifficulty(options)`
- `miacode.document.deleteDifficulty(id)`
- `miacode.document.renameDifficulty(id, label)`
- `miacode.document.onDidChangeText(callback)`
- `miacode.editor.getSelection()`
- `miacode.editor.getCursor()`
- `miacode.editor.getLine(line)`
- `miacode.editor.getCurrentLine()`
- `miacode.editor.getCurrentToken()`
- `miacode.editor.insertText(text)`
- `miacode.editor.replaceSelection(text)`
- `miacode.editor.replaceRange(range, text)`
- `miacode.editor.setSelection(range)`
- `miacode.editor.addDecoration(range, options?)`
- `miacode.editor.clearDecorations(ownerId?)`
- `miacode.editor.showHover(range, markdown)`
- `miacode.editor.addGutterIcon(options)`
- `miacode.editor.clearGutterIcons(ownerId?)`
- `miacode.editor.fold(range)`
- `miacode.editor.unfold(range)`
- `miacode.editor.registerHoverProvider(provider)`
- `miacode.editor.registerCompletionProvider(provider)`
- `miacode.editor.registerCodeActionProvider(provider)`
- `miacode.editor.onDidChangeSelection(callback)`
- `miacode.validation.run()`
- `miacode.validation.getLastResult()`
- `miacode.validation.addDiagnostics(ownerId, diagnostics)`
- `miacode.validation.clearDiagnostics(ownerId?)`
- `miacode.diagnostics.validateDocument()`
- `miacode.analysis.runMuriAnalysis()`
- `miacode.analysis.getLastMuriResult()`
- `miacode.timeline.getSnapshot()`
- `miacode.timeline.getCurrentSecond()`
- `miacode.timeline.seek(second)`
- `miacode.timeline.addMarker(marker)`
- `miacode.timeline.clearMarkers(ownerId?)`
- `miacode.timeline.addBand(band)`
- `miacode.timeline.addVerticalLine(line)`
- `miacode.timeline.clearVisuals(ownerId?)`
- `miacode.timeline.registerMarkerClickCommand(command)`
- `miacode.timeline.onDidSeek(callback)`
- `miacode.preview.play()`
- `miacode.preview.pause()`
- `miacode.preview.stop()`
- `miacode.preview.seek(second)`
- `miacode.preview.getState()`
- `miacode.preview.setSpeed(value)`
- `miacode.preview.addOverlay(overlay)`
- `miacode.preview.clearOverlays(ownerId?)`
- `miacode.preview.onDidChangeState(callback)`
- `miacode.preview.onFrame(callback)`
- `miacode.export.getPresets()`
- `miacode.export.registerPreset(preset)`
- `miacode.export.startVideoExport(options)`
- `miacode.export.startCoverExport(options)`
- `miacode.export.registerBeforeExportHook(hook)`
- `miacode.export.registerAfterExportHook(hook)`
- `miacode.export.registerCoverTemplate(templateSpec)`
- `miacode.export.registerBatchJobProvider(provider)`
- `miacode.resources.getMediaInfo()`
- `miacode.resources.getAssetPath(id)`
- `miacode.resources.setAssetPath(id, path)`
- `miacode.fs.readText(path)`
- `miacode.fs.writeText(path, text)`
- `miacode.fs.exists(path)`
- `miacode.fs.listDir(path)`
- `miacode.net.fetch(url, options?)`
- `miacode.net.download(url, targetPath)`
- `miacode.settings.get(key)`
- `miacode.settings.set(key, value)`
- `miacode.extensions.all()`
- `miacode.extensions.get(id)`
- `miacode.extensions.enable(id)`
- `miacode.extensions.disable(id)`
- `miacode.extensions.installFromFolder(path)`
- `miacode.extensions.remove(id)`
- `miacode.ui.registerSidebarView(view)`
- `miacode.ui.registerBottomTabView(view)`
- `miacode.ui.registerPreferencesPage(page)`
- `miacode.ui.registerToolbarButton(button)`
- `miacode.ui.getContributions()`
- `miacode.tasks.withProgress(options, callback?)`
- `miacode.tasks.registerTask(task)`
- `miacode.tasks.reportProgress(taskId, percent, message?)`
- `miacode.logs.append(channel, message)`
- `miacode.logs.getPath(channel?)`
- `miacode.logs.open(channel?)`

Privileged APIs return `{ ok, value?, error? }`. Read-only snapshot helpers may return a direct object when the current implementation has no expected failure path. Events, providers, UI surfaces, export hooks, and task providers are registration-style APIs: extensions can register them now, MiaCode stores and cleans the registrations by extension owner, and concrete UI/feature consumers can attach to those registrations as their surfaces evolve.

File access defaults to the extension folder, the current chart folder, and read-only log paths. Broader file access requires high-risk confirmation. Network access blocks local and private-network targets by default unless the user explicitly confirms that grant.

## Developer Tools

- Runtime implementation: `src/extensions/EmbeddedExtensionRuntime.*`
- Extension manager: `src/extensions/ExtensionManager.*`
- Template: `templates/extensions/hello-world`
- Type declarations: `packages/miacode-extension-api`
- Manifest schema: `resources/extensions/miacode-extension.schema.json`
- User-facing extension README: `resources/extensions/README.md`, copied into release packages as `extensions/README.md`
- Local validator:

```powershell
node tools/extensions/validate-extension.mjs templates/extensions/hello-world
```
