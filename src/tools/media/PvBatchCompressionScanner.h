#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace miacode::media {

inline constexpr qint64 kPvCompressionTargetBytes = 20LL * 1024LL * 1024LL;
inline constexpr int kPvCompressionAudioBitrateKbps = 96;
inline constexpr int kPvCompressionMinVideoBitrateKbps = 120;
inline constexpr double kPvCompressionShrinkRatio = 0.86;
inline constexpr double kPvCompressionMuxSafetyRatio = 0.965;

struct PvCompressionJob {
    QString directoryPath;
    QString displayName;
    QString videoPath;
    qint64 originalBytes = 0;
};

QList<PvCompressionJob> scanPvCompressionFolders(const QString& rootDirectory);
int appendUniquePvCompressionJobs(
    QList<PvCompressionJob>* queue,
    const QList<PvCompressionJob>& candidates);

}  // namespace miacode::media
