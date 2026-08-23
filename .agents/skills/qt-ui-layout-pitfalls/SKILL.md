---
name: qt-ui-layout-pitfalls
description: Symptom→root-cause playbook for MiaCode UI layout bugs. Use WHENEVER touching dialog/window/QML-scene layout, or when the user reports 边界被吞/被截断/clipped borders, 布局不合理/controls overlap or misaligned, 层叠关系错误/wrong stacking or z-order, 1px缝隙/接缝/sliver/seam, tab 页被裁剪, dark-mode 看不清, 文本溢出, or hit-area mismatch. Distilled from ~40 commits of layout fixes (many took 2-3 iterations); ALWAYS diagnose via the routing table here BEFORE editing — never nudge per-control pixel values.
---

# Qt UI Layout Pitfalls (MiaCode)

History shows the same ~15 layout bugs recur, and first attempts based on intuition fail
~half the time (tab clipping took 3 commits, dialog width 2, dark mode 2, intro frame
alignment 3 + a revert). The cause is a hard capability boundary: **you cannot see rendered
pixels, and several Qt defaults lie** (`sizeHint()` under QSS, `SetFixedSize`,
`QSizePolicy::Fixed`). This skill replaces guessing with proven recipes.

## Hard rules (capability boundary → working method)

1. **Diagnose before editing.** Classify the symptom with the routing table below and apply
   the matching recipe. Per-control pixel nudging is banned — the user has explicitly
   rejected it; every recurring bug here had a structural cause.
2. **Never trust `sizeHint()` on styled widgets.** QSS borders/padding are under-reported.
   Measure: `ensurePolished()` first, then read `sizeHint()`, then `setFixedHeight()`.
   Size containers from **live geometry after `adjustSize()` + flushed `LayoutRequest`**,
   not from hints.
3. **Don't invent pixel numbers — measure them.** For widgets: live geometry. For sprites:
   alpha-channel bounds (assets ship baked transparent padding). For alignment claims:
   build/reuse pixel-diff probes (`measure_lv_alignment.py` pattern, batch render scripts).
4. **Verify by rendering, not by reading code.** A layout change is "probably fixed" until
   confirmed on an actual render/screenshot (or a pixel probe). Say so; ask for a screenshot
   or produce a render sample when one is cheap.
5. **Every visual change has a twin.** Timeline/preview visuals exist in BOTH the widget
   paint path and the QSG path; preview-time and export-time are sync pairs
   (dev-guide `references/cross-chain-linkage.md`). Fix both or document the divergence.
6. **Every new styled dialog/page must register in the theme re-apply hook**
   (`MainWindow::WindowSection::applyUiTheme()`), and take colors from `UiTheme::colors()`
   — hardcoded hex = guaranteed dark-mode bug later.
7. **Headless export has no event loop ticks**: QML `PropertyAnimation`/`Timer` never fire
   during CLI/video export. All export-visible motion must be frame-driven
   (`frame`-indexed math), with a still-mode fallback (e.g. elide instead of marquee).

## Routing table — match the symptom first

| Symptom (zh / en) | Root cause | Recipe |
|---|---|---|
| 按钮/输入框底边被吞 (styled button/edit bottom border clipped) | `QSizePolicy::Fixed` ignores `setMinimumHeight`; QSS `sizeHint` short 1–2px | W1 |
| Tab 页内容被裁剪 / tab 互相叠 (tab page clipped, tabs overlap) | `QTabWidget` pane sizes to *current* tab; QSS pane padding NOT in `sizeHint` | W2 |
| 对话框加宽无效 / CJK 标签被截断 (widen has no effect, CJK labels cut) | `QLayout::SetFixedSize` ignores dialog `setMinimumWidth`; honors CHILD `minimumWidth` | W3 |
| Slider 把手被吞 (styled slider handle clipped) | QSS groove+negative margins taller than bare slider | W4 |
| 深色模式看不清/不更新 (dark mode unreadable / frozen) | Hardcoded colors; styles applied once, never on theme switch | W5 |
| 对话框底部超出屏幕被截 (dialog clamped by screen) | Over-tall fixed dialog; middle stretch pushes content to clipped bottom | W6 |
| 设置宽度被 QSS 覆盖 (`setFixedWidth` ineffective) | QSS `min-width` beats `setFixedWidth` | W7 |
| 下拉弹窗右下角纯色块 (combo popup solid wedge at BR corner ONLY) | default-ctor `QProxyStyle` wraps DESKTOP style (windows11); its popup panel fights the QSS panel at 1px offset anchored top-left | W8 |
| 1px 缝隙/曲绘露边 (1px sliver past a frame) | Per-edge rounding drifts under fractional scale; DPR not folded in | Q1 |
| 接缝黑线/白边 (seam dark bleed / bright rim) | AA'd alpha over-composite at shared cut; LANCZOS ringing | Q2 |
| 边框模糊/粗细不均 (blurry/uneven stroke under scale) | Stroke scales with card; fractional coords | Q3 |
| 素材对不齐/凸起 (sprite misaligned, bump pokes out) | Baked transparent padding in prefab sprites | Q4 |
| 文本溢出卡槽 (text overflows a fixed slot) | No overflow mode; ellipsis loses data; headless can't animate | Q5 |
| 字符间隙/挤压 (glyph gaps or squash from an atlas) | Monospaced atlas cells + AA tails | Q6 |
| 层叠关系错误 (element above/below the wrong thing) | QML: declaration order = paint order; widget: `paintEvent` draw order | Z1 |
| 模态弹窗藏到主窗口下方 / clicks only play the task-dialog warning sound | QuickShell uses a hidden QWidget backend, so parentless application-modal dialogs lack a native owner and can fall behind the visible QQuickWindow | Z8 |
| 点击区域错位 (hit area ≠ visual) | Hit geometry and visuals derived from different sources | Z2 |
| 平台蓝色填充闪现 (platform blue-fill flashes) | Viewport default paint path fires on style/palette events | Z3 |
| 取消关闭但弹窗已没了 (cancel close, popups already gone) | Side-effect sweep ran BEFORE the cancellable prompt | Z4 |
| 方向键被预览劫持 (arrows hijacked from text input) | Geometric "armed" gate without focus-widget check | Z5 |
| 整个编辑列错位且跨页持续 (whole workspace column offset, persists across pages) | `QQuickWidget` under a quick-shell rehosted surface → top-level HWND recreated → foreign-window embed broken | Z6 |
| macOS 嵌入页弹窗与文字错位 (popup displaced from text in a rehosted surface) | QWidget `mapToGlobal()` still uses the orphan NSPanel after its NSView is adopted by QuickShell | Z7 |

Full recipes with code idioms and the commit history behind each: `references/recipes.md`.
The W-patterns are also condensed in user memory `reference-widget-dialog-clipping`.

## Recipe one-liners (open references/recipes.md before applying)

- **W1**: `btn->ensurePolished(); btn->setFixedHeight(qMax(btn->sizeHint().height(), 30) + 4);`
- **W2**: floor every page to max page `sizeHint` → `adjustSize()` →
  `sendPostedEvents(tabs, QEvent::LayoutRequest)` → height = maxPage + live chrome
  (`tabs->height() - stack->height()`) + non-tab height. Never `tabs->sizeHint()`.
- **W3**: put `setMinimumWidth()` on the child (e.g. the `QTabWidget`), not the dialog.
- **W4**: styled percent sliders get `setFixedHeight(20)`.
- **W5**: colors via `UiTheme::colors()` in a `xxxStyleSheet()` factory; expose
  `applyThemeStyles()`; call it from `buildUi()` AND `WindowSection::applyUiTheme()`.
- **W6**: top-align page content (header → controls → trailing `addStretch(1)`), compact
  spacing; do NOT re-level controls when the WM clamps the window.
- **W7**: inline QSS override `"QPushButton { min-width:Npx; max-width:Npx; padding:0; }"`
  alongside `setFixedSize`.
- **W8**: popup proxy styles get an explicit base (`QStyleFactory::create("Fusion")`);
  container made translucent by hand (WState_Created dance) + ONE panel painter via a
  Paint-event filter; QSS view AND popup-scrollbar backgrounds transparent (an opaque view
  bg leaves a see-through hole under the scrollbar column on a layered window).
- **Q1**: ONE shared snap helper in absolute device space —
  `snap(n) = (Math.round((origin + n*s) * dpr) / dpr - origin) / s` — and make the clip
  rect AND the stroke read the same snapped rect.
- **Q2**: fix in the asset pipeline (flatten color, harden seam alpha by dominance), verify
  with a pixel probe; not fixable in QML.
- **Q3**: `border.width: N / scale`, `antialiasing: false`, edges from Q1 snapping. Draw
  frames as QML strokes, NOT baked sprite borders (asset-crop approach was tried 3× and
  reverted — strokes scale, sprites don't).
- **Q4**: measure sprite alpha bounds first; offset layout coords or crop — never assume
  the PNG bbox is the visual bbox.
- **Q5**: dual overflow modes — still render = shrink/elide; animated intro = frame-driven
  marquee (two-copy wrap, `(frame - begin) * pxPerFrame % period`), tunables in template.
- **Q6**: per-glyph alpha bounds table + `sourceClipRect` + small negative `Row.spacing`
  (−1/−2) to close AA tails; calibrate offsets with a measurement script.
- **Z1**: QML stacking = declaration order (document the intended order in a comment);
  widget stacking = `paintEvent` draw order (background → grid → content → overlays).
  When a new layer must sit between existing ones, move the draw call, don't add `z` hacks.
- **Z2**: one canonical position function (e.g. `handleCenterX()`); every visual AND the
  hit test derive from it. Constrain interaction band explicitly (anchors + height), never
  `anchors.fill` an oversized parent.
- **Z3**: override `viewportEvent()` to consume `QEvent::Paint`, set `WA_OpaquePaintEvent`
  on widget + viewport, pre-set viewport palette to the custom background color.
- **Z4**: run destructive UI sweeps (closing child popups etc.) only AFTER the cancellable
  confirm returns true; `event->ignore()` must leave the world untouched.
- **Z5**: keyboard gates need a focus-widget check (`QTextEdit`/`QPlainTextEdit`/`QLineEdit`
  → pass through), not just geometric armed state.
- **Z6**: NO `QQuickWidget`/`QOpenGLWidget` anywhere under a quick-shell rehosted surface
  (texture child → top-level HWND recreate → the `fromWinId` embed dies). Live QML preview
  in widgets = native `QQuickView` + `createWindowContainer` + key-forwarding event filter
  (cf. `IntroPreviewWidget`).
- **Z7**: bind the bridge surface to its adopted `QWindow`, convert child-local coordinates
  to bridge-surface coordinates in the QWidget hierarchy, then call the adopted window's
  `mapToGlobal()`; never compensate with a fixed x/y offset.
- **Z8**: bind ownerless/hidden-owner top-level dialogs' native `QWindow::transientParent`
  to the visible QuickShell root and re-raise only blocking modals on app/root activation.
  Preserve visible owners for nested dialogs and never force-activate non-modal dialogs. Never use
  `WindowStaysOnTopHint`, which would incorrectly place it above other applications.

## Known rejected approaches — do not retry

- QScrollArea-per-tab + screen-height cap for the **modal** export dialog (user wants the
  dialog sized to the tallest tab). Scope note 2026-06-12: the EMBEDDED export-page panel is
  the opposite by product decision — fixed frame (pinned header / tab bar / Start-Export
  footer), tabs at natural height, per-tab vertical-only QScrollArea as a short-window
  fallback, horizontal scrolling forbidden everywhere on the page.
- Two-column slider grids in the export dialog (right column clips; use full-width rows).
- `QFrame::HLine` styled dividers (content rect collapses, paints nothing).
- Unicode emoji-presentation glyphs (⏸ U+23F8) on buttons — Windows renders color emoji,
  ignores theme color. Dingbat fallbacks (❚❚ U+275A) were ALSO rejected (too heavy/wide,
  spacing uncontrollable): **paint a QIcon with QPainter** (theme color, exact bar
  width/gap — cf. `makeTransportIcon` in ExportCoverDialog.cpp, `makeSettingsGearIcon`).
- Baked sprite borders / asset top-crops for the intro card frame (superseded by QML
  stroke + device-pixel snap).
- Raw CJK literals in C++ (mojibake risk) — use `l10n()` / `UiText` map / `QStringLiteral`.

## Maintenance

When a NEW layout bug class is root-caused (not in the table), add a row + recipe here and
detail in `references/recipes.md` in the same change. Keep `miacode-dev-guide` routing
pointed at this skill.
