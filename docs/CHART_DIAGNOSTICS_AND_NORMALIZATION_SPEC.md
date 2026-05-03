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

### Entry points

| Surface | File / symbol |
|---|---|
| Public API | `SimaiNativeParser::validateSyntax()` — `src/core/chart/parser/SimaiNativeParser.h:62` |
| Structured report | `SimaiNativeParser::buildValidationReport()` — `:67` |
| UI host | `MainWindow::ValidationSection` — `src/app/mainwindow/sections/validation/MainWindow.Validation*.cpp` |
| Bottom-tabs gate | `controller.validationTabVisible` in `BottomTabsQuickHost.qml` |

### Detection categories

The strict pass invokes several verifier groups (all under
`SimaiNativeParser.*.cpp`):

| Group | File | Examples |
|---|---|---|
| Bracket / structural | `SimaiNativeParser.Driver.cpp` | unbalanced `[ ]`, `( )`, `{ }`; unterminated string |
| Lane validity | `MuriConfig.h::padTokenIsValid()` | lane out of `1..8`, ring outside `A..E`, unknown center other than `C` |
| Slide / wifi | `SimaiNativeParser.Slide.cpp` | malformed slide path, missing endpoint, invalid `[N:M]` timing fraction |
| Tap / touch / hold | `SimaiNativeParser.TouchTap.cpp` | tap+slide-head conflicts, hold without timing |
| Modifier order | `SimaiNativeParser.StrictChecks.cpp` | non-canonical `b/x/h/f` ordering; double-applied modifier |
| Time-signature | `SimaiNativeParser.StrictChecks.cpp:105` | `||x/y` placement; comment-only lines skipped |

### Issue presentation

Each issue is `SimaiNativeValidationIssue` (header `:36-43`):

```cpp
struct SimaiNativeValidationIssue {
    int line;
    int col;
    int endCol;
    SimaiNativeValidationSeverity severity;  // Error | Warning
    QString rawMessage;
    QString displayMessage;     // localized
};
```

Surface in the UI:

- **Panel list**: one row per issue, `[Lline Ccol]` prefix + display text.
- **Editor extra-selection**: red squiggle / amber underline overlay at
  `(line, col..endCol)` (driven by `refreshEditorExtraSelections`).
- **Header-level ignore**: an issue-type key on the entry lets the chart
  author add `||#ignore <key>` at the top of the file to suppress one
  category project-wide.

### What syntax detection does NOT do

- Does not flag muri / playability concerns (those go to subsystem 2).
- Does not auto-fix; rewrites are subsystem 3's job.
- Does not block parsing — `parseForTimeline()` is lenient and runs
  regardless of strict errors so the user can continue editing.

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
