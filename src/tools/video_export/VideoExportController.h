#pragma once

#include <QString>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "TimelineView.h"

class PreviewCanvas;
class QProgressDialog;

struct VideoExportTask {
    QString outputPath;
    QString chartPath;
    QString trackPath;
    QVector<TimelineNoteMarker> noteMarkers;
    PreviewAudioSettings audioSettings;
    double backgroundBrightness = 0.2;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int resolution = 1024;
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
