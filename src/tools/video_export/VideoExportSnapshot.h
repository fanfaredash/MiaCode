#pragma once

#include <QJsonObject>
#include <QString>

#include "common/MuriConfig.h"
#include "common/MuriRenderOptions.h"
#include "VideoExportController.h"

struct VideoExportSnapshot {
    QString schema = QStringLiteral("miacode_export_snapshot_v1");
    QString jobId;
    QString createdAtUtc;
    QString chartTextUtf8;
    int difficultyId = 0;
    QString difficultyName;
    QString originalChartPath;
    QString projectDir;
    QString trackPath;
    QString backgroundMediaPath;
    QString skinDirectory;
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
    MuriRenderOptions muriRenderOptions;
    double staticTapOnSlideThresholdSeconds =
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int outputWidth = 1024;
    int outputHeight = 1024;
    int fps = 60;
    VideoExportPreset preset = VideoExportPreset::Fast;
    bool fullRangeExport = true;
    QString outputPath;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
    bool showChartInfoHud = false;
    int skinLoadWaitMs = 2000;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& object, VideoExportSnapshot* snapshot, QString* errorMessage = nullptr);
};

bool buildVideoExportTaskFromSnapshot(
    const VideoExportSnapshot& snapshot,
    VideoExportTask* task,
    QString* errorMessage = nullptr
);
