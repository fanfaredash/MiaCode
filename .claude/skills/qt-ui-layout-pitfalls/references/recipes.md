# Full recipes + commit archaeology

Each pattern: symptom, root cause, proven fix (code), commits that hit it, and how many
iterations it took. Iteration counts > 1 mark places where the first intuitive fix was
wrong — read those recipes especially carefully before "improving" them.

---

## W — QWidget dialogs

### W1. Styled button/edit bottom border clipped (圆角按钮底边被吞)

**Root cause:** vertical `QSizePolicy::Fixed` pins height to `sizeHint()` and **ignores
`setMinimumHeight()`**; `QStyleSheetStyle` under-reports the styled `sizeHint()` by 1–2px,
so the rounded bottom border is cut by the layout cell.

```cpp
button->ensurePolished();                       // MUST come before sizeHint()
button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
```

Both menu-button factories do this (`VideoExportDialog.cpp` and the lambda in
`MainWindow.Dialogs.cpp`). Commits: 97aca5f, be567a5. 1 iteration once root-caused.

### W2. QTabWidget page clipped / tabs overlap (tab 页被裁剪)

**Root cause (two compounding facts):**
(a) the pane (`QStackedWidget`) gets only the height the layout grants — it does NOT size
to the tallest tab; (b) `QStyleSheetStyle` does NOT fold styled `QTabWidget::pane` padding
into `sizeHint()` (export dialog uses `::pane { padding: 14px }` = 28px vertical missing).
Computing chrome as `tabs->sizeHint() - maxPage` inherits the same omission — that was the
failed first attempt.

**Working fix** (`VideoExportDialog::refreshDialogGeometry()`):
1. Unpin stale constraints (`setMinimumHeight(0)`, `setMaximumHeight(QWIDGETSIZE_MAX)`),
   measure each page's natural `sizeHint().height()` (activate its layout first), take the
   **max**, floor every page to it (each page's trailing stretch absorbs slack → no overlap).
2. `adjustSize()`, then flush:
   `QCoreApplication::sendPostedEvents(settingsTabs_, QEvent::LayoutRequest);`
3. Size from LIVE geometry, not hints:
   ```cpp
   auto* stack = settingsTabs_->findChild<QStackedWidget*>();
   int tabChrome   = settingsTabs_->height() - stack->height(); // TRUE chrome incl. QSS padding
   int nonTab      = height() - settingsTabs_->height();
   int target      = maxPage + tabChrome + nonTab;              // idempotent
   ```

Commits: 81d755d (introduced tabs, incomplete) → 97aca5f (fix attempt) → 5a96a85
(live-geometry fix that stuck). **3 iterations.**

### W3. Dialog won't widen / right-column CJK labels clipped

**Root cause:** root layout `QLayout::SetFixedSize` locks the dialog to content
`sizeHint()` and **ignores `dialog.setMinimumWidth()`** — but it DOES honor a child
widget's `minimumWidth()`.

```cpp
rootLayout->setSizeConstraint(QLayout::SetFixedSize);
tabWidget->setMinimumWidth(420);   // works — child minimum feeds the fixed-size hint
// dialog.setMinimumWidth(880);    // IGNORED — this was the failed first attempt
```

Two-column CJK checkbox grids need ~150–160px per column; set column stretches equal.
Commits: 3d14241 (failed) → df0829d (fix). **2 iterations.**

**Horizontal pane-squeeze variant (preferences dialog, 2026-06-10):** under
`SetFixedSize`, W2's "QSS pane padding absent from `QTabWidget::sizeHint`" also bites
HORIZONTALLY — every tab page gets laid out ~2×padding narrower than its hint, the
QFormLayout shrinks the field column below the children's needs, and Fixed-width children
(styled menu buttons, trailing hint labels) get clipped at their row-container's right
edge ("右侧边界爆了", trailing text like `Ctrl+Shift+=` losing its tail). A dead
`dialog.setMinimumWidth()` alongside `SetFixedSize` is the tell — remove it when found.

Fix lives on the tab widget (W3), but DON'T pick a static width: reviving the dialog's
old 620 floor got "这也太宽了" rejected the same day (**2 iterations**). Measure instead
(horizontal twin of the W2 vertical recipe — chrome is invariant under the squeeze, so
this is idempotent):
```cpp
int maxPageWidth = 0;   // max over pages of (layout()->activate(); sizeHint().width())
dialog.adjustSize();
QCoreApplication::sendPostedEvents(pageStack, QEvent::LayoutRequest);
auto* stack = pageStack->findChild<QStackedWidget*>();
int tabChrome = (stack != nullptr && pageStack->width() > stack->width())
    ? pageStack->width() - stack->width()
    : 18;  // fallback: 2*8 QSS pane padding + 2*1 border
pageStack->setMinimumWidth(maxPageWidth + tabChrome);
```
Run it AFTER all tabs are added; the dialog then sits exactly at the widest page's
natural width and never re-clips when rows change.

### W4. Styled slider handle clipped

QSS groove 6px + `-4px` handle margins = 14px+, taller than a bare slider's hint.
`slider->setFixedHeight(20);` after applying `dialogSliderStyleSheet()`. Commit: 97aca5f.

### W5. Dark mode unreadable / doesn't update on theme switch

**Root cause:** hardcoded hex colors in QSS, and/or stylesheet applied once in the
constructor with no re-application on theme change.

Recipe (both halves required — fixing only the first was the failed iteration):
1. Colors come from `UiTheme::colors()` inside a `UiTheme::xxxStyleSheet()` factory.
2. Expose `applyThemeStyles()` on the page/dialog; call from `buildUi()` **and** register
   in `MainWindow::WindowSection::applyUiTheme()`. That hook is a hardcoded list — any
   dialog not registered there silently freezes on theme switch.

Checkbox/radio indicators need explicit dark-aware QSS (`darkAwareCheckBoxStyleSheet()`).
Commits: 62a3ae8 (colors only — still froze) → 3bf7b95 (re-apply hook) ; c628156
(indicators); 34a25c0 (UiTheme foundation). **2+ iterations.**

### W6. Dialog taller than screen → bottom clipped by WM

Keep pages top-aligned: header → controls → trailing `addStretch(1)`; compact spacing
(`setSpacing(3)`, margins 4–6). Content pushed down by a *middle* stretch is the first
thing the WM clamp eats. A screen-height cap + scroll was tried and REVERTED (user wants
"tall enough for the tallest tab"). Commits: 81d755d, 97aca5f, 1f8bde1.

### W7. QSS `min-width` overrides `setFixedWidth`

A style sheet's `min-width` beats `setFixedWidth()` from code. Override inline alongside:
```cpp
btn->setStyleSheet(base + " QPushButton { min-width:30px; max-width:30px; padding:0; }");
btn->setFixedSize(30, 30);
```
Seen in the in-flight ExportCoverDialog work (play/pause button), 2026-06.

---

## Q — Qt Quick / QML scenes & rendered assets

### Q1. 1px sliver / seam under fractional scale (1px 曲绘露边)

**Root cause:** each edge snapped independently in card-local coordinates, without folding
in fractional scene origin AND `devicePixelRatio` — edges round different ways and a
~1-device-px sliver of the layer below shows past the stroke.

**Fix — ONE shared snapped rect, snapping in absolute device space:**
```qml
QtObject {
    id: jacketBox
    readonly property real s: geom.cardScale
    readonly property real dpr: root.renderDpr        // Screen.devicePixelRatio
    function snapX(n) { return (Math.round((geom.cardX + n*s) * dpr) / dpr - geom.cardX) / s }
    function snapY(n) { return (Math.round((geom.cardY + n*s) * dpr) / dpr - geom.cardY) / s }
    readonly property real nx: snapX(jb.x);  readonly property real ny: snapY(jb.y)
    readonly property real nx2: snapX(jb.x + jb.w); readonly property real ny2: snapY(jb.y + jb.h)
}
// BOTH the clipped Image and the border Rectangle read jacketBox.* — one truth, no drift.
```
Commits: 14479f6, 4ef9b65.

### Q2. Seam dark bleed / bright rim where sprite halves meet (接缝黑线)

**Root cause:** halves AA'd + LANCZOS-downscaled independently, then over-composited at a
shared cut: composite `α = 1-(1-αA)(1-αB) < 1` leaks the backdrop (dark line); LANCZOS
ringing overshoots just inside the cut (bright rim).

**Fix lives in the asset extractor (Python), not QML:** flatten interior RGB to the plate's
modal color (kills ringing), then harden seam alpha to 255 on whichever half dominates each
seam pixel (kills bleed); only touch the ~2–3px shared edge so silhouettes survive. Verify
with a pixel probe (commit reports 0.0000% leak). Commit: 981bdf8.

### Q3. Blurry / uneven strokes under non-integer scale

`border.width: 2` in card-local units becomes ~2.65 fuzzy device px under scale 1.324.
Fix: `border.width: N / scale` + `antialiasing: false` + edges from Q1 snapping.
**Draw frames as QML strokes, not baked sprite borders** — baked borders scale fuzzy and
need re-baking per change. Commits: 4ef9b65, 14479f6.

### Q4. Sprite transparent padding breaks alignment (素材自带透明边)

Prefab sprites (UI_TST_MBase_*) carry baked transparent rows (e.g. rows 0–56); naive
bbox-to-bbox placement leaves gaps/bumps (frame corners poking above the Tab). **Measure
opaque bounds from the alpha channel first**, then offset layout coords. Asset top-crops
were applied in dab3f11 + 4f9f71d, then REVERTED in 4ef9b65 in favor of QML strokes (Q3)
— prefer code over asset surgery. **3 iterations + revert.**

### Q5. Text overflow in a fixed slot (标题/曲师过长)

Two modes on one gate, because headless export can't tick QML animations:
- Still cover → shrink / `elide`.
- Animated intro → frame-driven marquee: two text copies for seamless wrap, offset
  `((frame - begin) * scrollPxPerFrame) % (textWidth + gap)`, tunables
  (`startHoldFrames`, `scrollPxPerFrame`, `loopGapPx`) in the template JSON.
Commit: fec42d8.

### Q6. Atlas glyph spacing (LV 数字间隙/挤压)

Monospaced atlas cells waste width on narrow glyphs ("1") and AA tails leave visible gaps.
Fix: per-glyph alpha-bounds table → tight `sourceClipRect` per digit → proportional
`width` → negative `Row.spacing` (−1 outer, −2 digit-to-digit) to close AA tails →
center-offset calibrated against a reference image. **Build measurement tools**
(`measure_lv_alignment.py`, `render-lv-samples.ps1`) — alignment converged in 2 rounds
WITH tools vs. blind guessing. Commits: d3392de, 9466496.

---

## Z — stacking, paint order, windows, events

### Z1. Wrong stacking (层叠关系错误)

- **QML:** declaration order = paint order. The intro card's required order is documented:
  frame plate → Tab → clipped jacket → thin frame stroke → LV pill. Comment the intended
  order; insert new layers by moving declarations, not by sprinkling `z:`.
- **Widget `paintEvent`:** draw order is the z-order. Waveform had to move AFTER the grid
  loop to read on top (64183a0) — with a comment saying so. Convention: background → grid
  → content → overlays.
- Text outline+shadow = two passes: filled soft shadow path first, then the rim stroke
  (982feee).
- **Dual-path caveat:** the QSG layer and the widget painter are separate implementations
  (e.g. c2d68de split judge rings only in QSG; DComp diverges by design). State intended
  divergences explicitly, otherwise fix both.

### Z2. Hit area ≠ visual (点击错位)

One canonical source function (e.g. `handleCenterX()` derived from `displayedProgress`);
handle, fill, tooltip AND the hit test all read it. Constrain the interaction band with
explicit anchors+height — `anchors.fill` on an oversized parent makes dead-looking zones
clickable. Commit: e17c598.

### Z3. Platform blue-fill fallback in custom-painted views

`QAbstractScrollArea` viewport default paint fires on style/palette events if the custom
path doesn't consume `QEvent::Paint`. Fix bundle (ff2de48):
```cpp
setAttribute(Qt::WA_OpaquePaintEvent, true);
viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
viewport()->setAutoFillBackground(false);
// viewportEvent(): consume QEvent::Paint → route to custom paint, return true
// + pre-set viewport palette (Window/Base) to the custom background color as belt-and-braces
```

### Z4. Popup cascade-close before a cancellable prompt

`dismissOpenChildPopupDialogs()` ran before the save-changes prompt; Cancel left the world
already mutated (QuickShell native bridge surfaces destroyed). Rule: side-effect sweeps run
only after the confirm returns true; the `event->ignore()` path must be a perfect no-op.
Commit: 8a89959.

### Z5. Keyboard gate by geometry instead of focus

Preview seek armed by "last click inside preview bounds" stole arrows from the editor
(native sub-window overlaps the pane frame). Gate on
`QApplication::focusWidget()` being a text input → pass through; log the passthrough for
verification. Commit: cf53bc3.

---

## Sync-pair constants to keep aligned (UI-layout-relevant)

- `kBottomTabsContentScaleMax = 4.0` — `MainWindow.WindowShell.cpp` +
  `TimelineSceneStateBuilder.cpp` + clamps in `TimelineView.cpp` / `TimelineQuickStateBridge.cpp`.
- Tiered grid heights `kTimelineGridHeightFraction*` (`TimelineThemeConfig.h`) — consumed by
  BOTH `TimelineSceneStateBuilder.cpp` (QSG) and `TimelineView.Paint.cpp` (widget).
- Two-tier content scale: `gridContentScale` (unbounded) vs `normalizedContentScale`
  (capped 1.0) — grid grows past 100%, notes/markers don't (23773b6).
- Pause-hide judge-line gate duplicated in `effectivePreviewOutlineVariant()` and
  `applyPreviewStageMediaRouteVisualSettings()` (0bd14da).

See dev-guide `references/cross-chain-linkage.md` for the authoritative list.

---

## Iteration scoreboard (why the hard rules exist)

| Bug | Iterations | What the failed attempts trusted |
|---|---|---|
| Tab pane clipping | 3 | `sizeHint()` arithmetic under QSS |
| Dialog widening | 2 | `dialog.setMinimumWidth` under `SetFixedSize` |
| Dark-mode latency page | 2 | one-shot stylesheet application |
| Intro frame alignment | 3 + revert | asset bbox = visual bbox; baked borders |
| Slider rows | 2 | guessed two-column widths |
| LV glyph alignment | 2 (with tools) | — converged fast BECAUSE tools measured pixels |

Common thread: the model can't see pixels and Qt's documented-looking defaults
(`sizeHint`, size policies, QSS interactions) are unreliable under styling. Measure live
geometry / alpha bounds, apply the recipe, verify on a render.
