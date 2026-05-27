#pragma once

// Trivially-copyable projection of PreviewFrameState for cross-process IPC.
//
// PreviewFrameState carries QImage, std::shared_ptr, QVector<QVector<...>>,
// and QString members — none of which are safe to memcpy across a process
// boundary. For the out-of-process preview worker, the editor projects the
// rich PreviewFrameState into PreviewFrameStateSerial just before publishing
// to the snapshot ring buffer; the worker reads it back, hydrates a
// PreviewFrameState shell, and feeds it into the existing render path
// alongside its own copy of the asset repository (loaded from disk via
// PreviewSceneAssetLoader using paths included in the serial form).
//
// What crosses the boundary:
//
//   * scalar fields: playhead, fps stats, refresh rate, tick counters
//   * a fixed-capacity sprite list (subset relevant to the visible window)
//   * UTF-8 byte offsets into a side blob for variable-length strings
//
// What does NOT cross:
//
//   * QImage / QVideoFrame data — assets stay editor-side; the worker loads
//     its own copies from disk
//   * std::shared_ptr<PreviewProgressStatsCache> — sprite-derived stats; the
//     worker recomputes them or accepts an extracted scalar projection
//   * full TimelineNoteMarker (huge nested QVectors) — only fields needed for
//     the active visible window
//
// See docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md
// section 4.5 for the rationale.
//
// Versioning: bumping `kSerialLayoutVersion` invalidates all snapshot slots.
// The worker reads `layoutVersion` first and aborts attach if it disagrees,
// matching the kPreviewWorkerProtocolVersion guard at the JSON layer.

#include <QtGlobal>

#include <array>
#include <cstdint>
#include <type_traits>

namespace miacode::preview::ipc {

inline constexpr quint32 kSerialLayoutVersion = 12;

// Hard cap so the slot byte size is bounded at compile time. The ring
// buffer's slot stride is sizeof(PreviewFrameStateSerial); editor and worker
// must agree exactly. If we hit this cap in production, bump it AND
// increment kSerialLayoutVersion together.
//
// 1024 is comfortably above the worst-case observed visible window
// (~500 sprites for dense charts at peak). Combined with the per-entry
// + blob sizing below it keeps a single snapshot ~150 KB — small enough
// for snappy memcpy through the ring buffer, big enough that downstream
// QSG layers (which do their own internal batching) never lose data.
inline constexpr int kMaxSerializedSpriteCount = 1024;

// Fixed-capacity blob backing the variable-length string fields (asset paths,
// chart identifiers). 16 KB matches the nominal max chart asset path
// budget (~512 entries × ~30 char average).
inline constexpr int kSerialStringBlobBytes = 16 * 1024;

// Per-marker variable-length geometry blob — packed slide segment curves,
// slide track-area metadata (rotations / thresholds / checkpoints / cut
// indices), and wifi geometry (lane points, track areas, etc.).
// Sized for worst-case dense charts: ~80 visible slides × ~1.2 KB each
// after the v4 metadata extension + occasional wifi notes.
inline constexpr int kMarkerGeometryBlobBytes = 96 * 1024;

// Per-snapshot Muri analysis report blob. Carries `padWindows`,
// `actionTrails`, `judgeSpriteEvents`, and the visible-marker subset of
// `markerStates`. The MaimuriDxJudge / MuriPad / MuriAction layers and
// the prepared-cache MaimuriDx slot all depend on this. v5 added.
inline constexpr int kMuriReportBlobBytes = 64 * 1024;

// Variable-length data is referenced by [offset, length] into stringBlob.
// length == 0 means "absent" / empty.
struct SerialStringRef
{
    quint32 offset = 0;
    quint32 length = 0;
};

// Compact enum for `TimelineNoteMarker::type`. Values are stable; new types
// must be appended (never reordered) to avoid invalidating already-published
// snapshots. Increment `kSerialLayoutVersion` if reordering is unavoidable.
enum class SerialSpriteTypeKind : quint32
{
    Unknown   = 0,
    Tap       = 1,
    Hold      = 2,
    Slide     = 3,
    Wifi      = 4,
    Touch     = 5,
    TouchHold = 6,
};

// Bitmap layout for `SerialSpriteEntry::flagsBitmap`. Mirrors the boolean
// fields on `TimelineNoteMarker` that affect rendering. Worker-side code
// can reconstruct a partial `TimelineNoteMarker` from these bits without
// pulling the full marker over IPC.
namespace SerialSpriteFlags {
inline constexpr quint32 kIsEach           = 1u << 0;
inline constexpr quint32 kIsBreak          = 1u << 1;
inline constexpr quint32 kIsEx             = 1u << 2;
inline constexpr quint32 kIsFirework       = 1u << 3;
inline constexpr quint32 kOnSlide          = 1u << 4;
inline constexpr quint32 kSlideHead        = 1u << 5;
inline constexpr quint32 kSameHeadSlide    = 1u << 6;
inline constexpr quint32 kBeforeSlide      = 1u << 7;
inline constexpr quint32 kAfterSlide       = 1u << 8;
inline constexpr quint32 kHeadEach         = 1u << 9;
inline constexpr quint32 kHeadBreak        = 1u << 10;
inline constexpr quint32 kHeadEx           = 1u << 11;
inline constexpr quint32 kTrackBreak       = 1u << 12;
inline constexpr quint32 kHasHeadStar      = 1u << 13;
inline constexpr quint32 kHeadlessImmediate = 1u << 14;
inline constexpr quint32 kTapUsesStarMaterial = 1u << 15;
inline constexpr quint32 kTapStarDouble    = 1u << 16;
inline constexpr quint32 kTailOnSlideHead         = 1u << 17;
inline constexpr quint32 kSlideEach               = 1u << 18;
inline constexpr quint32 kSlideHeadUsesTapMaterial = 1u << 19;
}  // namespace SerialSpriteFlags

// Bitmap layout for `PreviewFrameStateSerial::muriRenderFlagsBitmap`.
// Mirrors the boolean fields on `MuriRenderOptions`. Order must remain
// stable across layout versions; new flags append.
namespace MuriRenderFlags {
inline constexpr quint32 kShowSlideTracks                  = 1u << 0;
inline constexpr quint32 kShowJudgeMarkers                 = 1u << 1;
inline constexpr quint32 kShowTouchTrail                   = 1u << 2;
inline constexpr quint32 kShowChartReviewSlideJudgeOverlay = 1u << 3;
inline constexpr quint32 kShowChartReviewTapJudgeOverlay   = 1u << 4;
inline constexpr quint32 kShowChartReviewTouchJudgeOverlay = 1u << 5;
inline constexpr quint32 kWifiNeedC                        = 1u << 6;
inline constexpr quint32 kExcludeTouchFromMultiTouch       = 1u << 7;
}  // namespace MuriRenderFlags

// Bitmap layout for `PreviewFrameStateSerial::renderFlagsBitmap`.
// Mirrors the boolean fields on `PreviewRenderState`.
namespace RenderFlags {
inline constexpr quint32 kSmoothBrightness                 = 1u << 0;
inline constexpr quint32 kSlideEarlierSecondAndTextOnTop   = 1u << 1;
inline constexpr quint32 kShowDebugInfo                    = 1u << 2;
inline constexpr quint32 kShowTimestamp                    = 1u << 3;
inline constexpr quint32 kShowObjectStatsHud               = 1u << 4;
inline constexpr quint32 kShowChartInfoHud                 = 1u << 5;
}  // namespace RenderFlags

// Reference into the snapshot's `markerGeometryBlob` for variable-length
// per-marker payloads (slide segment curves, wifi geometry, etc.). Empty
// (`length == 0`) when the marker doesn't carry that kind of data.
struct SerialBlobRef
{
    quint32 offset = 0;
    quint32 length = 0;
};

// Per-sprite payload — captures every field the renderer's layer code
// needs from `TimelineNoteMarker`. Variable-length data (strings, slide
// segment curves) is held in `stringBlob` / `markerGeometryBlob` and
// referenced via `SerialStringRef` / `SerialBlobRef`.
struct SerialSpriteEntry
{
    double startSeconds = 0.0;       // marker.second
    double endSeconds = -1.0;        // marker.endSecond (-1 = no end)
    SerialSpriteTypeKind typeKind = SerialSpriteTypeKind::Unknown;
    qint32 lane = 0;                 // marker.lane (1-based)
    qint32 endLane = 0;              // marker.endLane (slide/wifi)
    quint32 flagsBitmap = 0;         // see SerialSpriteFlags
    SerialStringRef slideTrackKey;   // marker.slideTrackKey
    SerialStringRef typeText;        // raw type string fallback
    SerialStringRef slideDisplayKey; // marker.slideDisplayKey
    SerialStringRef touchPad;        // marker.touchPad
    // Layout v8 — slide segment chain keys joined with '\n', so the
    // worker can pick the LAST segment's shape for slide-judge sprite
    // selection. The editor-side PreviewChartReviewLayerState reads
    // `marker.slideSegmentKeys.constLast()` to choose between
    // just_str / just_curv / just_wifi; without this projection the
    // worker falls back to `slideTrackKey` (which is the FIRST segment
    // for chain slides), and the worker always rendered just_str for
    // multi-segment slides whose final segment is curved or wifi.
    SerialStringRef slideSegmentKeysJoined;

    // Touch geometry — `touchPoint` from TimelineNoteMarker. Used by
    // the touch / touch-hold / touch-judge layers.
    float touchPointX = 0.0f;
    float touchPointY = 0.0f;

    // Ordering metadata — used by judge effect / firework / each-group
    // layers to break ties between markers at the same `second`.
    qint32 parseOrder = -1;
    qint32 eachGroupId = -1;
    qint32 sourceLine = 1;
    qint32 sourceCol = 1;

    // Slide geometry — points into `markerGeometryBlob` at a packed
    // `SlideGeometryPayload` (see PreviewSlideGeometryLayout.h). Empty
    // for non-slide markers. Iteration B wires this; iteration A
    // leaves it default.
    SerialBlobRef slideGeometry;

    // Slide critical proportion + native/runtime track length come from
    // the marker's slide-related scalars. Default 0 / 1.0 for non-slides.
    float wifiCriticalProportion = 1.0f;
    float slideNativeTrackLength = 0.0f;
    float slideRuntimeTrackLength = 0.0f;

    // Slide timing scalars — both default to -1.0 in TimelineNoteMarker.
    // The slide track layer uses these as visibility gates:
    //   * `availableSecond < 0` skips the slide entirely
    //   * `endSecond > slideTraceSecond && playhead >= endSecond` skips past trace
    // Without projection both stay at -1.0 on the worker side and the
    // first gate hides every slide. Layout v4 added.
    double slideTraceSecond = -1.0;
    double availableSecond = -1.0;

    // HS multiplier in effect at the moment this note was emitted by the
    // parser. Default 1.0 = legacy behavior. The worker-side layer code
    // multiplies this into the effective flow speed to slow/speed up the
    // visual fall trajectory; judge time (marker.second) is unaffected.
    // Layout v10 added.
    float hsMultiplier = 1.0f;
};

// Top-level POD. trivially copyable; suitable for memcpy across the IPC
// boundary as long as both editor and worker were compiled with the same
// PreviewFrameStateSerial.h. The static_assert enforces it.
struct PreviewFrameStateSerial
{
    quint32 layoutVersion = kSerialLayoutVersion;
    quint32 reserved0 = 0;

    // Sequence number written by the editor publisher; the worker reads the
    // most recent fully-published slot (sequence parity check, see
    // PreviewSnapshotRingBuffer).
    quint64 sequence = 0;

    // Editor-side monotonic ns timestamp at publish time. Used by the worker
    // for IPC latency diagnostics and for the visual-lookahead bias.
    qint64 publishMonotonicNs = 0;

    // Authoritative audio playhead in seconds (editor side computes this from
    // the BASS / Miniaudio backend; worker treats as ground truth).
    double playheadSeconds = 0.0;

    // Render-time stats that the HUD wants to show. Worker rebuilds the HUD
    // QImage locally; these scalars feed the text content. v5 added the
    // updateRequest* triplet — without them the worker HUD's stutter row
    // showed `0/0` for the third metric even when the editor had spikes.
    double fpsDisplay = 0.0;
    double tickFpsDisplay = 0.0;
    double updateRequestFpsDisplay = 0.0;
    double presentMaxMsDisplay = 0.0;
    double tickMaxMsDisplay = 0.0;
    double updateRequestMaxMsDisplay = 0.0;
    qint32 presentStutterCountDisplay = 0;
    qint32 tickStutterCountDisplay = 0;
    qint32 updateRequestStutterCountDisplay = 0;
    qint64 tickCount = 0;
    qint64 updateRequestCount = 0;
    qint64 presentedFrameCount = 0;

    // Display refresh / pacing intent.
    double framePacingTargetFps = 0.0;
    double displayRefreshRate = 0.0;
    quint32 framePacingUsesDisplayRefresh = 0;

    // Mirrors `PreviewFrameState::muriRenderOptions` — these gate which
    // chart layers render and feed the prepared-cache key. Without them
    // the worker would render with the struct's defaults instead of
    // the user's selected settings (e.g. MaimuriDxStyle vs Native, the
    // chart-review overlay toggles). Layout v3 added.
    //
    // muriRenderModeKind: 0=Native, 1=MaimuriDxStyle. New values must
    // be appended; reordering invalidates already-published snapshots.
    quint32 muriRenderModeKind = 0;
    quint32 muriRenderFlagsBitmap = 0;  // see MuriRenderFlags below

    // Mirrors `PreviewFrameState::render` — drives sprite timing
    // (flow speeds gate the visible-marker window), backdrop scale,
    // background brightness/scale-mode, HUD toggles. Wrong defaults
    // here meant taps / slides appeared with the wrong scroll cadence
    // and sprites entered the visible window at editor-side times the
    // worker silently shifted. Layout v3 added.
    double tapFlowSpeed = 0.0;
    double touchFlowSpeed = 0.0;
    double backgroundBrightnessOuter = 0.0;
    double backgroundBrightnessInner = 0.0;
    double layoutSquareScale = 0.0;
    quint32 backgroundScaleModeKind = 0;  // PreviewBackgroundScaleMode enum
    quint32 renderFlagsBitmap = 0;        // see RenderFlags below

    // Popup-target screen rectangle in editor pixels (origin + display size).
    // Worker MoveWindows the popup HWND to this rectangle. The editor's
    // PreviewWorkerSupervisor::setVisualTransform also publishes this through
    // the JSON channel; the duplicate path here lets the worker pick up
    // intra-frame motion without waiting on the JSON queue.
    qint32 popupOriginXPx = 0;
    qint32 popupOriginYPx = 0;
    qint32 popupDisplayWPx = 0;
    qint32 popupDisplayHPx = 0;

    // Small fixed-cap sprite list. count is how many entries are valid in
    // sprites[]. count <= kMaxSerializedSpriteCount (compile-time guard).
    quint32 spriteCount = 0;
    SerialSpriteEntry sprites[kMaxSerializedSpriteCount];

    // Asset directory paths — references into stringBlob. The worker
    // process loads its own copies of skin/judge/firework images from
    // disk via PreviewSceneAssetLoader using these paths, since the
    // QImage data themselves are not safe to memcpy across the IPC
    // boundary. New paths must be appended here (never reordered) to
    // preserve `kSerialLayoutVersion` compatibility.
    SerialStringRef skinDirectory;

    // Chart media (PV / BG) paths. The worker loads these locally —
    // image bgs via PreviewSceneAssetLoader, video bgs (Phase 2) via a
    // worker-side PreviewStageMediaHost. `mediaImagePath` is the file
    // path of the resolved image bg when `mediaKind == Image`. Empty
    // when no resolved media or when video mode. `mediaVideoPath` is
    // the resolved video file (Phase 2 — currently unused by worker).
    // `mediaKind` mirrors `PreviewStageMediaHost::MediaKind`:
    // 0=None, 1=Image, 2=Video. v6 added.
    SerialStringRef mediaImagePath;
    SerialStringRef mediaVideoPath;
    quint32 mediaKind = 0;
    quint32 mediaVisible = 0;
    quint64 mediaSerial = 0;  // bumps on chart-path / mode change

    // Chart metadata for the optional top-left chart info HUD shown during
    // export preview / export. All four are SerialStringRef into the
    // shared stringBlob; empty refs mean "no value yet" and the painter
    // skips the missing line. chartDifficultyLabel is pre-formatted by
    // the producer as e.g. "MAS 13+". v11 added title/designer;
    // v12 added artist/difficultyLabel.
    SerialStringRef chartTitle;
    SerialStringRef chartArtist;
    SerialStringRef chartDifficultyLabel;
    SerialStringRef chartDesigner;

    // String blob for variable-length data referenced by SerialStringRef.
    quint32 stringBlobUsedBytes = 0;
    std::array<char, kSerialStringBlobBytes> stringBlob{};

    // Per-marker geometry blob for variable-length packed payloads
    // (slide curves, wifi track areas). Referenced via SerialBlobRef
    // on each SerialSpriteEntry. See PreviewSlideGeometryLayout.h for
    // the byte layout.
    quint32 markerGeometryBlobUsedBytes = 0;
    std::array<char, kMarkerGeometryBlobBytes> markerGeometryBlob{};

    // Per-snapshot MuriAnalysisReport blob — visible-window subset of
    // padWindows, actionTrails, judgeSpriteEvents, and markerStates.
    // Strings are encoded inline (length-prefixed UTF-8) so the blob is
    // self-contained.
    quint32 muriReportBlobUsedBytes = 0;
    std::array<char, kMuriReportBlobBytes> muriReportBlob{};
};

static_assert(std::is_trivially_copyable_v<PreviewFrameStateSerial>,
              "PreviewFrameStateSerial must be trivially copyable for shared-memory IPC");

inline constexpr int kPreviewFrameStateSerialBytes =
    static_cast<int>(sizeof(PreviewFrameStateSerial));

}  // namespace miacode::preview::ipc
