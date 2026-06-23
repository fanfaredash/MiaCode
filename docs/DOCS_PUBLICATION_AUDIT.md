# Docs Publication Audit

This audit reviews tracked files under [docs](.) for public prerelease readiness. It does not remove or redact documents; it records the risk level and recommended action before the repository is made public.

## Summary

- Most tracked `docs/` files are architecture, diagnostic, timeline, export, and release notes that can be public after normal proofreading.
- A small number of tracked docs contain local machine paths, dump/log investigation details, old branch names, or handoff/process notes. These should be redacted or moved to a private archive before publication.
- Ignored local docs exist under `docs/`, but they are not tracked and will not publish through Git unless force-added later.

## Recommended Actions

1. Redact local paths and private investigation artifacts before making tracked docs public.
2. Decide whether process handoff docs should remain public, be shortened, or be moved to a private archive.
3. Keep public docs focused on stable architecture, diagnostics, build/release, and user/developer workflows.
4. Re-run a current-tree scan after any docs cleanup and before the first public prerelease.

## Needs Redaction Before Public

| File | Finding | Recommendation |
| --- | --- | --- |
| `docs/EXPORT_FULLSCREEN_CRASH_INVESTIGATION_ZH.md` | Contains a real local dump directory, local Qt install path references, dump timestamps, machine/driver investigation details, and local log artifact names. | Redact local paths and private dump locations. Consider replacing exact dump inventory with a summarized public incident note. |
| `docs/PLAYBACK_START_LATENCY_PLAN.md` | Contains a local regression dataset path. | Replace with a placeholder such as `<local regression dataset>` and document dataset shape instead of the private path. |
| `docs/REFACTOR_HANDOFF.md` | Contains local Qt path examples and is primarily a handoff/process document. | Replace concrete local paths with placeholders if kept public; consider moving to private archive because public value is lower than architecture/spec docs. |

## Consider Private Archive Or Rewrite

These files are not necessarily sensitive, but they contain internal process, old branch/commit references, or development-session details that may distract public readers.

| File | Reason | Recommendation |
| --- | --- | --- |
| `docs/GOD_FILE_REFACTOR_PROGRESS_AND_PLAN_ZH.md` | Detailed internal refactor execution log, old branch name `refactor/god-file-split`, commit sequence, rollback notes, and session process. | Keep only if you want to publish historical engineering notes. Otherwise move to private archive or condense into a short architecture/refactor note. |
| `docs/LOGGING_AUDIT_REMAINING_REFACTORS_ZH.md` | Internal multi-agent audit history, commit/branch references, and backlog prioritization. | Publicly useful as a design backlog after removing process framing; otherwise private archive. |
| `docs/REFACTOR_HANDOFF.md` | Handoff-oriented, references `.claude` workflow docs and local test setup. | Same as above: redact and condense, or archive privately. |
| `docs/EXPORT_FULLSCREEN_CRASH_INVESTIGATION_ZH.md` | Useful crash-analysis knowledge, but includes private evidence inventory and machine-specific investigation notes. | Best public form is a sanitized postmortem or known-issue note. |

## Generally Safe / Public Useful

These tracked docs appear suitable for public publication after ordinary proofreading and link checks:

- `docs/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC.md`
- `docs/CHART_DIAGNOSTICS_AND_NORMALIZATION_SPEC_ZH.md`
- `docs/DEBUG_INDEX.md`
- `docs/EXPORT_PAGE_AND_LATENCY_ENTRY_MIGRATION_SPEC_ZH.md`
- `docs/MURI_DETECTION_SPEC.md`
- `docs/MURI_DETECTION_TEST_CHECKLIST.md`
- `docs/OPERATION_LOG_PATTERNS_SPEC.md`
- `docs/PREVIEW_FRAMEDROP_DIAGNOSIS_AND_FIX_SPEC_ZH.md`
- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `docs/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC_ZH.md`
- `docs/PREVIEW_RUNTIME_WORKFLOW.md`
- `docs/QT_QUICK_FULL_FRONTEND_MIGRATION_PLAN.md`
- `docs/SLIDE_DELAY_AND_HEAD_MATERIAL_SPEC.md`
- `docs/TIMELINE_COORDINATE_FOCUS_SPEC.md`
- `docs/TIMELINE_COORDINATE_FOCUS_TEST_CHECKLIST.md`
- `docs/TIMELINE_LAYER_STACK_AND_SLIDE_ORDER_SPEC.md`
- `docs/TIMELINE_LAYER_STACK_AND_SLIDE_ORDER_SPEC_ZH.md`
- `docs/TIMELINE_PREVIEW_LATENCY_RECOVERY_SPEC.md`
- `docs/TIMELINE_QTQUICK_GPU_FEASIBILITY_SPEC.md`
- `docs/TIMELINE_QTQUICK_GPU_PARITY_CHECKLIST.md`
- `docs/TIMELINE_QTQUICK_GPU_PARITY_CHECKLIST_ZH.md`
- `docs/VIDEO_EXPORT_MEMORY_ANALYSIS.md`
- `docs/VIDEO_EXPORT_SUBPROCESS_ISOLATION.md`

Notes:

- Mentions of `MajdataPlay` in chart diagnostics appear to be behavioral reference notes, consistent with [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).
- Terms such as `crash`, `dump`, and `private_mb` are mostly technical diagnostics and not sensitive by themselves.
- `docs/DEBUG_INDEX.md` intentionally lists `MIACODE_*` environment flags; this is useful for public debugging.

## Publication Support Docs

These are meant to remain public and should continue to track release decisions:

- `docs/OPEN_SOURCE_CHECKLIST.md`
- `docs/RELEASE_CHECKLIST.md`
- `docs/DOCS_PUBLICATION_AUDIT.md`

## Ignored Local Docs

The working tree currently has ignored local docs under `docs/` that are not tracked by Git. They include older plans, handoff notes, and research docs such as:

- `docs/BASS_AUDIO_REARCHITECTURE_PLAN_ZH.md`
- `docs/COVER_EXPORT_COMPOSER_HANDOFF_ZH.md`
- `docs/HWDECODE_TEST_GUIDE_ZH.md`
- `docs/LATENCY_SFX_ISOLATION_HANDOFF.md`
- `docs/MINE_NOTE_RESEARCH_AND_MIACODE_PORT_HANDOFF_ZH.md`
- `docs/PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md`
- `docs/VIDEO_DECODE_BACKEND_QTAVPLAYER_MIGRATION_ZH.md`

Do not force-add ignored docs without a separate publication review.

## Scan Approach

The audit used a broad current-tree scan over tracked docs for local path patterns, private/handoff markers, secrets-like tokens, deprecated dependency names, and reference-project mentions.

This is a publication-readiness scan, not a full secret scan. Full current-tree and Git-history secret scans are still required before making the repository public.
