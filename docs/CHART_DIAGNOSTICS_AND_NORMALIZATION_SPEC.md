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

| Strict-only check | Source | Example |
|---|---|---|
| `{N}` divisor of 384 | `SimaiNativeParser.Driver.cpp:565` `(384 % beats) != 0` | `{7}` rejected (7 ∤ 384); `{16}` accepted |
| Beat value clamping warning | `:561` `formatBeatValueClamped(beats)` | `{1024}` clamped to a representable maximum |
| Repeated `/` separator | `:593-595` `kRepeatedSlashSeparator()` | `1//5` flagged |
| Repeated `` ` `` separator | `:604-606` `kRepeatedBacktickSeparator()` | `` 1``5 `` flagged |
| Modifier canonical order `b x h f` | `SimaiNativeParser.StrictChecks.cpp` (`kInvalidHoldModifierSequencePrefix`, `kNonCanonicalHoldModifierPlacementPrefix`) | `1xb` instead of `1bx` |
| Break-slide `b` modifier position | `kInvalidBreakSlideModifierPositionPrefix` | mis-placed `b` on slide head/body |
| Slide / hold duration block placement | `kInvalidSlideDurationPlacementPrefix`, `kInvalidSlideDurationPrefix`, `kInvalidHoldDurationPrefix` | duration block in wrong position |
| Touch duration requires `h` | `kTouchDurationRequiresHPrefix` | duration on touch token without `h` |
| Time-signature `||x/y` placement | `SimaiNativeParser.StrictChecks.cpp:105` | inline TS at unexpected slot; comment-only `||` lines skipped |
| End-of-file strict flush | `SimaiNativeParser.Driver.cpp:652` | trailing token state validation |

Strict additionally invokes the `StrictChecks.cpp` verifier suite,
included via `#include "SimaiNativeParser.StrictChecks.cpp"` at the
end of `SimaiNativeParser.cpp:1487`.

### 1.2 The merge step — `buildValidationReport()`

The "Syntax Check" tab does NOT just show strict errors verbatim. It
calls `buildValidationReport()` (`SimaiNativeParser.Driver.cpp:986-1068`)
which runs **both** passes and produces a merged report. Output type:

```cpp
struct SimaiNativeValidationReport {
    bool ok;
    int errorCount;
    int warningCount;
    int lenientNoteCount;
    int lenientErrorCount;
    int strictNoteCount;
    int strictErrorCount;
    QVector<SimaiNativeValidationIssue> issues;
};
```

The merge logic (Driver.cpp:1023-1050):

1. Take the *strict* error list as the issue source.
2. For each strict error, compute a stable key and check whether the
   lenient pass produced the same key.
3. **If lenient also failed at the same place** → severity stays
   `Error` (it's a real structural break).
4. **If only strict failed** → severity is downgraded to `Warning`
   (the chart parses fine, it just deviates from canonical form).
5. **Exception list — `shouldRemainValidationError()`**
   (Driver.cpp:235-241) keeps these as `Error` even when only strict
   reports them, because they almost always cause downstream
   correctness issues:
   - Repeated `/` separator
   - Repeated `` ` `` separator
   - Unmatched closing bracket (any kind)
   - Unclosed bracket (any kind)
6. Strict *warnings* (already `Warning` in the strict pass) pass
   through as-is.
7. Empty-chart shortcut: if `text.trimmed().isEmpty()`, skip both
   passes and emit a single `Error` issue with `kChartEmpty()` raw
   message.

This is how you get the four reporting counters: `lenientNoteCount`
and `strictNoteCount` show how many markers each pass produced (often
identical, but can diverge when strict rejects a token lenient
accepted); `lenientErrorCount` / `strictErrorCount` reveal how much of
the issue list is "structural" vs "stylistic."

#### Severity downgrade examples

| Input | Lenient | Strict | UI severity |
|---|---|---|---|
| `1[4:1` (unclosed bracket) | Error | Error | **Error** (lenient agrees) |
| `1//5` (repeated `/`) | OK | Error | **Error** (in `shouldRemainValidationError` exception list) |
| `1xh` (modifier non-canonical) | OK | Error | **Warning** (strict-only, downgraded) |
| `{7}1,2,3,` ({7} not 384 divisor) | OK | Error | **Warning** (strict-only) |

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
