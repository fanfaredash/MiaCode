---
name: miacode-dev-guide
description: Repository-specific guide for MiaCode. Use when working in this repo to locate implementations down to files, classes, and functions; trace linked behavior across document parsing, timeline, preview, audio, latency detection, Muri analysis, assets, scripts, and video export; check fixed-vs-flexible design rules; review hardcoded values and resource conventions; or update the guide after structural changes.
---

# MiaCode Dev Guide

Use this skill as the repo memory layer for MiaCode. Start from the user-facing feature, map it to the owning entry point, then open only the reference files needed for that task.

## Route The Task

- For feature location, ownership, or impact analysis, read `references/feature-index.md`.
- For parser/timeline/preview/audio/export coupling, read `references/cross-chain-linkage.md`.
- For product logic, interaction expectations, and "must vs flexible" decisions, read `references/design-ledger.md`.
- For constants, thresholds, tuning numbers, and magic-value ownership, read `references/hardcode-registry.md`.
- For assets, filenames, lookup rules, build/package tooling, and helper scripts, read `references/assets-and-tools.md`.
- For repo-wide debug switches, runtime timing logs, preview overrides, and export diagnostics, read `references/debug-flags.md`.

## Work The Repo In This Order

1. Identify the user-facing capability and its primary entry point.
2. Follow downstream consumers before editing. In MiaCode, many behaviors are mirrored across runtime and export paths.
3. If a behavior exists in both preview-time and export-time code, treat it as a sync pair unless the references explicitly say otherwise.
4. Prefer code over docs when they disagree. If code disproves a reference, update the reference in the same change.

## Core Anchors

- App boot and CLI: `src/app/main.cpp`
- Main window orchestration: `src/app/mainwindow/`
- Document model: `src/simai/document/`
- Parser and validation primitives: `src/simai/parser/`
- Timeline data and UI: `src/timeline/`
- Preview video: `src/preview/video/`
- Preview audio: `src/preview/audio/`
- Tools: `src/tools/latency/`, `src/tools/muri/`, `src/tools/video_export/`
- Shared config headers: `src/common/`

## Maintenance Rules

- Keep `SKILL.md` short. Put repo-specific detail into `references/*.md`, and point to those files from here.
- After renames, moves, or architectural splits, update the affected file paths, class names, function names, and ownership notes in `references/feature-index.md`.
- After changing cross-module behavior, update `references/cross-chain-linkage.md` in the same change.
- After changing product behavior, expectations, defaults, or decision boundaries, update `references/design-ledger.md`.
- After introducing, removing, centralizing, or re-scoping constants, update `references/hardcode-registry.md`.
- After changing filenames, lookup order, packaged dependencies, helper scripts, or debug tooling, update `references/assets-and-tools.md`.
- After adding, removing, centralizing, or re-scoping debug flags, timing logs, or diagnostic env vars, update `references/debug-flags.md`.
- After changing this skill or any of its reference Markdown files, update the Chinese mirror under `.codex/i18n/zh-CN/miacode-dev-guide/` in the same change.
- After updating the Chinese mirror, run `python .codex/tools/check_translation_sync.py --stamp` to refresh the recorded source hashes.
- When adding a new hardcoded value, first check whether an existing `src/common/*.h` config header should own it. If it must stay local, document the file, meaning, unit, and linked surfaces.
- When adding a new feature path, record both its primary owner and every mirrored or downstream path that must stay in sync.
- When removing a feature, delete stale breadcrumbs instead of leaving dead references behind.

## High-Risk Sync Areas

- Runtime SFX timeline and export SFX timeline must stay aligned.
- Background media resolution is implemented in both preview-time and export-time code.
- Track path resolution is implemented in multiple places.
- `&first` and timing offsets affect parser output, preview positioning, export timing, and latency detection.
- Export uses a snapshot/worker boundary; changes to serialized task shape must be reflected on both sides.

## References

- `references/feature-index.md`
- `references/cross-chain-linkage.md`
- `references/design-ledger.md`
- `references/hardcode-registry.md`
- `references/assets-and-tools.md`
- `references/debug-flags.md`
