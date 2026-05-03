# Chart Diagnostics & Normalization Spec

This document organises three related but distinct subsystems that all
operate on the simai chart text and surface results in the bottom-tabs
panel of the editor:

1. **Grammar / syntax detection** — surfaced under the "语法检查 / Syntax
   Check" tab. Catches malformed simai constructs.
2. **Irrational pattern detection** — surfaced under the "无理检查 /
   Muri Diagnostics" tab. Catches hand-impossible playable patterns.
3. **Chart normalization (simplification & reduction)** — invoked from
   the "调整 / Modify" menu. Rewrites the chart text into a canonical
   form.

Treat this doc as the navigation index for the more detailed specs. Where
a subsystem already has a deep spec the references are listed in-line; the
content here is the contract surface plus design decisions that applies
across all three.

## 0. Common ground

All three subsystems operate **on the same simai text** that the editor
holds. They differ in how aggressively they parse:

| Subsystem | Parser pass | Behaviour on syntax error |
|---|---|---|
| Grammar detection | strict | Reports the error; still parses what it can |
| Muri detection | lenient | Skips broken segments, analyses the rest |
| Normalization | lenient | Aborts with `errorMessage` if structurally unparseable |

The shared simai parser lives at `src/core/chart/parser/SimaiNativeParser.{h,cpp}`,
with split implementation files for tap, touch, slide, hold, and strict-format
checks. Diagnostics are emitted into the public `SimaiNativeMessage` struct
(line, col, endCol, message) consumed by all three subsystems.

The bottom-tabs UI host is `BottomTabsQuickHost.qml`; per-tab visibility
is gated on `controller.<tabName>TabVisible` properties exposed by
`QuickShellController`.

---

## 1. Grammar detection — "语法检查 / Syntax Check"

Pure text-shape validation. Runs on every editor edit; results render
into a list of error/warning rows below the timeline.

### 1.1 Two parsing modes

The simai parser exposes a single `parseInternal()` core, toggled by a
`strictMode` boolean to produce two semantically distinct passes:

| Mode | Public API | `strictMode` | Used for |
|---|---|---|---|
| **Lenient** | `SimaiNativeParser::parseForTimeline()` (`SimaiNativeParser.h:59`) | `false` | Live timeline marker extraction; runs on every editor edit |
| **Strict** | `SimaiNativeParser::validateSyntax()` (`:62`) | `true` | The "Syntax Check" tab list; runs through `buildValidationReport()` |

Both passes share the same parser code and produce a
`SimaiNativeParseResult { ok, errors, warnings, measureLineSeconds,
beatMarkers, noteMarkers, durationSeconds }`. The difference lives in
*which* checks the parser activates:

#### Lenient pass (`strictMode = false`)

Goal: **always extract a usable marker set, even if the text is
imperfect.** Reports an error only when a token cannot produce a
marker.

Lenient errors / failure points:

- Bracket / structural failures that prevent further parsing
  (unmatched `[ ]`, `( )`, `{ }`; unterminated `(BPM)` / `{N}` / `[HS*]`
  block — `SimaiNativeParser.Driver.cpp` lines 521, 543, 584).
- Invalid lane / ring / pad token (rejected by
  `MuriConfig.h::padTokenIsValid()` — rings `A..E`, lanes `1..8`,
  center `C`).
- Malformed slide / wifi path or `[N:M]` timing fraction
  (`SimaiNativeParser.Slide.cpp`).
- Malformed tap / touch / hold tokens
  (`SimaiNativeParser.TouchTap.cpp`).
- Invalid `(BPM)` / `{N}` numeric value (e.g. zero, negative, NaN).

Lenient *does not* care about: modifier ordering, `{N}` divisor
compatibility with 384, repeated separator runs, time-signature
placement style. Those are strict-only. A lenient parse with `errors
== 0` means "the chart text was structurally well-formed enough to
build the timeline."

#### Strict pass (`strictMode = true`)

Goal: **catch *every* deviation from canonical simai form,** including
things the lenient pass would silently accept. Adds the following
checks on top of all lenient checks:

Strict-only checks, audited from source. The strict pass output is
authoritative — there is no longer a merge step, so the severity column
below is exactly the severity displayed in the UI.

| # | Strict-only check | Severity | Source | Example |
|---|---|---|---|---|
| 1 | `{N}` divisor of 384 | Warning | `Driver.cpp:557-565` `(384 % beats) != 0` (only when `beats <= 384`) | `{7}` — 7 ∤ 384, parses but flagged |
| 2 | `{N}` clamped value warning | Warning | `Driver.cpp:550-564` `formatBeatValueClamped(beats)` (only when `beats > 384`) | `{1024}` clamped, warning emitted |
| 3 | Unterminated `[HS*…>` block | Error | `Driver.cpp:582-587` `kUnterminatedHsBlock()` | `[HS*x` without closing `>`. **Lenient mode silently `break`s** instead of erroring. |
| 4 | Repeated `/` separator | Error | `Driver.cpp:593-597` `kRepeatedSlashSeparator()` | `1//5` |
| 5 | Repeated `` ` `` separator | Error | `Driver.cpp:604-611` `kRepeatedBacktickSeparator()` | `` 1``5 `` |
| 6 | Non-canonical hold modifier placement (touch_hold) | Error | `TouchTap.cpp:59-66` `kNonCanonicalHoldModifierPlacementPrefix` | `Ah[4:1]b` — touch with `[duration]` AND non-empty suffix modifier after `]` |
| 7 | Non-canonical hold modifier placement (tap/hold) | Error | `TouchTap.cpp:157-164` `kNonCanonicalHoldModifierPlacementPrefix` | `1h[4:1]x` — hold with `[duration]` AND non-empty suffix modifier after `]` |
| 8 | Invalid break-slide `b` modifier position | Error | `Slide.cpp:69-77` `kInvalidBreakSlideModifierPositionPrefix` | `b` not immediately before the first `[` in a slide token |
| 9 | Invalid slide duration placement | Error | `Slide.cpp:113-115` `kInvalidSlideDurationPlacementPrefix` | Slide token with more than one `[…]` block |
| 10 | Multi-segment slide with per-segment wait+duration | Error (via parse failure → `classifyInvalidNoteMessage`) | `SimaiNativeParser.cpp:1206` strict returns `false` from `parseStandardSlideChain` | Multi-shape chain with per-shape `[wait#duration]` markers — strict rejects, lenient distributes |
| 11 | Missing beat separator `,` | Error | `StrictChecks.cpp:119-123` `runStrictFormatChecks` | A note-bearing line that contains no `,` and isn't the terminal `E` line |
| 12 | Unmatched closing bracket | Error | `StrictChecks.cpp:131-137` (within `runStrictFormatChecks`) | `]` without matching `[`; `}` without `{`; `)` without `(` |
| 13 | Unclosed bracket | Error | `StrictChecks.cpp:142-145` (within `runStrictFormatChecks`) | `[` / `{` / `(` left on the stack at end of file |

The `runStrictFormatChecks(state, lines)` pass at `Driver.cpp:652` runs
**after** the main parse loop. It strips control blocks `( … )`, `{ … }`,
`<HS* … >` and full-line `||`-prefixed comments (`StrictChecks.cpp:101-115`)
before checking, so commented-out content does not trigger the
strict-only checks #11–#13.

**Lenient-only behavior** (relaxations the strict pass doesn't apply):

| Lenient relaxation | Source | Behavior |
|---|---|---|
| Touch token `C1` / `C2` → `C` normalization | `TouchTap.cpp:13-18` | Lenient silently rewrites `C1`/`C2` to `C` before validation. Strict skips this rewrite, so the token reaches `parseTouchSuffix` unchanged and almost certainly produces an `Invalid touch token` error. |
| Multi-segment slide chain with per-segment timing | `SimaiNativeParser.cpp:1209-1213` | Lenient distributes total duration across shapes by relative shape length; strict refuses (check #10 above). |
| Unterminated `[HS*` block | `Driver.cpp:582-587` | Lenient `break`s the line silently; strict reports check #3. |

### Modifier-letter ordering — explicitly NOT a strict check

The parser **does not enforce a canonical order on `b / x / h / f`**.
`parseTapModifierSequence` (`SimaiNativeParser.cpp:90+`) iterates the
modifier characters and only rejects **duplicates**. So all of
`1bx`, `1xb`, `1xh`, `1hx`, `1bhxf`, `1fxhb` parse identically.

The Chinese display string "Hold 修饰符顺序无效" historically reads as
"modifier order invalid," but the underlying English message
`Invalid hold modifier sequence: <token>` actually fires for two
distinct conditions, **both of which are mode-independent (lenient AND
strict)**:

| Condition | Source | Example |
|---|---|---|
| Duplicate modifier in prefix or suffix | `parseTapModifierSequence` returns `false` → `TouchTap.cpp:126` | `1bb`, `1xx`, `1hh` |
| `h` modifier appears in the suffix after `]` | `TouchTap.cpp:119-122` | `1[4:1]h`, `1x[4:1]h` |

The strict-only Warning `kNonCanonicalHoldModifierPlacementPrefix`
(checks #6 / #7) is a **separate condition** about modifier *placement*
relative to a `[duration]` block, not about modifier *ordering*. It
fires only when the `[duration]` block exists AND there is a non-empty
suffix modifier string after `]`.

Strict additionally invokes the `StrictChecks.cpp` verifier suite,
included via `#include "SimaiNativeParser.StrictChecks.cpp"` at the
end of `SimaiNativeParser.cpp:1487`.

### 1.2 The validation report — `buildValidationReport()`

`buildValidationReport()` (`SimaiNativeParser.Driver.cpp:971-1042`)
produces the report consumed by the "Syntax Check" tab. Output type:

```cpp
struct SimaiNativeValidationReport {
    bool ok;
    int errorCount;
    int warningCount;
    int lenientNoteCount;       // legacy field; always 0 in the new pipeline
    int lenientErrorCount;      // legacy field; always 0 in the new pipeline
    int strictNoteCount;
    int strictErrorCount;
    QVector<SimaiNativeValidationIssue> issues;
};
```

The build logic is now strict-only:

1. **Empty-chart shortcut.** If `text.trimmed().isEmpty()`, emit a single
   `Error` issue with `kChartEmpty()` raw message and return.
2. **Strict pass.** Call `validateSyntax()` (which is `parseInternal()`
   with `strictMode = true`).
3. **Issue copy-through.** Each strict *error* becomes a UI `Error`
   issue. Each strict *warning* becomes a UI `Warning` issue. **No
   merge, no downgrade, no exception list** — the strict pass severity
   is the final severity.
4. `report.ok = (report.errorCount == 0)`.

#### Decoupling from the lenient pass

The lenient pass (`parseForTimeline`) is **no longer consulted** when
building the validation report. It exists exclusively to extract a
usable timeline marker set for chart preview rendering — that is its
only job. Grammar diagnostics never look at it.

This decoupling is reflected in the report struct:

- `lenientNoteCount` and `lenientErrorCount` are kept for ABI
  compatibility (existing `MainWindow::ValidationEntry` consumers
  reference them) but are populated as `0`. They no longer carry
  meaning.
- The legacy `lenientResult` parameter on `buildValidationReport()`
  remains in the signature for source-compatibility with cached-result
  call sites (`ValidationRuntime.cpp` passes a cached lenient result
  to avoid re-parsing). The implementation marks it `Q_UNUSED`.
- The previously documented `shouldRemainValidationError()` exception
  list and the `makeValidationMessageKey()` helper have been removed
  outright — they were only meaningful in service of the merge step.

What this means in practice:

- "What does the editor render on the timeline?" → answered by the
  lenient pass alone.
- "Is this canonical simai?" → answered by the strict pass alone.
- The two questions never influence each other.

#### Severity outcomes (post-decoupling)

The "Lenient raw" column has been removed because it no longer
participates. The strict severity is the UI severity.

| Input | UI severity | Strict-pass source |
|---|---|---|
| `1[4:1` (unclosed bracket) | **Error** | `kUnclosedBracketPrefix` from `runStrictFormatChecks` |
| `]` (unmatched closing bracket) | **Error** | `kUnmatchedClosingBracketPrefix` from `runStrictFormatChecks` |
| `1//5` (repeated `/`) | **Error** | `kRepeatedSlashSeparator` |
| `` 1``5 `` (repeated `` ` ``) | **Error** | `kRepeatedBacktickSeparator` |
| `1bb` (duplicate `b` modifier) | **Error** | `parseTapModifierSequence` returns `false` (mode-independent) |
| `1[4:1]h` (`h` after duration block) | **Error** | `TouchTap.cpp:119-122` (mode-independent) |
| `1h[4:1]x` (tap/hold non-canonical modifier placement) | **Error** *(flipped from Warning)* | `kNonCanonicalHoldModifierPlacementPrefix` (`TouchTap.cpp:157-164`) |
| `Ah[4:1]b` (touch_hold non-canonical modifier placement) | **Error** *(flipped from Warning)* | `kNonCanonicalHoldModifierPlacementPrefix` (`TouchTap.cpp:59-66`) |
| `{7}1,2,3,` (7 ∤ 384) | **Warning** *(flipped from Error)* | `formatStrictBeatValue` (`Driver.cpp:557-565`) |
| `{1024}1,2,…` (beats clamped) | **Warning** | `formatBeatValueClamped` |
| `[HS*xyz` (no closing `>`) | **Error** | `kUnterminatedHsBlock` |
| `1abc,2,3,` (note-line missing comma somewhere) | **Error** | `Missing beat separator ','` from `runStrictFormatChecks` |
| `1xh` / `1hx` / `1bxhf` / `1fxhb` | **Not flagged** | `parseTapModifierSequence` is order-agnostic |
| Touch `C1` | **Error** | Strict skips the lenient `C1`/`C2` → `C` rewrite, so `parseTouchSuffix` rejects (`Invalid touch token: C1`). The lenient pipeline still renders this as a `C` touch on the preview timeline — the two pipelines differ silently here, by design. |

### 1.3 Locale-aware display messages

`buildValidationReport(text, locale, ...)` accepts
`SimaiNativeValidationLocale::English` or `Chinese`. The raw
`message` from the parser is mapped through:

- `ValidationMessage::zhExactMap()` (Driver.cpp:230) — exact-match
  Chinese translations.
- `ValidationMessage::zhPrefixMap()` (Driver.cpp:243) — prefix-based
  Chinese translations for messages with variable suffix.

Format: `[ERROR] <localized detail>` or `[WARNING] <localized detail>`.

### 1.4 Hidden runtime toggle — invalid-star preview

`SimaiNativeParser::setInvalidStarPreviewEnabled(bool)` /
`invalidStarPreviewEnabled()` (`SimaiNativeParser.h:65-66`,
Driver.cpp:976-984) gates a debug-only preview path that lets the
user see what an "invalid" star slide would look like during chart
authoring. Only `parseForTimeline` honors it (via the third arg to
`parseInternal`); `validateSyntax` always passes `false`. This flag
is settings-driven, not part of the public spec, but worth knowing
when reasoning about marker count drift between modes.

### 1.5 Issue presentation

Each issue is `SimaiNativeValidationIssue` (header `:36-43`):

```cpp
struct SimaiNativeValidationIssue {
    int line;
    int col;
    int endCol;
    SimaiNativeValidationSeverity severity;  // Error | Warning
    QString rawMessage;
    QString displayMessage;     // localized, includes "[ERROR]"/"[WARNING]" prefix
};
```

Surface in the UI:

- **Panel list**: one row per issue, `[Lline Ccol]` prefix + display
  text.
- **Editor extra-selection**: red squiggle / amber underline overlay
  at `(line, col..endCol)` (driven by `refreshEditorExtraSelections`).
- **Header-level ignore**: an issue-type key on the entry lets the
  chart author add `||#ignore <key>` at the top of the file to
  suppress one category project-wide.

### 1.6 What syntax detection does NOT do

- Does not flag muri / playability concerns (those go to subsystem 2).
- Does not auto-fix; rewrites are subsystem 3's job.
- Does not block parsing — `parseForTimeline()` runs regardless of
  strict errors so the user can continue editing while seeing strict
  warnings in the panel.

---

## 2. Irrational pattern detection — "无理检查 / Muri Diagnostics"

Detects **hand-impossible** patterns. Operates on the parsed timeline
markers, not raw text. Two analysis modes — runtime (180-TPS hand-action
simulation) and static (geometry + timing thresholds) — merged into a
single panel.

### Entry points

| Surface | File / symbol |
|---|---|
| Runtime analyzer | `MuriAnalyzer::analyze()` — `src/tools/muri/MuriAnalyzer.h` |
| Static analyzer | `miacode::muri::buildStaticMuriReferences()` — `src/tools/muri/MuriStaticChecker.h` |
| Panel merger | `miacode::muri::buildVisibleMuriPanelEntries()` — `src/tools/muri/MuriPanelEntries.h` |
| Detection enum | `MuriKind` — `src/common/MuriTypes.h:16-22` |
| Render-time options | `MuriRenderOptions` — `src/common/MuriRenderOptions.h` |
| Static threshold config | `kStaticTapOnSlideThresholdDefaultMs` — `src/common/MuriConfig.h` |
| UI host | `MainWindow::ValidationSection` (shared with grammar) — same `.Validation*.cpp` |
| Bottom-tabs gate | `controller.muriTabVisible` |

### Detection categories (`MuriKind`)

| Kind | Chinese label | Detected condition |
|---|---|---|
| `SlideTooFast` | — | Slide / wifi final completion outside the critical judge window |
| `SlideHeadTap` | 外无 | Slide / wifi head pre-judges a subsequent tap / hold / star |
| `TapOnSlide` | 撞尾 | Slide / wifi tail or path collides with a subsequent tap / hold / star |
| `Overlap` | 叠键 | Two simultaneous press events on the same pad |
| `MultiTouch` | 多押 | Required hand count exceeds 2 (runtime mode only) |

Each kind maps to a `MuriAlertLevel` (`Muri` hard fail / `Warning` soft).
The mapping rules — six conditional rules total — are catalogued in the
existing `docs/MURI_DETECTION_SPEC.md` along with the 180-TPS shared
constants and panel merge / dedup rules.

### Static vs runtime split

| Aspect | Runtime (`MuriAnalyzer::analyze`) | Static (`buildStaticMuriReferences`) |
|---|---|---|
| Input | Live markers + actual judge timing | Markers + timing thresholds |
| `MultiTouch` detected? | Yes | No |
| Configurable threshold | n/a | `staticTapOnSlideThresholdMs` (150–250 ms, default 200) |
| Slide-head extra-pad-down window | `slideTraceSecond + min(50ms, first-area-enter)` | Same constant |
| Output | `MuriPanelEntry` list with markers cross-referenced | Static reference list, merged into runtime panel |

### Render-time options affecting detection / display

`MuriRenderOptions`:

- `renderMode` — `Native` vs `MaimuriDxStyle` (visual variant)
- `showSlideTracks`, `showJudgeMarkers`, `showTouchTrail` — visibility toggles
- `wifiNeedC` — whether wifi geometry rule requires center-C
- `excludeTouchFromMultiTouch` — filter touch events out of hand-count

User-configurable static threshold reachable via Validation menu →
`onEditStaticTapOnSlideThreshold()` (`MainWindow.ValidationSection.h:63`),
range `[kStaticTapOnSlideThresholdMinMs, kStaticTapOnSlideThresholdMaxMs]`.

### Existing detail spec

`docs/MURI_DETECTION_SPEC.md` is the authoritative reference for this
subsystem (covers all six alert-level rules, 180-TPS basis, panel
merge/dedup, jump-to-location semantics). Test coverage:
`docs/MURI_DETECTION_TEST_CHECKLIST.md`.

---

## 3. Chart normalization — "调整 / Modify" menu

Rewrites the chart text into a canonical form. **Never invoked
implicitly** — always user-triggered via menu, with an undoable single
edit block. Two orthogonal options can be combined.

### Entry points

| Surface | File / symbol |
|---|---|
| Public API | `normalizeChartText()`, `normalizeChartSelectionText()` — `src/core/chart/transform/ChartNormalization.h:36,41` |
| Options struct | `ChartNormalizationOptions` — `:10-13` |
| Result struct | `ChartNormalizationResult` (`ok`, `text`, `errorMessage`, `changedCount`, `measureLineCount`) |
| Menu invocation | `MainWindow::DocumentSection::onNormalizeWholeChart()` — `src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp:311+` |
| Batch transform tool | `src/tools/chart_transform/ChartBatchTransformSpec.cpp` |
| Preferences | `chartNormalizeStartAtNewMeasurePreferenceKey`, `chartNormalizeReduceTo384GridPreferenceKey` |

### Strategy options

| Option | Default | Effect |
|---|---|---|
| `startAtNewMeasure` | true | Treats the selection start as a measure boundary; rewrites preceding partial-measure context |
| `reduceTo384Grid` | true | Snaps non-384 subdivisions to the nearest 384-grid position |

The dialog (`MainWindow.DocumentTransforms.cpp:72-86`) shows two
checkboxes labelled:

- "选区起点视作小节线开始 / Treat selection start as measure boundary"
- "统一近似至384分音 / Snap approximately to 384 grid"

Choices persist across sessions via the editor preferences JSON.

### Algorithm character

- **Parser-free token-level transform.** Works on the chart text rather
  than on a parsed AST so comments, whitespace, and chart-author
  conventions stay attached to the right tokens.
- **One measure per line.** Output reformats each measure onto its own
  line, prefixing the `{N}` subdivision declaration.
- **Time-signature aware.** Reads `SimaiTimingMetadata` for whole-chart
  time-signature; respects inline `|| x/y` overrides which truncate the
  current measure and restart the grid basis.
- **BPM-triggered grid restart.** A `(BPM)` line restarts the
  subdivision basis at that point.
- **Modifier order canonicalised.** Modifiers within a single note are
  re-ordered to canonical `b x h f` per the
  `SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md` decision.

### Error / abort behaviour

If the input text cannot be re-tokenised (e.g. an unterminated bracket
group spanning multiple measures), `normalizeChartText()` returns
`{ ok: false, errorMessage: "..." }` and the editor leaves the document
untouched. No partial rewrite is ever committed.

### Existing detail spec

`docs/SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md` carries the full
design rationale: 10 final decisions, 9 architecture findings,
preconditions, output rules, blank-line policy, modifier-order
specification, and the 5-phase implementation split.

---

## 4. Cross-cutting policies

### Where each subsystem reads from

```
                     ┌────────────────────────┐
                     │  Editor text (live)    │
                     └───────────┬────────────┘
                                 │
                ┌────────────────┼────────────────────┐
                │                │                    │
        ┌───────▼─────┐  ┌───────▼──────┐    ┌────────▼────────┐
        │ Grammar     │  │ Lenient      │    │ Token-level     │
        │ strict pass │  │ parser →     │    │ rewrite         │
        │ → list      │  │ markers →    │    │ (Modify menu    │
        │             │  │ Muri analyze │    │  only)          │
        │             │  │ → list       │    │                 │
        └─────────────┘  └──────────────┘    └─────────────────┘
                │                │                    │
                ▼                ▼                    ▼
        语法检查 tab      无理检查 tab           writes back to
        + extra-sel       + jump-to-marker        editor in a
                                                  single undo block
```

### Issue-key conventions

Both grammar and muri lists use a stable string `key` per issue type so
the user can:
1. Suppress all issues of one kind via header `||#ignore <key>` line.
2. Click a panel row to jump the editor caret to `(line, col)`.

### Threshold / config touchpoints

If a future change adds a new kind to any of the three subsystems, the
following must be updated together:

| Layer | What to add |
|---|---|
| Detection enum | New `MuriKind` / new validation rule case |
| Display strings | `displayMessage` localizations (zh + en) |
| Issue-key | A unique stable string for `||#ignore` parsing |
| Spec doc | Append to `MURI_DETECTION_SPEC.md`, this index, or a new SPEC if it's its own subsystem |
| Test checklist | Coverage row in `MURI_DETECTION_TEST_CHECKLIST.md` |

### What this index doc deliberately does NOT cover

- The simai parser internals — covered by source-level documentation in
  `src/core/chart/parser/SimaiNativeParser.h` and the per-feature `.cpp`
  splits.
- The muri detection alert-level rules — covered by `MURI_DETECTION_SPEC.md`.
- The normalization design rationale — covered by
  `SIMAI_NORMALIZATION_TIME_SIGNATURE_RESEARCH.md`.

This doc is the navigation index. Detail specs are the source of truth.
