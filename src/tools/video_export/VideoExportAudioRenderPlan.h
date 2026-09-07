#pragma once

#include <QString>
#include <QVector>

#include "timeline/TimelineData.h"

struct VideoExportTask;

namespace miacode::video_export {

struct BackgroundTrackRenderPlan {
    bool enabled = false;
    QString path;
    double mixStartSecond = 0.0;
    double sourceStartSecond = 0.0;
    double durationSeconds = 0.0;
    double gain = 1.0;
};

struct ScheduledSfxPlaybackRenderPlan {
    QString kind;
    QString assetKind;
    double mixSecond = 0.0;
    double gain = 0.0;
    double maxDurationSeconds = -1.0;
};

// One stretch of the export where a single touch-hold span owns the riser, in
// mix time. Mirrors what the preview voice plays across that stretch, so
// `sourceStartSecond` (where to enter the riser sample) is non-zero whenever the
// preview voice would have been mid-sample there — see
// preview_sfx_timeline::buildTouchholdOwnershipSegments.
struct TouchholdSpanRenderPlan {
    QString kind;
    QString assetKind;
    double mixSecond = 0.0;
    double sourceStartSecond = 0.0;
    double gain = 0.0;
    double durationSeconds = 0.0;
};

struct VideoExportAudioRenderPlan {
    double segmentStartSecond = 0.0;
    double segmentEndSecond = 0.0;
    double leadInSeconds = 0.0;
    // Extra silent pre-roll prepended in FRONT of the lead-in for the maimai
    // track-start intro (full-range exports with intro enabled). The chart
    // timeline origin, total duration and frame count all absorb it, so the
    // existing linear frame->chart-second mapping still joins at -leadIn; the
    // first `introFrameCount` frames are the intro window (driven separately).
    double introLeadSeconds = 0.0;
    int introFrameCount = 0;
    double timelineOriginSecond = 0.0;
    double totalSeconds = 0.0;
    double alignedTotalSeconds = 0.0;
    int frameCount = 0;
    QString sfxDirectory;
    QVector<TimelineNoteMarker> exportMarkers;
    BackgroundTrackRenderPlan backgroundTrack;
    QVector<ScheduledSfxPlaybackRenderPlan> scheduledSfxPlaybacks;
    QVector<TouchholdSpanRenderPlan> touchholdSpanPlaybacks;
};

bool buildVideoExportAudioRenderPlan(
    const VideoExportTask& task,
    VideoExportAudioRenderPlan* plan,
    QString* errorMessage = nullptr
);

}  // namespace miacode::video_export
