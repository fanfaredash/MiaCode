# MiaCode Extension System v1

MiaCode v1 extensions are local packages loaded from the install-root `extensions` directory or development paths. v1 is a small closed loop, not a broad platform catalog: public APIs must be real, callable, and documented.

## Runtime

- JavaScript runtime: embedded Qt `QJSEngine`.
- Node.js is not required and Node built-ins are not available.
- Entry format:

```js
function activate(context) {}
function deactivate() {}
module.exports = { activate, deactivate };
```

- Discovery paths:
  - `<install-root>/extensions`
  - `MIACODE_EXTENSION_DEV_PATHS`
  - `<app-dir>/extensions-dev`
- Preferences > Extensions lists discovered extensions and supports enable/disable/refresh.

## Manifest

Required fields: `id`, `name`, `version`, `publisher`, and `engines.miacode`. `main` is required for JavaScript extensions. Data-only language-pack extensions can omit `main`.

Supported contribution points in v1:

- `contributes.commands`
- `contributes.menus["tools/menu"]`
- `contributes.menus["menubar/beforeHelp"]`
- `contributes.languages`

## API Status

The public registry uses exactly three statuses:

- `implemented`: callable v1 API.
- `planned`: known direction, not callable in v1.
- `blocked`: rejected in the ordinary v1 host.

`api.call` and `api.invoke` only call `implemented` entries. `planned` and `blocked` entries can be queried, but execution returns an error. Missing needs should be recorded with `api.request`.

## Permission Model

Runtime allow/deny has two classes:

- unblocked: callable when the manifest declares the required permission.
- blocked: denied even when the manifest declares the permission.

Risk labels such as low/medium/high are documentation only. They do not trigger prompts, grants, or stored approvals.

Blocked APIs in the ordinary v1 host:

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

## Implemented v1 Surface

Core:

- manifest loading and validation
- commands, menu contributions, and command execution
- language packs
- API discovery: `api.list`, `api.has`, `api.describe`, `api.describeNamespace`, `api.call`, `api.invoke`, `api.request`

Document/editor:

- `workspace.getActiveDocument`
- `workspace.applyDocumentEdit`
- `workspace.getChartMetadata`
- `workspace.updateChartMetadata`
- `workspace.getChartFolder`
- `workspace.getMediaFiles`
- `workspace.save`
- `workspace.saveAs`
- `document.query`
- `document.edit`
- difficulty read/write helpers
- parsed note markers and timing metadata
- text edits and formatting
- editor selection/cursor/line/token helpers
- editor insert/replace/decorations

Validation/timeline/preview/UI:

- validation run/result/extension diagnostics
- timeline snapshot/current second/seek
- timeline markers, bands, vertical lines, and clear
- preview playback controls and speed
- preview text overlays: add/update/remove/clear/list/render/hit-test
- bottom-tab extension views
- toolbox buttons
- basic declarative view rendering
- logs append/path/open
- extension list/get/enable/disable/install/remove/reload

## Planned But Not Callable In v1

- event callbacks
- editor hover/completion/code-action providers
- export hooks/templates/providers
- native sidebar slots
- native Preferences pages
- media processing APIs
- theme APIs
- backup APIs
- shortcut APIs

These should remain `planned` until they have a real callback/consumer/rendering path and tests.

## Blocked Capabilities

Ordinary v1 extensions must not execute shell/process/native/raw renderer/raw export/security/update capabilities. Future dev-mode, trusted-extension, or experimental-build work can revisit them separately without changing the ordinary v1 host.

## Consistency Rules

- C++ manifest loader, JSON schema, CLI validator, TypeScript declarations, README, and this spec must agree.
- Registry status must be one of `implemented`, `planned`, or `blocked`.
- A registry entry may be `implemented` only when the host method is callable and the feature has a real consumer/rendering path.
- Planned APIs must return a clear error if reached through a direct wrapper.
- Blocked APIs must deny at runtime regardless of manifest declaration.
- Unblocked permissions do not prompt the user; extension installation is the trust boundary.
