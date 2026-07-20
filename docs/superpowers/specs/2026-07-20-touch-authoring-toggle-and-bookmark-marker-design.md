# Touch Authoring Toggle and Bookmark Marker Design

## Goal

Refine Ctrl-held touch-area authoring so mouse buttons choose the insertion separator,
repeating an existing ordinary area Touch removes its first occurrence, and bookmark line
numbers retain their color/background cues without an underline.

## Confirmed Interaction

- Authoring remains available only while Ctrl is held in the existing editable context.
- Ctrl+left-click inserts with `/` when the pad is absent.
- Ctrl+right-click inserts with `` ` `` when the pad is absent.
- Shift no longer affects separator selection.
- If the ordinary area Touch already occurs in the caret's comma-delimited token, either mouse
  button removes the first occurrence instead of inserting another one.
- The existing pressed visual, cancellation lifecycle, single undo step, and seek to the
  pre-edit token time minus `1/60s` remain unchanged.

## Token Toggle Semantics

`TouchPadAuthoringEdit` remains the sole owner of the edit plan. It scans the active token as
parallel items separated by `/` or `` ` `` and matches the normalized pad exactly.

- For the first item only, strip its leading `(BPM)` / `{division}` controls for matching; the
  entire remaining item must equal the normalized pad. Exact `A1` therefore matches, while `A10`
  and `A1h[...]` do not.
- Leading timing controls on the first item are preserved: `(120){4}A1` matches A1 and removal
  produces `(120){4}`.
- Only the first matching occurrence is removed.
- For a non-first item, remove its preceding separator with the item.
- For the first item with a following item, remove the item and its following separator.
- Timing controls remain attached to the new first item: `(120){4}A1/B2` becomes `(120){4}B2`.
- For a sole item, remove only the item.
- Other items, their separator types, leading controls, and trailing whitespace are preserved.

The edit plan is extended from insertion-only data to a replacement range plus replacement text,
so insertion and deletion continue to share one `QTextDocument` edit block and one undo step.

## Pointer Routing

`PreviewQuickSceneRoot` accepts both left and right mouse buttons. Press/move/release ownership and
same-pad completion remain unchanged. On release, the button determines the separator request:
left means slash, right means backtick. Accepted right-click authoring is consumed by the scene so
it does not open an unrelated context menu.

`PreviewRuntime` continues to emit the completed pad plus the separator choice; MainWindow remains
the only owner of document mutation and preview seeking.

## Bookmark Marker

`PlainCodeEditor::lineNumberAreaPaintEvent` keeps the bookmark/drop-row background fill and accent
line-number color, but removes the underline drawing. The underline geometry helper is removed if
it has no remaining caller, and its old width tests are replaced by a source/behavior guard that
locks the no-underline design without changing gutter sizing.

## Tests

- Token editing: left/right separator insertion; first/middle/sole removal; mixed separators;
  duplicate pad removes only the first; timing-prefix preservation; Touch Hold and prefix-like pad
  exclusion; trailing whitespace; one-step undo.
- Gesture routing: both left and right buttons are accepted, and Shift is not consulted for the
  separator choice; accepted right press/release is consumed and does not open a context menu.
- Bookmark gutter: bookmark/drop coloring remains while underline rendering/helper is absent.
- Existing timeline resolution and gesture lifecycle specifications remain green.
