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
#include "VideoExportSettings.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QString>

namespace miacode::video_export::dialog_detail {

inline int normaliseAudioBitrateKbps(int requested)
{
    return miacode::video_export::normalizedVideoExportAudioBitrateKbps(requested);
}

inline int normaliseExportFps(int requested)
{
    return miacode::video_export::normalizedVideoExportFps(requested);
}

inline constexpr auto& kAudioBitrateOptionsKbps =
    miacode::video_export::kVideoExportAudioBitrateOptionsKbps;
inline constexpr auto& kFpsOptions = miacode::video_export::kVideoExportFpsOptions;

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
