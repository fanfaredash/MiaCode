# MiaCode Extension System v1

MiaCode v1 extensions are local packages loaded from the user extensions directory or development paths. Command extensions run JavaScript/TypeScript in a separate Node.js Extension Host process over line-delimited JSON-RPC. Pure language-pack extensions are data-only and do not start the Extension Host.

MiaCode does not ship test languages as hidden built-ins. If a language such as Japanese appears in the language selector, its id, label, and translation file came from an installed and enabled extension package.

## Runtime

- Host process: `node resources/extensions/extensionHost.js`, deployed beside `MiaCode.exe` as `extensions/extensionHost.js`.
- IPC: one JSON-RPC message per line over stdio.
- Discovery paths:
  - User data: `QStandardPaths::AppDataLocation/extensions`
  - Development: `MIACODE_EXTENSION_DEV_PATHS`, separated with the platform path-list separator; each entry may point directly at an extension root or at a parent directory containing extension roots
  - Local build helper: `<app-dir>/extensions-dev`
- User management: Preferences > Extensions lists discovered extensions, diagnostics, contribution summaries, and an enable/disable checkbox.
- Refresh behavior: the user extension directory is watched and rescanned after a debounce; the Preferences page also provides a manual refresh button.
- Failure boundary: an extension or Extension Host crash is logged and must not crash MiaCode.

## Manifest

Extensions may use `miacode-extension.json`, or `package.json` with a `miacodeExtension` object. Required fields are `id`, `name`, `version`, `publisher`, and `engines.miacode`. The `main` entry is required only for command or activation-event extensions.

```json
{
  "id": "hello-world",
  "name": "Hello World",
  "version": "0.0.1",
  "publisher": "local",
  "main": "./extension.js",
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

Pure language-pack extension:

```json
{
  "id": "sample-language",
  "name": "Sample Language Extension",
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

Language translation files are UTF-8 JSON objects keyed by `UiText` ids:

```json
{
  "dialog.preferences.title": "Settings",
  "dialog.preferences.language": "Language"
}
```

If the current language belongs to an extension that is deleted or disabled, MiaCode falls back to `Follow System` and warns the user that the language pack is unavailable.

Supported v1 menu contribution point:

- `tools/menu`

Reserved but not yet wired contribution points:

- `editor/title`
- `editor/context`
- `timeline/context`

## API

The JS entry exports:

```js
function activate(context) {}
function deactivate() {}
module.exports = { activate, deactivate };
```

The Extension Host exposes `global.miacode`:

- `miacode.commands.registerCommand(command, callback)`
- `miacode.window.showInformationMessage(message)`
- `miacode.window.showWarningMessage(message)`
- `miacode.window.showErrorMessage(message)`
- `miacode.workspace.getActiveDocument()`
- `miacode.workspace.applyDocumentEdit({ text })`
- `miacode.diagnostics.validateDocument()`

Document edits are v1 whole-document replacements for the active difficulty text. MiaCode does not expose internal C++ document objects to extensions.

## Developer Tools

- Template: `templates/extensions/hello-world`
- Type declarations: `packages/miacode-extension-api`
- Manifest schema: `resources/extensions/miacode-extension.schema.json`
- User-facing extension README: `resources/extensions/README.md`, copied into release packages as `extensions/README.md`
- Local validator:

```powershell
node tools/extensions/validate-extension.mjs templates/extensions/hello-world
```
