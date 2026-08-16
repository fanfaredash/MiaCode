#pragma once

// Internal helpers shared across the VideoExportDialog translation units (the
// god-file split of VideoExportDialog.cpp on 2026-06-18). These were file-local
// anonymous-namespace helpers; the ones referenced by MORE THAN ONE of the
// resulting .cpp TUs are hoisted here into a NAMED namespace so every TU links
// against the SAME definition (an anonymous-namespace copy has internal linkage
// and could not be shared). TU-private helpers stay in their own .cpp's
// anonymous namespace; the file-local QWidget subclasses (TimestampSpinBox /
// ExportRangeTrack) stay in the core VideoExportDialog.cpp.

#include "UiText.h"
#include "UiTheme.h"
#include "VideoExportController.h"
#include "common/PreviewGameplayConfig.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QString>

namespace miacode::video_export::dialog_detail {

inline constexpr int kAudioBitrateOptionsKbps[] = {128, 160, 192, 256, 320};
inline constexpr int kFpsOptions[] = {30, 60, 120};

inline int normaliseAudioBitrateKbps(int requested)
{
    int closest = kAudioBitrateOptionsKbps[0];
    int closestDelta = qAbs(requested - closest);
    for (int candidate : kAudioBitrateOptionsKbps) {
        const int delta = qAbs(requested - candidate);
        if (delta < closestDelta) {
            closest = candidate;
            closestDelta = delta;
        }
    }
    return closest;
}

inline int normaliseExportFps(int requested)
{
    int closest = kFpsOptions[0];
    int closestDelta = qAbs(requested - closest);
    for (int candidate : kFpsOptions) {
        const int delta = qAbs(requested - candidate);
        if (delta < closestDelta || (delta == closestDelta && candidate > closest)) {
            closest = candidate;
            closestDelta = delta;
        }
    }
    return closest;
}

inline QString exportDialogPresetLabel(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return UiText::text(QStringLiteral("dialog.video_export.preset.high_quality"));
    case VideoExportPreset::Fast:
    default:
        return UiText::text(QStringLiteral("dialog.video_export.preset.fast"));
    }
}

// Flow-speed field formatting/snapping. Shared by the dialog's field builder
// (VideoExportDialog.cpp) and the Return-key commit path
// (VideoExportDialog.ExportFlow.cpp), so both round identically.
inline double snappedFlowSpeed(double flowSpeed)
{
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    return qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
}

inline QString flowSpeedValueLabel(double flowSpeed)
{
    const double snapped = snappedFlowSpeed(flowSpeed);
    const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
    const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
    return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
}

inline QString exportBaseDirectory(const VideoExportTask& task)
{
    const QFileInfo chartInfo(task.chartPath);
    if (!chartInfo.absoluteDir().path().isEmpty()) {
        return chartInfo.absoluteDir().absolutePath();
    }
    return QDir::currentPath();
}

inline QString displayOutputPathForDialog(const QString& outputPath, const QString& baseDirectory)
{
    if (outputPath.trimmed().isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(outputPath);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(outputPath)
        : outputInfo.absoluteFilePath();
    return QDir::toNativeSeparators(QDir(baseDirectory).relativeFilePath(QDir::cleanPath(absolutePath)));
}

inline QIcon makePreviewStopIcon(const QColor& color)
{
    return UiTheme::dialogTransportStopIcon(color);
}

}  // namespace miacode::video_export::dialog_detail
