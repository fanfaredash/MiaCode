# Source layout

The maintained repository guide is `.agents/skills/miacode-dev-guide/SKILL.md`;
`.claude` and `.codex` copies are generated. Code remains the source of truth.
Current ownership contracts live in `docs/specs/ui/CURRENT_ARCHITECTURE_ZH.md`.

| Directory | Responsibility |
| --- | --- |
| `app/qml_ui/` | Product QML frontend, models and reusable components |
| `app/v2/` | Workspace, shared application services and typed ports |
| `app/runtime/` | Session assembly and domain runtime hosts |
| `app/ui/` | Shared theme, localization, shortcuts and native window integration |
| `core/chart/` | Document, parser and transforms |
| `core/scene/` | GPU-independent frame/layer math |
| `core/video/` | Shared preview/video render settings |
| `editor/` | Text policy, completion and bookmark syntax |
| `audio/` | Audio backends and SFX runtime |
| `preview/runtime/` | Preview runtime, assets and export sessions |
| `preview/quick_scene/` | Shared Qt Quick/QSG chart rendering |
| `timeline/` | Timeline model and Quick surface |
| `tools/` | Domain tools, export, analysis, specs and probes |
| `common/` | Shared configuration, resource paths and logging |
| `extensions/` | Retained manifest/schema contract; no active extension host |
| `wrapper/` | Windows launcher |

Keep sources with their domain owner. Reuse existing QML controls and v2 services;
do not create parallel document, playback or resource-lookup authorities.

QmlUiBootstrap is the GUI entry. Session is a QObject, not a hidden QMainWindow.
The old v1 shell, DComp chart renderer and external realtime preview worker are retired.
QSG export still supports its current D3D11/QRhi and OpenGL sessions and export worker;
see `docs/specs/preview/CURRENT_RENDER_EXPORT_CONTRACT_ZH.md`.

Executable contracts stay under `tools/`; registration and source groups live in
`cmake/devtools/`. See `docs/tests/SPEC_CATALOG.md` for targeted validation.
