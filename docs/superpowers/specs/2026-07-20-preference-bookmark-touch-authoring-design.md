# Preference Persistence, Bookmark Underline, and Touch Authoring Design

Date: 2026-07-20

## Goals

- Persist the last edited cover-export composition as an application preference even when no export is performed.
- Persist the BreakSlide tail-cheer mute switch immediately as an application-level preference without changing the existing local audio-preset semantics for mix levels.
- Repair other confirmed preference round-trip failures found by auditing the same load, normalize, and save paths.
- Draw bookmark underlines to the actual rendered width of the line-number text.
- Replace Alt-coupled touch-area authoring with a Ctrl-held hover and click interaction, add a pressed visual state, perform caret-token-aware insertion, and seek preview to the token time minus one frame at 60 fps.

## Non-goals

- Redesigning the full preference schema or replacing `QSettings`/JSON persistence.
- Changing video-export dialogs whose established behavior is to remember values only after an export action.
- Changing local audio preset semantics for volumes, offsets, or mix controls.
- Replacing the canonical touch-pad geometry or judge-area coordinate system.

## Architecture

The change is split into four focused units.

### Cover composition persistence

`CoverStudioPanel` continues to load `app.cover_export` on construction. Its destructor is the single close-time persistence point and calls a content-idempotent `persistCompositionPreferences()` helper before detaching the live scene. The existing export path may call the same helper, but there is no separate close-event write. The helper skips only a payload identical to the last successfully persisted composition; editing after an export makes the close-time payload different and therefore saves again. Closing the editor therefore saves the user's actual last edited state independently of export success.

Persistence is performed while all composition widgets and models are still alive. Repeated widget changes are not written individually, avoiding disk writes for every slider movement. `CoverCompositionState::savePreferences()` is changed to return the underlying save result. A failed close-time write is logged through the existing diagnostics channel and does not block destruction.

### Audio and extension preferences

The canonical BreakSlide application preference is the boolean `app.preview.break_slide_tail_cheer_muted`, separate from the local mix preset in `app.preview.audio`.

Loading follows this order:

1. Read `app.preview.audio` as the local mix preset.
2. Read canonical `app.preview.break_slide_tail_cheer_muted` when present.
3. For migration only, fall back to `app.preview.audio.break_slide_tail_cheer_muted`, then the legacy top-level key.
4. Overlay the resolved application value onto both the live settings and the in-memory local-preset mirror.

The checkbox immediately updates the canonical boolean, live settings, and local-preset mirror before portable state is saved. Applying a local preset copies mix values and then reapplies the canonical boolean. Saving a local preset copies live mix values but writes the canonical boolean into the compatibility mirror. `app.preview.audio.break_slide_tail_cheer_muted` remains a compatibility mirror during this schema version; it is never authoritative when the sibling canonical key exists. Other audio values remain controlled by the explicit local-preset action.

Preference normalization starts from the raw object, preserves every non-built-in top-level key, writes normalized `schema`, `ui`, `app`, and `extensions` containers over it, and removes the complete set of recognized legacy built-in top-level keys after migrating them. In particular, when `ui.theme` is absent, top-level `theme` is migrated after `ui_theme`; when both exist, `ui.theme` wins. Top-level `theme` is then removed. Extension shortcut registration therefore survives a save/load round trip. Extension `theme/getCurrent`, `theme/setCurrent`, and the theme returned by `app/getInfo` all read or write canonical `ui.theme`.

The audit checks every existing preference entry point for:

- symmetric load/save keys;
- values discarded by normalization;
- accidental coupling between current values and explicit presets;
- close/cancel behavior inconsistent with the feature's documented persistence semantics;
- duplicated legacy values after migration.

The audited entry points and outcomes are:

- `CoverCompositionState` / `CoverStudioPanel`: confirmed close-without-export loss; fixed.
- `MainWindow::loadPortableState()` / `savePortableState()` audio: confirmed BreakSlide current/preset ownership bug; fixed with the canonical sibling key.
- `UiText::normalizedPreferencesRoot()`: confirmed unknown extension-key loss; fixed.
- `ExtensionManager` settings and shortcut APIs: affected by the same normalizer loss; covered by the normalizer fix.
- `ExtensionManager` theme APIs and `app/getInfo`: confirmed key asymmetry (`theme` versus `ui.theme`); fixed directly.
- Main-window UI, preview HUD, disabled extensions, batch-download folders, latency SFX, and QSettings-backed browse folders: load/save paths are symmetric; no change.
- Video-export and other action dialogs that document export/action-time persistence: intentional; no close-time behavior change.

Only the confirmed defects above are changed. Existing action-triggered persistence semantics are retained where intentional.

### Bookmark underline geometry

The line-number gutter keeps its current font, padding, and right-aligned text layout. The underline right edge is the right edge of the existing text rectangle; its left edge is that value minus `QFontMetrics::horizontalAdvance(number)`. This uses the exact same font metrics as painting, so proportional fonts, high-DPI scaling, and any digit count remain aligned.

The calculation is factored into a small testable geometry helper used by painting code.

### Touch-area authoring

Touch-area authoring receives its own fixed Ctrl-held activation state and no longer reads the Alt pause-display state. It is enabled only when the existing authoring preference is on, a chart difficulty is active, the visible editor page is that chart's writable editor, export preview is inactive, and no modal or popup is active. Embedded and fullscreen preview are both allowed. Activating it during playback is allowed; a successful authoring click uses the discrete seek path and therefore pauses playback at the requested time.

The pause-display hold shortcut remains rebindable. If it is bound to bare Ctrl while touch authoring is enabled in an editable context, touch authoring has priority and the pause-display inversion is not activated for that key press. Outside an authoring context, the rebound pause-display shortcut keeps its normal behavior.

`PreviewRuntime` is the single gesture coordinator. It owns hovered and pressed pad state and exposes idempotent begin, move/cancel, and finish operations. `PreviewQuickSceneRoot` is the exclusive pointer-phase owner and forwards hover, press, release, and ungrab to the coordinator; it never edits text. The native `QWindow` filter no longer handles or consumes authoring mouse press/release. It only tracks Ctrl and observes application/host/fullscreen lifecycle transitions that must cancel an active gesture.

QML accepts the authoring press and is the exclusive Qt Quick mouse grabber until release. `mouseReleaseEvent`, `mouseUngrabEvent`, Ctrl release, application deactivation, preview-host replacement, and fullscreen transitions all finish or cancel through the same coordinator. Finish clears pressed state before emitting, so reentrant or duplicated finish attempts are no-ops. The coordinator emits one successful command containing the pad and the Shift state captured at release. One MainWindow connection owns text mutation and preview seeking.

The interaction state machine is:

1. Ctrl press enables authoring and hover hit-testing.
2. Moving over a valid touch area displays its canonical judge-area circle.
3. Mouse press records the pressed area and uses a darker fill/stroke state.
4. Moving away cancels the pressed area; the newly hovered area receives only hover styling.
5. Mouse release over the same area invokes the central authoring command once.
6. Ctrl release, window deactivation, pointer leave, modal interruption, or context loss clears hover and press state.

Shift affects only the insertion separator and does not alter hit-testing.

The visual overlay and hit-testing continue to use the same canonical touch-pad coordinates and radii. No per-control pixel offsets are introduced.

## Caret-token insertion

Before editing, the command uses `QTextCursor::position()` (the active end of a selection), collapses any selection without deleting it, and locates the comma-delimited token at that position. A caret immediately before a comma belongs to the left token; a caret immediately after it belongs to the right token. Token boundaries are otherwise the closest comma before the caret and the closest comma at or after it, or document boundaries.

Whitespace-only tokens are empty and receive the area number at the token start, leaving their whitespace after the inserted number. For non-empty tokens, trailing whitespace is preserved and the new content is inserted immediately before that whitespace.

- Empty token: insert the area number.
- Non-empty token with Ctrl-click: append `/` and the area number.
- Non-empty token with Ctrl-Shift-click: append a backtick and the area number.

The edit is grouped as one undo operation. The time anchor is the token start position immediately after its preceding comma (or document start), captured before mutation.

`TimelineQuickModel` gains a boolean-returning cursor-time resolver so a valid zero-second mapping can be distinguished from failure. After insertion, the discrete synchronized preview-seek path pauses playback and moves to:

`max(0, tokenTime - 1.0 / 60.0)`

If the current document cannot map the token anchor to a valid time, text insertion still succeeds and preview time remains unchanged.

## Error and lifecycle handling

- Cover persistence failures are logged and do not block dialog closure; other preference saves retain their current return-value handling.
- Touch hover/press state is cleared on focus loss and modifier release so no pressed visual can become stuck.
- A press must be released over the same touch area to perform an edit.
- Text is not changed when there is no active chart difficulty, the chart editor page is hidden, the editor is unavailable/read-only, export preview is active, or a modal/popup owns interaction.
- Existing Alt pause-display behavior remains intact and independent.

## Test strategy

Regression tests are written before implementation where practical.

### Preferences

- Cover state survives closing and reopening without an export.
- BreakSlide cheer mute persists immediately and is not reverted by applying the local audio preset.
- Extension custom keys and registered shortcuts survive normalization and a save/load round trip.
- Theme extension APIs and `app/getInfo` use `ui.theme`; top-level `theme` migrates only when canonical theme is absent and is then removed with other legacy built-in fields.

### Bookmark geometry

- Underline bounds match the rendered width of representative one-, two-, three-, and five-digit line numbers using a proportional font.
- Right alignment, gutter padding, and high-DPI-independent logical geometry remain unchanged.

### Authoring text and time

- Empty tokens, whitespace-only tokens, non-empty tokens, Ctrl-Shift separator selection, trailing whitespace, consecutive commas, document start/end, caret immediately before/after a comma, and an active selection whose position differs from its anchor.
- One user action produces one undo step.
- Preview discretely seeks one sixtieth of a second before the mapped token-start time, clamps at zero, and pauses active playback.
- Invalid timeline mapping leaves preview time unchanged.

### Interaction

- Ctrl activates authoring while Alt no longer does.
- Hover, press-darkening, move-away cancellation, same-area release, ungrab, host/fullscreen transition, modifier release, and focus-loss cleanup.
- One physical click produces one edit.
- Existing Alt pause-display interaction remains unchanged.
- A pause-display shortcut rebound to bare Ctrl yields to authoring only in the defined authoring context.

## Verification

Use the repository's Release-only concurrent build workflow, run all directly affected specification targets, and run the existing relevant editor/preview preference regression suite. Manually inspect preference JSON round trips and the QML overlay state if automated coverage cannot observe a rendering detail reliably.
