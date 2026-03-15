#pragma once

#include <QString>
#include <QVector>

#include "PreviewRenderSettings.h"
#include "PreviewAudioSettings.h"
#include "TimelineView.h"
#include "common/PreviewVideoGeometryConfig.h"

class PreviewCanvas;
class QProgressDialog;

struct VideoExportTask {
    QString outputPath;
    QString chartPath;
    QString trackPath;
    QVector<TimelineNoteMarker> noteMarkers;
    PreviewAudioSettings audioSettings;
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int outputWidth = 1024;
    int outputHeight = 1024;
    int fps = 60;
    bool showTimestamp = true;
};

struct VideoExportResult {
    bool success = false;
    QString message;
    QString details;
};

class VideoExportController
{
public:
    static VideoExportResult exportFullPreview(
        const VideoExportTask& task,
        const PreviewCanvas* sourceCanvas,
        QProgressDialog* progress
    );
};
