#pragma once

#include "VideoExportController.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <array>

namespace miacode::video_export {

struct VideoExportResolutionPreset {
    int width = 1080;
    int height = 1080;
    const char* label = "1080x1080 (1:1)";
    double aspectRatio = 1.0;
};

inline constexpr std::array<VideoExportResolutionPreset, 10> kVideoExportResolutionPresets{{
    {720, 720, "720x720 (1:1)", 1.0},
    {1024, 1024, "1024x1024 (1:1)", 1.0},
    {960, 720, "960x720 (4:3)", 4.0 / 3.0},
    {1280, 720, "1280x720 (16:9)", 16.0 / 9.0},
    {1080, 1080, "1080x1080 (1:1)", 1.0},
    {1440, 1080, "1440x1080 (4:3)", 4.0 / 3.0},
    {1920, 1080, "1920x1080 (16:9)", 16.0 / 9.0},
    {1440, 1440, "1440x1440 (1:1)", 1.0},
    {1920, 1440, "1920x1440 (4:3)", 4.0 / 3.0},
    {2560, 1440, "2560x1440 (16:9)", 16.0 / 9.0},
}};

inline constexpr std::array<int, 5> kVideoExportAudioBitrateOptionsKbps{{128, 160, 192, 256, 320}};
inline constexpr std::array<int, 3> kVideoExportFpsOptions{{30, 60, 120}};
inline constexpr double kFullRangeVideoExportStartEpsilonSeconds = 0.01;

int normalizedVideoExportAudioBitrateKbps(int requested);
int normalizedVideoExportFps(int requested);

QString videoExportPreferencePresetToken(VideoExportPreset preset);
VideoExportPreset videoExportPresetFromPreference(
    const QJsonValue& value,
    VideoExportPreset fallback);
VideoExportSizePreset videoExportSizePresetFromPreference(
    const QJsonValue& value,
    VideoExportSizePreset fallback);

void applyVideoExportPreferences(const QJsonObject& settings, VideoExportTask* task);
void appendVideoExportPreferences(QJsonObject* settings, const VideoExportTask& task);

QString sanitizeVideoExportTimestamp(QString text);
QString formatVideoExportTimestamp(double seconds);
bool parseVideoExportTimestamp(const QString& text, double* seconds);
bool isFullRangeVideoExport(double exportStartSeconds);

// Copies only values the user edits in an export UI. Chart paths, parsed data,
// metadata, output naming and range remain owned by the newly seeded chart.
void copyVideoExportUserSettings(const VideoExportTask& source, VideoExportTask* target);

}  // namespace miacode::video_export
