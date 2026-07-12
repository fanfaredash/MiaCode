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
- `blocked`: reserved for future hard-deny policy.

`api.call` and `api.invoke` only call `implemented` entries. `planned` entries can be queried, but execution returns an error. Missing needs should be recorded with `api.request`.

## Open Bridge

`miacode.open` exposes internal facade objects for extension authors who need a more composable surface than one wrapper per feature. Stable facade objects use `stability: "open"`. Raw/internal targets are also listed, but they must carry `stability: "experimentalRaw"`, `experimentalRaw: true`, and `rawAccess: true`.

Implemented v1 Open Bridge entry points:

- `open.list`: list all registered facade objects.
- `open.describe`: describe one facade object, including method `status`, `permission`, and callable route.
- `open.call`: call an `implemented` facade method by object id and method name.
- `open.forbiddenTargets`: legacy discovery name for experimental raw targets.
- `open.describeForbiddenTarget`: legacy describe name for one experimental raw target.

The stable registered facade objects are `app`, `workspace`, `document`, `editor`, `timeline`, `preview`, `validation`, `analysis`, `export`, `ui`, and `extensions`. Experimental raw targets such as `MainWindow`, `QWidget`, `QQuickItem`, `QSGNode`, `QPainter`, `QRhi`, D3D/DirectComposition, `PreviewRuntime`, preview scene/layer internals, `TimelineQuickItem`, `SimaiDocument`, `PlainCodeEditor`, arbitrary `QObject`, `QProcess`, `shell.execute`, `renderer.raw`, `internal.raw`, security internals, and update internals are appended to the same Open Bridge list with `stability: "experimentalRaw"`.

A method may be described as `implemented` only when it has either a `hostMethod` or `command` route that is callable. Planned methods must be marked `planned`, not silently left to fail at `open.call`. Each experimental raw target exposes `inspect` and `callUnsafe`; these are intentionally unstable descriptors for trusted/local development.

`open.call` requires layered permissions: `open.call`, the object permission such as `open.document`, `open.preview`, or `experimental.invoke`, and the method permission such as `workspace.read`, `document.edit`, `preview.control`, or `experimental.invoke`. `open.list`, `open.describe`, and raw-target queries require `open.inspect`.

## SDK Convenience Layer

Stable SDK methods such as `miacode.preview.getState()`, `miacode.timeline.seek(...)`, `miacode.document.edit(...)`, editor commands, and `miacode.ui.registerPetOverlay(...)` are convenience wrappers over the same controlled host/Open Bridge surface. They must not duplicate policy. If a convenience method wraps an Open Bridge facade method, the facade descriptor remains the source of truth for method status, route, and permission.

## Real Events and DevTools

Event registration APIs are real callback paths, not descriptor-only records.
The embedded runtime stores the registering extension id with each callback.
When public host APIs mutate document text, save the document, seek the
timeline, or change preview playback state, the host dispatches the matching
event back into JavaScript:

- `events/document.onDidChangeText`
- `events/workspace.onDidSaveDocument`
- `events/timeline.onDidSeek`
- `events/preview.onDidChangeState`

Callbacks run under the registering extension's identity, so API calls made
inside an event callback still use that extension's manifest permissions.

`miacode.devtools` is a stable diagnostic facade. It is not raw renderer,
QWidget, QML, or QObject access. It exposes:

- `devtools.snapshot`: API descriptors, Open Bridge descriptors, raw markers,
  extension diagnostics, registered event callback count, UI contributions, UI
  views, and recent host calls.
- `devtools.diagnose`: one API id or host method, including route, required
  permission, manifest declaration state, and block-hook state.
- `devtools.recentCalls`: a capped recent host API call ring buffer with compact
  parameter previews.

The visual entry for the same diagnostic data is **Preferences > Extensions >
DevTools Panel**. It is a read-only host snapshot window with API Registry,
Open Bridge, Recent Calls, Extensions, UI Contributions, Diagnostics, and Raw
JSON tabs plus a Refresh Snapshot action. The panel must stay diagnostic-only:
it must not hand raw QWidget/QML/QObject/renderer pointers to extension JS.

## Controlled Pet Overlay

`ui.registerPetOverlay` registers a declarative preview overlay for extension-owned pet/sprite UI. It supports static images, frame animation metadata, `position`/`anchor`, `width`/`height`/`size`, `opacity`, click command, drag, and drag-end command fields.

Pet overlay resource paths (`image`, `src`, `resource`, `frames`, and `sprite.frames`) are resolved by the host and must canonicalize inside the calling extension's root directory. The API must reject missing files, directories, path traversal, and raw renderer/QML/widget object access. Rendering stays host-owned; extensions only provide data.

## Permission Model

Runtime allow/deny is permission-declaration based: callable APIs require the manifest to declare the required permission. Raw/internal namespaces are not hard-blocked, but they are marked `experimentalRaw` or high-risk in descriptors and require explicit raw/experimental permissions.

Risk labels such as low/medium/high are documentation only. They do not trigger prompts, grants, or stored approvals.

Experimental raw namespaces are implemented but unstable:

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

`shell.execute` and `process.spawn` start detached host processes after the
extension declares `shell.execute` or `process.manage`. The other experimental
raw namespaces route through the controlled raw bridge and return an accepted
descriptor unless a concrete native backend is later attached.

## Implemented v1 Surface

Core:

- manifest loading and validation
- commands, menu contributions, and command execution
- language packs
- API discovery: `api.list`, `api.has`, `api.describe`, `api.describeNamespace`, `api.call`, `api.invoke`, `api.request`
- DevTools diagnostics: `devtools.snapshot`, `devtools.diagnose`, `devtools.recentCalls`
- Open Bridge discovery/calls: `open.list`, `open.describe`, `open.call`, `open.forbiddenTargets`, `open.describeForbiddenTarget`
- SDK convenience wrappers over Open Bridge/host methods: `app.openAboutDialog`, `editor.undo`, `editor.redo`, `editor.cut`, `editor.copy`, `editor.paste`, `editor.selectAll`, `ui.registerPetOverlay`

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
- controlled pet overlays with extension-local resources
- bottom-tab extension views
- sidebar-style and preferences-page extension views
- toolbox buttons
- basic declarative view rendering
- event/provider/export hook registration descriptors
- media, theme, backup, and shortcut facades
- logs append/path/open
- extension list/get/enable/disable/install/remove/reload

## Planned But Not Callable In v1

There are currently no registry entries left in `planned`. Future capability
ideas may be added as `planned`, but they must not be advertised as callable
until they have a host route and a clear runtime response.

## Blocked Capabilities

There are currently no hard-blocked extension API ids or permissions. The
empty block hooks remain in source so a future policy can deny specific methods
or permissions without changing the surrounding permission model.

## Consistency Rules

- C++ manifest loader, JSON schema, CLI validator, TypeScript declarations, README, and this spec must agree.
- Registry status must be one of `implemented`, `planned`, or `blocked`.
- A registry entry may be `implemented` only when the host method is callable and the feature has a real consumer/rendering path.
- Planned APIs must return a clear error if reached through a direct wrapper.
- If a future API is marked `blocked`, it must deny at runtime regardless of manifest declaration.
- Unblocked permissions do not prompt the user; extension installation is the trust boundary.
