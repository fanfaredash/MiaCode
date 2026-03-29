#pragma once

#include <functional>
#include <QString>
#include <QVector>

#include "PreviewRenderSettings.h"
#include "PreviewAudioSettings.h"
#include "timeline/TimelineData.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "common/PreviewGameplayConfig.h"

class PreviewCanvas;
class QProgressDialog;

struct VideoExportTask {
    QString outputPath;
    QString chartPath;
    QString backgroundMediaPath;
    QString trackPath;
    QString skinDirectory;
    QVector<TimelineNoteMarker> noteMarkers;
    MuriAnalysisReport muriAnalysisReport;
    MuriRenderOptions muriRenderOptions;
    PreviewAudioSettings audioSettings;
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double noteFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int outputWidth = 1024;
    int outputHeight = 1024;
    int fps = 60;
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
        const PreviewCanvas* sourceCanvas,
        QProgressDialog* progress
    );
    static VideoExportResult exportPreparedTask(
        const VideoExportTask& task,
        const PreviewCanvas* sourceCanvas = nullptr,
        const VideoExportProgressCallback& progressCallback = {}
    );
};
