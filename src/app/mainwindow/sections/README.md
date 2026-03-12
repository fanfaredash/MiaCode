# MainWindow Sections

`MainWindow.cpp` keeps the entry point and event backbone, then composes feature blocks via `#include`:

- `sections/frame/MainWindow.BootstrapAndMenus.cpp`
  - Window skeleton, menus, toolbar, dock/splitter, base widgets.
- `sections/document/MainWindow.DocumentFlow.cpp`
  - Open/save flow, dirty state, field switching and sidebar.
- `sections/timeline/MainWindow.PreviewTimelineFlow.cpp`
  - Timeline data refresh, cursor sync, playback position flow.
- `sections/validation/MainWindow.ValidationFlow.cpp`
  - Syntax-check execution, error list rendering, highlight decoration and jump.
- `sections/editor/MainWindow.EditorDisplay.cpp`
  - Editor display preferences (font/spacing) and related helpers.
- `sections/preferences/MainWindow.PreferencesDialog.cpp`
  - Preferences dialog and persistence.
- `sections/preview/MainWindow.PreviewSessionFlow.cpp`
  - Preview session lifecycle and IPC.

## Split Rule

- Keep `MainWindow.cpp` and `MainWindow.h` in `src/app/mainwindow/`.
- New feature slices go under `sections/<feature>/`.
- If cross-slice declarations become noisy, add thin bridging headers instead of moving everything back to `MainWindow.cpp`.
