#pragma once

// Projects the editor's rich `PreviewFrameState` into the wire-format
// `PreviewFrameStateSerial` POD that crosses the shared-memory boundary to
// the preview worker.
//
// Lives at the IPC seam by design. Higher-up code (PreviewRuntime, MainWindow,
// QuickShellBootstrap) calls into here on every `frameStateChanged` tick;
// this file owns the contract for what survives the projection.
//
// What is projected today (scalar fields only):
//   * playhead, fps stats, refresh rate
//   * frame-pacing intent
//   * tick / presented counters
//
// What is NOT projected yet (deferred to Phase 4):
//   * `noteMarkers` (variable-length, nested QVectors — needs sprite-window
//     projection into `SerialSpriteEntry[]` + the string blob)
//   * `judgeOverlay` / `judgeEffect` / `skin` asset images — worker loads
//     these from disk via `PreviewSceneAssetLoader` using paths from the
//     blob (also Phase 4)
//
// The function is fast (just member copies) and safe to call on the GUI
// thread inside the existing `frameStateChanged` slot.

#include "core/scene/PreviewFrameState.h"
#include "preview/ipc/PreviewFrameStateSerial.h"

namespace miacode::preview::ipc {

// Populate the scalar / pacing fields of `out` from `state`. Does NOT touch
// `out.sequence`, `out.publishMonotonicNs`, `out.layoutVersion` — those are
// owned by the publisher (`PreviewSnapshotRingBuffer::publish`). Does NOT
// touch `out.sprites` / `out.stringBlob` either — those are zero-initialised
// by the caller (the synthetic harness does this; the real path must too).
void projectScalarsToSerial(const miacode::preview::scene::PreviewFrameState& state,
                            PreviewFrameStateSerial& out);

// Window of seconds around the playhead that is considered "visible" for
// sprite projection purposes. Wider than strictly needed so the worker can
// pre-roll judge effects and slide tracks; narrower than the full chart so
// the kMaxSerializedSpriteCount cap doesn't trip on long charts.
//
// The lookback must cover the longest post-marker animation so that the
// sprite stays in the worker's snapshot until its animation finishes:
//   - judgeEffectDuration       ≈ 0.72 s
//   - judgeEffectFireworkDelay  ≈ 0.05 s
//   - judgeEffectFireworkDuration ≈ 1.33 s   (longest, ~1.38 s total)
// At 0.5 s lookback, fireworks vanished mid-animation when the playhead
// passed marker.second + 0.5 — the marker dropped from the snapshot, the
// prepared cache rebuild lost the firework entry, and the visible burst
// truncated. Bumped to 2.0 s for headroom.
inline constexpr double kSpriteProjectionLookbackSeconds = 2.0;
inline constexpr double kSpriteProjectionLookaheadSeconds = 5.0;

// Walk `state.noteMarkers` and pack the subset whose `[start, end]` window
// overlaps `[playhead - kSpriteProjectionLookbackSeconds,
//             playhead + kSpriteProjectionLookaheadSeconds]` into
// `out.sprites[0..spriteCount-1]`. Variable-length strings (slide track key,
// raw type text fallback) are written into `out.stringBlob` and referenced
// via `SerialStringRef` offsets.
//
// Caller must zero-initialise `out` before calling (or the previous call's
// stringBlob residue will leak across snapshots). Truncates silently at
// `kMaxSerializedSpriteCount` and `kSerialStringBlobBytes` — the dropped
// count is logged so operators can flag a chart that exceeds the budget
// and bump the cap + layout version together.
//
// Returns the number of sprites packed (which equals `out.spriteCount`).
int projectActiveSpritesToSerial(const miacode::preview::scene::PreviewFrameState& state,
                                  PreviewFrameStateSerial& out);

// Map a `TimelineNoteMarker::type` string to the compact serial enum. Used
// internally by `projectActiveSpritesToSerial` and exposed for callers that
// want to inspect a marker without going through the full projector.
SerialSpriteTypeKind classifyMarkerType(const QString& typeText);

// Pack asset directory paths (skin, judge effects, etc.) into the snapshot's
// string blob. Caller passes the editor-side paths (typically from
// PreviewRuntime::skinDirectory()). The worker uses these to feed
// PreviewSceneAssetLoader on its own side, loading its own QImage copies
// from disk — the QImage objects themselves never cross the IPC boundary.
//
// Must be called AFTER projectActiveSpritesToSerial because both share the
// same string blob and sprite projection writes its own variable-length
// strings (slide track keys, type fallbacks) first.
//
// Returns the byte count appended to the blob, for diagnostic logging.
int projectAssetPathsToSerial(const QString& skinDirectory,
                              PreviewFrameStateSerial& out);

// Pack chart-media (PV / BG) paths and mode hints. v6 added — drives
// the worker-side stage-background path: image bgs load locally via
// PreviewSceneAssetLoader, video bgs (Phase 2) drive a worker-side
// PreviewStageMediaHost. Caller passes:
//   - mediaImagePath:  the resolved image bg file path (empty for
//     video-only or no-media charts)
//   - mediaVideoPath:  the resolved video bg file path (Phase 2 use)
//   - mediaKind:       0=None, 1=Image, 2=Video — mirrors
//     `PreviewStageMediaHost::MediaKind`
//   - mediaVisible:    whether the bg should display (mirrors the
//     host's `mediaVisible` flag)
//   - mediaSerial:     bumps whenever any of the above changes — the
//     worker uses it as a cache invalidator and to detect chart load
//
// Must run AFTER projectAssetPathsToSerial and projectActiveSpritesToSerial
// because all share the same string blob; bg paths land at the end.
int projectMediaPathsToSerial(const QString& mediaImagePath,
                              const QString& mediaVideoPath,
                              int mediaKind,
                              bool mediaVisible,
                              quint64 mediaSerial,
                              PreviewFrameStateSerial& out);

// Worker-side: reconstruct a `TimelineNoteMarker` from a single
// `SerialSpriteEntry` plus the snapshot's string blob. The mapping is
// best-effort — slide segment geometry (`slideSegmentPoints`,
// `wifiTrackAreaPoints`, etc.) is NOT yet projected through IPC so those
// fields stay default-initialised on the worker side. Heads / touches /
// taps render correctly; slide tracks render only their head + tail
// stars (no curved track between them) until segment-geometry projection
// lands.
//
// Inverse of `projectActiveSpritesToSerial` for the per-marker fields
// it does cover. Used by the QSG-mode worker session to fill
// `PreviewFrameState::noteMarkers` from the snapshot.
TimelineNoteMarker inflateSerialSpriteToMarker(const SerialSpriteEntry& entry,
                                                const PreviewFrameStateSerial& snapshot);

// Convenience wrapper: walk the snapshot's full sprite list and build a
// QVector of TimelineNoteMarker for the renderer to consume. Equivalent
// to `for (i in 0..spriteCount) inflate(...)`. Returns the number of
// markers produced.
int inflateActiveSpritesToMarkers(const PreviewFrameStateSerial& snapshot,
                                   QVector<TimelineNoteMarker>* outMarkers);

// Project the visible-window subset of `state.muriAnalysisReport` into
// `out.muriReportBlob`. The MaimuriDxJudge / MuriPad / MuriAction layers
// (and the prepared-cache MaimuriDxJudge slot) all gate on this report;
// without projection they render empty even when the user has Muri
// detection toggled on. Filters by the same visible-window bounds as
// `projectActiveSpritesToSerial` so the blob stays bounded for dense
// charts. Returns the number of bytes written, or 0 if the blob is
// disabled (`MIACODE_PREVIEW_WORKER_DISABLE_MURI_REPORT=1`).
int projectMuriAnalysisReportToSerial(const miacode::preview::scene::PreviewFrameState& state,
                                       PreviewFrameStateSerial& out);

// Worker-side: unpack the muri report blob into
// `outState.muriAnalysisReport`. Returns true on success / partial-read,
// false on a malformed blob (magic mismatch / truncated). On failure the
// out-state is left in an empty state.
bool unpackMuriAnalysisReport(const PreviewFrameStateSerial& snapshot,
                               miacode::preview::scene::PreviewFrameState* outState);

}  // namespace miacode::preview::ipc
