#pragma once

#include <functional>
#include <memory>

#include <QtCore>
#include <QtGui>

#include "PreviewAudioSettings.h"
#include "PreviewRenderSettings.h"
#include "SimaiDocument.h"
#include "SimaiNativeParser.h"
#include "SimaiTimingMetadata.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewTimingSettings.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/chart/transform/ChartNormalization.h"
#include "app/v2/PlaybackControl.h"
#include "timeline/TimelineData.h"
#include "timeline/TimelineQuickModel.h"
#include "timeline/TimelineSlowRefresh.h"
#include "tools/video_export/VideoExportSnapshot.h"

class BracketScopeHighlighter;
class IntroBannerSpec;
class QAction;
class QWidget;
class PreviewAudioDeviceWatcher;
class PreviewRuntime;
class PreviewStageMediaHost;
class QmlExportSession;
class QtPreviewSfxRuntime;
class QuickShellPreviewCompositeSurface;
class TimelineQuickStateBridge;

namespace miacode::preview::scene {
class PreviewProgressStatsCache;
}

namespace miacode::waveform {
class WaveformCacheService;
struct WaveformData;
}

namespace miacode::v2 {
class JobProgressService;
class UiRequestService;
}

namespace miacode::runtime {

// Transitional runtime storage boundary. The two nested records are kept
// together for now because the legacy runtime still has cross-domain reads,
// but they no longer form part of Session's type definition. This lets each
// host declare the storage boundary it actually borrows and keeps playback
// contract headers independent from Session internals.
class RuntimeContext final
{
public:
    enum class TextEncoding {
        Utf8,
        System,
    };

    enum class BottomTabsTabId {
        Timeline,
        Validation,
        Muri,
        Unknown,
    };

    struct ValidationCachedIssue {
        int line = 1;
        int col = 1;
        int endCol = 1;
        SimaiNativeValidationSeverity severity = SimaiNativeValidationSeverity::Error;
        QString rawMessage;
        QString displayMessage;
    };

    struct ValidationDecoration {
        int line = 1;
        int col = 1;
        int endCol = 1;
        QString message;
        bool warning = false;
    };

    struct ValidationCacheEntry {
        QString chartText;
        SimaiNativeValidationLocale validationLocale = SimaiNativeValidationLocale::English;
        miacode::simai::SimaiTimingMetadata timingMetadata;
        quint64 validationRevision = 0;
        bool ok = true;
        int errorCount = 0;
        int warningCount = 0;
        int lenientNoteCount = 0;
        int lenientErrorCount = 0;
        int strictNoteCount = 0;
        int strictErrorCount = 0;
        QVector<ValidationCachedIssue> issues;
    };

    struct DeletedDifficultyUndoState {
        bool valid = false;
        bool wasActive = false;
        int difficultyId = 0;
        SimaiDifficultyData difficultyData;
    };

    struct EditorBookmark {
        QString title;
        QString text;
        int line = 1;
        QString source;
        QString commentText;
        QString commentFingerprint;
        QString contextBefore;
        QString contextAfter;
        int difficultyId = 0;
        bool nameLocked = false;
    };

    // Timeline projection and refresh state has a separate storage owner. The
    // legacy State record below exposes references during this incremental
    // migration, but it no longer owns these values.
    struct TimelineState {
        TimelineQuickStateBridge* timelineQuickStateBridge_ = nullptr;
        QThreadPool* timelineSlowRefreshPool_ = nullptr;
        QThreadPool* timelineAnalysisPool_ = nullptr;
        bool timelineReady_ = false;
        quint64 deferredTimelineBridgeFlushGeneration_ = 0;
        bool pendingQuickTimelineCursorSync_ = false;
        double pendingQuickTimelineCursorSecond_ = 0.0;
        bool pendingQuickTimelineCursorCenterView_ = false;
        int lastTimelineParseDifficultyId_ = 0;
        QString lastTimelineParseChartText_;
        miacode::simai::SimaiTimingMetadata lastTimelineParseTimingMetadata_;
        SimaiNativeParseResult lastTimelineParseResult_;
        QVector<TimelineNoteMarker> latestTimelineNoteMarkers_;
        QByteArray latestTimelineNoteMarkerSignature_;
        quint64 latestTimelinePreviewRevision_ = 0;
        bool latestTimelinePreviewSnapshotReady_ = false;
        TimelineQuickModel timelineQuickModel_;
        TimelineSlowRefreshRequest pendingTimelineSlowRefresh_;
        TimelineAnalysisRefreshRequest pendingTimelineAnalysisRefresh_;
        quint64 timelineRevision_ = 0;
        quint64 timelineSlowRequestedRevision_ = 0;
        quint64 timelineSlowRunningRevision_ = 0;
        quint64 timelineAnalysisRequestedRevision_ = 0;
        quint64 timelineAnalysisRunningRevision_ = 0;
        bool timelineSlowWorkerRunning_ = false;
        bool timelineAnalysisWorkerRunning_ = false;
        quint64 waveformRefreshGeneration_ = 0;
        PreviewCanvasFrameRateMode timelineFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
        bool timelineSyncEnabled_ = false;
    };

    // Stage 4.9e-4: canonical playback-authority storage has a separate owner,
    // same shape as TimelineState above. The nine fields here are exactly the
    // ones that decide "where is the playhead right now / is it playing / at
    // what rate" — the set PlaybackCoordinator::authoritativeAudioClockSecond
    // itself reads to answer that question (Playback.cpp), plus
    // previewTransportState_, which is written in lockstep with playing_ by
    // the same single-writer primitive (writePreviewPlayingFlag). Every write
    // site for every field below already lived inside playback/ before this
    // move; the legacy State record keeps const-reference aliases for the many
    // cross-domain readers, but it no longer owns these values and cannot
    // write them.
    struct PlaybackState {
        bool playing_ = false;
        miacode::v2::PlaybackTransportState previewTransportState_ =
            miacode::v2::PlaybackTransportState::Stopped;
        double pauseSecond_ = 0.0;
        double previewPlaybackRate_ = 1.0;
        double qtPreviewStartSecond_ = 0.0;
        QElapsedTimer qtPreviewElapsed_;
        // Startup handshake fields consulted directly by
        // authoritativeAudioClockSecond() to decide the frozen "now" second
        // while audio/video prepare asynchronously. The rest of the startup
        // handshake cluster (previewStartupAudioPrepared_,
        // previewStartupCanvasPresented_, ...) stays in State: those orchestrate
        // the handshake's progress rather than answering "where is the
        // playhead now".
        bool previewStartupSyncPending_ = false;
        bool previewLateVideoStartPending_ = false;
        double previewStartupPreparedSecond_ = 0.0;
    };

#define MIACODE_RUNTIME_CONTEXT_TYPES 1
#include "runtime/SessionMembers.inc"
#undef MIACODE_RUNTIME_CONTEXT_TYPES

    // Declaration order is load-bearing: `state` binds compatibility references
    // into `timeline` and `playback`, so both records must be declared (and
    // therefore constructed) first. Copying is deleted because a copied
    // `state` would still alias the source context's timeline/playback
    // storage.
    RuntimeContext()
        : state(timeline, playback)
    {}

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;

    TimelineState timeline;
    PlaybackState playback;
    Ui ui;
    State state;
};

}  // namespace miacode::runtime
