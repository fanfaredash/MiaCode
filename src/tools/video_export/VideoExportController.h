#pragma once

#include <functional>
#include <QString>
#include <QVector>

#include "PreviewRenderSettings.h"
#include "PreviewAudioSettings.h"
#include "common/PreviewTimingSettings.h"
#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "common/PreviewGameplayConfig.h"
class QProgressDialog;

enum class VideoExportPreset {
    Fast,
    HighQuality,
};

// Pre-roll "track-start" intro banner spec. Populated from the chart in
// buildVideoExportSnapshot; consumed by the export session when `enabled`.
// `enabled` is gated on a full-range export (partial/clip exports never get it).
struct IntroBannerSpec {
    bool enabled = false;
    QString title;
    QString artist;
    QString designer;
    QString level;
    // Atlas/sprite key: one of BASIC / ADVANCED / EXPERT / MASTER / ReMASTER.
    QString difficulty = QStringLiteral("MASTER");
    QString bpm;
    QString mode = QStringLiteral("DX");
    // Resolved background STILL image (never the bg video); empty -> the QML
    // falls back to the miacode logo.
    QString jacketPath;
};

struct VideoExportTask {
    QString outputPath;
    QString chartPath;
    QString backgroundMediaPath;
    QString trackPath;
    QString skinDirectory;
    QVector<TimelineNoteMarker> noteMarkers;
    MuriAnalysisReport muriAnalysisReport;
    MuriRenderOptions muriRenderOptions;
    double staticTapOnSlideThresholdSeconds =
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0;
    PreviewAudioSettings audioSettings;
    PreviewTimingSettings timingSettings;
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewOutlineVariant outlineVariant = PreviewOutlineVariant::Line;
    QString outlineImagePath;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double tapFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double touchFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    bool slideEarlierSecondAndTextOnTop = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int outputWidth = 1024;
    int outputHeight = 1024;
    int fps = 60;
    // AAC audio bitrate in kbps. Allowed values are surfaced in the
    // export dialog dropdown (128/160/192/256/320). The default of 192
    // is one step above the previous hard-coded 160k baseline — slight
    // quality bump for charts with stereo BGM, transparent for casual
    // phone playback. Routed into ffmpeg as `-b:a <kbps>k`.
    int audioBitrateKbps = 192;
    // Export-quality toggle. HighQuality = synchronous readback (no PBO, no
    // tearing); Fast = pipelined PBO readback (faster, may tear on some GPUs).
    // Encoder params are HighQuality regardless; this only picks the readback.
    VideoExportPreset preset = VideoExportPreset::HighQuality;
    bool fullRangeExport = true;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
    bool showChartInfoHud = false;
    // Pre-roll maimai track-start intro (full-range exports only). intro.enabled
    // is the dialog checkbox before the snapshot is built; the rest of the
    // banner payload is filled by buildVideoExportSnapshot from the chart and
    // round-trips back onto the task via buildVideoExportTaskFromSnapshot.
    IntroBannerSpec intro;
    // Chart metadata for the optional top-left chart info HUD. Populated
    // from the active SimaiDocument at task-construction time; the worker
    // re-derives these from the snapshot's chart text + difficulty id
    // when running out of process (see buildVideoExportTaskFromSnapshot).
    // chartDifficultyLabel is pre-formatted as e.g. "MAS 13+".
    QString chartTitle;
    QString chartArtist;
    QString chartDifficultyLabel;
    QString chartDesigner;
    miacode::preview_gameplay::CenterDisplayMode centerDisplayMode =
        miacode::preview_gameplay::kDefaultCenterDisplayMode;
    int skinLoadWaitMs = 2000;
    int clockCount = 0;
    double clockBpm = 0.0;
};

struct VideoExportResult {
    bool success = false;
    QString message;
    QString details;
};

using VideoExportProgressCallback = std::function<bool(int percent, const QString& text)>;

class VideoExportController
{
public:
    static VideoExportResult exportFullPreview(
        const VideoExportTask& task,
        QProgressDialog* progress
    );
    static VideoExportResult exportPreparedTask(
        const VideoExportTask& task,
        const VideoExportProgressCallback& progressCallback = {}
    );
};
