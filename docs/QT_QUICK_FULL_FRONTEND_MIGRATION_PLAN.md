# Qt Quick Hybrid Host Frontend Migration Plan

Status: Draft hybrid-host implementation spec

This document defines the migration strategy for moving MiaCode to a Qt Quick top-level host window while retaining the existing native `MainWindow` UI regions wherever keeping the current QWidget implementation is the lowest-risk way to preserve behavior and appearance.

The goal is no longer a full Qt Quick replacement of the desktop frontend. The goal is a hybrid host architecture:

- Qt Quick owns the top-level host window.
- The realtime preview lives directly inside that host window and shares the same Quick scene graph.
- The rest of the user-facing UI keeps its current native QWidget implementation and native visual style, then gets embedded into the new host as a small number of coarse-grained native regions.

This document is intentionally behavior-first. It enumerates the current UI regions, the ownership and interaction contracts they already have, and the exact retained-native versus same-window Quick split required by the new architecture.

Current source references used while building this plan:

- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `docs/PREVIEW_NATIVE_WINDOW_DEVELOPMENT_PLAN_SPEC.md`
- `docs/TIMELINE_COORDINATE_FOCUS_SPEC.md`
- `.codex/skills/miacode-dev-guide/references/feature-index.md`
- `src/app/mainwindow/sections/README.md`
- `src/app/mainwindow/MainWindow.cpp`
- `src/app/mainwindow/MainWindow.h`
- `src/editor/PlainCodeEditor.*`
- `src/timeline/TimelineView*`
- `src/tools/latency/LatencyDetectorDialog*`
- `src/tools/video_export/VideoExportDialog*`
- `src/tools/video_export/BatchVideoExportDialog*`

## 1. Scope And Non-Goals

### In scope

- Replace the current top-level frontend host window with a Qt Quick host window.
- Keep the realtime preview in the same Quick window and same scene graph as the new host.
- Preserve the existing native QWidget look and behavior for the current non-preview UI.
- Refactor the current `MainWindow` into embeddable native regions instead of embedding a full `QMainWindow`.
- Preserve current command routing, save/dirty-state behavior, focus rules, timeline/editor/preview linkage, and modal behavior.

### Out of scope

- Rewriting `PlainCodeEditor` into a Qt Quick editor as part of this plan.
- Rewriting `TimelineView` into a Qt Quick timeline as part of this plan.
- Rewriting current tool dialogs into Quick dialogs as part of this plan.
- Rewriting the existing Qt Quick preview scene-graph stack in `src/preview/quick_scene/*`.
- Rewriting parser, document, timeline, preview, export, or latency business rules.

### Explicit non-goal

- This plan does not pursue a pure-Quick frontend replacement anymore.

### Important baseline

- The realtime preview scene itself is already Qt Quick.
- The architecture problem we are solving is the old preview embedding model, not the preview renderer itself.
- The visual priority is now "preserve current native appearance" rather than "recreate native appearance in Quick".

## 2. Why This Pivot

The original full-replacement plan assumed that editor, timeline, shell chrome, and dialogs would all eventually move to pure Qt Quick. Implementation and evaluation showed that this path creates too much appearance drift and too much invasive surface-area change for the value it provides.

The hybrid-host pivot is chosen because it:

- minimizes appearance drift by preserving the current native QWidget UI
- keeps the highest-risk components on the implementations that already work
- removes the preview child-window performance and compatibility problem from the target architecture
- narrows the migration to the part where Qt Quick already provides clear value: top-level hosting and direct preview composition

## 3. Current UI Inventory And Hybrid Targets

| Current area | Current implementation | User-facing responsibility | Hybrid target |
|---|---|---|---|
| App window shell | `QMainWindow` + menu bar + toolbar + status bar | Top-level window, menus, toolbar, status, global shortcuts, modal orchestration | Replace with a Quick host window, but keep menu/toolbar/status UI as one retained-native top chrome region |
| Left outline sidebar | `QDockWidget` + `QListWidget` | Metadata/difficulty navigation, add difficulty, delete affordance | Keep native and move into the retained-native main workspace region |
| Metadata page | QWidget page in `QStackedWidget` | Edit title/artist/first/designer/extra metadata | Keep native and move into the retained-native main workspace region |
| Chart page shell | QWidget page in `QStackedWidget` | Editor shell, header, batch transform affordances, bottom tab visibility | Keep native and move into the retained-native main workspace region |
| `PlainCodeEditor` | `QTextEdit` subclass with custom paint/menu behavior | Chart text editing, line numbers, current-line highlight, transforms, find/replace interactions | Keep native and move into the retained-native main workspace region |
| Bottom tabs shell | `QTabWidget` | Hosts timeline, validation, and Muri tabs | Keep native and move into the retained-native main workspace region |
| `TimelineView` | `QAbstractScrollArea` subclass | Timeline rendering and interaction semantics | Keep native and move into the retained-native main workspace region |
| Validation list | `QListWidget` | Validation issue display, jump/copy/ignore actions | Keep native and move into the retained-native main workspace region |
| Muri list | `QListWidget` | Muri issue display, jump/copy/ignore/seek actions | Keep native and move into the retained-native main workspace region |
| Preview canvas | `PreviewRuntime` hosted through `QQuickView` child-window embedding | Realtime preview rendering | Keep Quick-rendered, but host directly inside the new Quick root window instead of via the old embedded child-window path |
| Preview controls | Native buttons, slider, menus, labels | Transport, speed, fullscreen, seek | Keep native and move into the retained-native preview controls/stats region |
| Preview stats card | QWidget + `QGridLayout` labels | Object-count chips and totals | Keep native and move into the retained-native preview controls/stats region |
| Preferences dialog | `QDialog` | Language/theme/editor display preferences | Keep native and unchanged |
| Preview settings dialog | `QDialog` | Audio/video/preview settings | Keep native and unchanged |
| About dialog | `QDialog` | Version/info and easter egg behavior | Keep native and unchanged |
| Latency detector | `QDialog` + waveform widget | BPM/offset detection workflow | Keep native and unchanged |
| Video export dialog | `QDialog` | Export setup, preview, range selection | Keep native and unchanged |
| Batch export dialog | `QDialog` | Batch export setup | Keep native and unchanged |
| Native file flows and message boxes | `QFileDialog`, `QMessageBox` | Open/save/save-as, confirmations, restart notices | Keep native and unchanged |

## 4. Target Hybrid Architecture

### 4.1 Root host

The root frontend becomes a Quick host window:

- `ApplicationWindow` or equivalent `QQuickWindow`-backed root
- one Quick scene graph for host chrome background and realtime preview
- no `QWidget::createWindowContainer()` preview embedding in the final hybrid path

The host is responsible for:

- top-level window lifetime
- top-level geometry and theme synchronization
- embedding the retained-native regions
- same-window preview composition
- modal orchestration and activation-state handling

### 4.2 Retained-native region decomposition

The retained-native architecture is decision-complete and must be decomposed into exactly these four regions:

1. Native top chrome widget
   - Contains the current menu bar, toolbar, and status strip
   - Preserves the current native visual styling and command structure
   - Is extracted from `MainWindow` into an embeddable widget, not left as `QMainWindow` chrome

2. Native main workspace widget
   - Contains the current outline, metadata/chart stack, bottom tabs, editor, timeline, validation list, and Muri list
   - Preserves the current native layout and interaction behavior
   - Remains the main authoring surface

3. Native preview controls/stats widget
   - Contains the current preview transport controls and preview stats card
   - Preserves the current native styling and transport semantics
   - Sits adjacent to the Quick preview canvas inside the host layout

4. Native dialogs
   - Existing `QDialog` tool windows remain unchanged
   - Existing native file dialogs and message boxes remain unchanged

### 4.3 Coarse-grained embedding rule

The retained-native regions must remain coarse-grained. The host must not embed dozens of tiny native islands.

Rules:

- embed one top chrome region
- embed one workspace region
- embed one preview-controls/stats region
- keep dialogs standalone and native

This minimizes churn and avoids turning the host into a patchwork of unrelated child windows.

### 4.4 Preview integration rule

Preview is the only area that must remain Quick-rendered in the final architecture.

Rules:

- preview canvas must be hosted directly in the Quick root window
- preview must share the same Quick scene graph as the host window
- preview must not rely on `QWidget::createWindowContainer()` in the target hybrid path
- preview must not be hosted as a separate child window in the final hybrid path

The existing `PreviewQuickSceneRoot` and `PreviewQuickHudLayer` remain the rendering path. The migration work here is host integration, not preview-scene replacement.

### 4.5 `MainWindow` refactor boundary

The current `MainWindow` must not be embedded whole as a `QMainWindow`.

Instead:

- extract its chrome into an embeddable native top chrome widget
- extract its main authoring UI into an embeddable native workspace widget
- extract its preview controls/stats card into an embeddable native preview widget
- keep the existing section-based logic and C++ business ownership

The business logic stays in C++ and remains shared. The refactor is about host shape and widget extraction, not about rewriting behavior.

## 5. Action Routing And State Ownership

### 5.1 Command ownership

Existing command behavior remains owned by C++.

Keep:

- current `QAction` semantics
- current save/open/new flows
- current transform actions
- current preview transport logic
- current focus-aware cut/copy/paste/undo/redo behavior
- current timeline/editor/preview linkage rules

### 5.2 Host responsibilities

The Quick host is responsible for:

- top-level window presentation
- placing the retained-native regions
- same-window preview placement
- theme synchronization between Quick host and retained-native regions
- modal visibility and activation handling

### 5.3 Native region responsibilities

The retained-native regions keep owning their current UI behavior:

- menu/toolbar/status behavior stays in the native top chrome region
- authoring workflow stays in the native workspace region
- preview transport/stats behavior stays in the native preview-controls/stats region
- dialogs stay fully native

## 6. Behavioral Contract In The Hybrid Model

The following current behaviors remain mandatory:

- outline switching between metadata and difficulty still stops preview and preserves current tab visibility semantics
- metadata and chart edits still mark the current field dirty and save through the same document path
- editor cursor movement, focus, and `Ctrl+LeftClick` interactions still drive timeline and preview exactly as today
- timeline interactions still drive preview and editor exactly as today
- validation and Muri activation/copy/ignore behavior stays unchanged
- preview transport, speed changes, slider seek, stats updates, and fullscreen behavior stay unchanged
- preferences/theme changes still apply immediately where they do today
- dialogs keep their current field semantics and side effects because they remain the same native dialogs

## 7. Phased Delivery

| Phase | Goal | In scope | Intentionally left unchanged |
|---|---|---|---|
| Phase 0 | Freeze hybrid contract and extraction boundaries | This document, extraction map, host/preview/native-region responsibilities | No production UI change yet |
| Phase 1 | Quick host window with same-window preview | Quick host root, direct Quick preview composition, retained-native top chrome region, retained-native workspace region, retained-native preview-controls/stats region, experimental rollout path | Native dialogs stay unchanged |
| Phase 2 | Stabilize hybrid integration | Geometry synchronization, action routing, theme synchronization, focus return, modal orchestration, preview/fullscreen correctness, retained-native extraction cleanup | Native look and widget implementations stay unchanged |
| Phase 3 | Optional targeted Quick migrations only by separate RFC | Only areas with an explicit, separately approved benefit-driven proposal | No default plan to rewrite editor, timeline, or dialogs into Quick |

There is no default phase in this plan that rewrites `PlainCodeEditor`, `TimelineView`, or the tool dialogs into Qt Quick.

## 8. Acceptance Criteria

| Area | Acceptance rule |
|---|---|
| Host window | Normal frontend path uses a Quick host window |
| Preview | Realtime preview is same-window Quick, not child-window embedded preview |
| Native appearance | Retained-native regions preserve the current QWidget appearance and behavior |
| Top chrome | Menu bar, toolbar, and status strip remain visually and behaviorally equivalent to the current native UI |
| Workspace | Outline, metadata/chart stack, bottom tabs, editor, timeline, validation, and Muri remain functionally identical because they are still the same native implementations |
| Preview controls/stats | Preview transport and stats remain functionally identical because they are still the same native implementations |
| Dialogs | Preferences, preview settings, latency detector, about, export, and batch export dialogs remain native and keep their existing behavior |
| Compatibility | The target architecture no longer depends on the old preview child-window embedding path |
| Code removal | There is no acceptance requirement to remove all QWidget shell code from the normal path |

## 9. Compatibility Constraints

- Existing widget-based styling is now part of the intended final architecture, not temporary scaffolding.
- Existing native dialog behavior is now part of the intended final architecture, not temporary scaffolding.
- The preview child-window path is the compatibility problem to remove; retained-native non-preview regions are acceptable in the target design.
- Any future attempt to replace retained-native regions with Quick must be justified by a separate RFC with its own product rationale, migration cost review, and QA plan.

## 10. Test And Validation Focus

The hybrid plan must be validated against both behavior and architecture:

- verify the updated host keeps the current native appearance for non-preview UI
- verify the preview is rendered directly in the Quick host window
- verify native dialogs no longer blank or orphan the preview because the preview is no longer hosted through the old child-window embedding path
- verify focus return, fullscreen preview, modal dialogs, save prompts, and theme changes still behave correctly
- verify command routing still matches the current `MainWindow` behavior

Architecture sanity checks:

- no retained-native region is specified vaguely; the four-region split above is the full retained-native set
- no section of this document still assumes editor, timeline, dialogs, or shell chrome must be rewritten in Quick by default
- no section of this document still forbids retaining native islands as the final architecture
- no section of this document still treats deleting `QMainWindow` or QWidget shell code as the success criterion

## 11. Final Recommendation

The safest forward path is:

1. Move the top-level host to Qt Quick.
2. Keep the current native UI implementations wherever appearance fidelity and behavior fidelity matter most.
3. Put the preview directly into the Quick host window and remove the old preview embedding model from the target architecture.
4. Treat any future full-Quick replacement work as a separate product and engineering decision, not as the default continuation of this plan.

The dominant risk is no longer "How do we rewrite everything in Quick?" The dominant risk is "How do we extract and host the current native UI cleanly while keeping the preview same-window Quick?" This plan is optimized around that risk profile.

## 12. Assumptions

- This English document is the only file updated in this pass.
- `docs/QT_QUICK_FULL_FRONTEND_MIGRATION_PLAN_ZH.md` is intentionally left unchanged for now.
- "Qt original style" means preserving the current QWidget visual implementation, not recreating it in Quick.
- Preview is the only area that must remain Quick-rendered in the final hybrid architecture.
- Any future push toward full Quick replacement of editor, timeline, dialogs, or shell chrome requires a separate proposal.
