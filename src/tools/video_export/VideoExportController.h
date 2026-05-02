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
    VideoExportPreset preset = VideoExportPreset::Fast;
    bool fullRangeExport = true;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
    int skinLoadWaitMs = 2000;
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
