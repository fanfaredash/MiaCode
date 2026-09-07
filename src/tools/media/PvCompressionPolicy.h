#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace miacode::media {

inline constexpr qint64 kPvCompressionHardLimitBytes = 20'000'000LL;
inline constexpr qint64 kPvCompressionWorkingTargetBytes = 19'500'000LL;
inline constexpr double kPvCompressionRetrySafetyRatio = 0.98;

struct PvCompressionPlan {
    qint64 targetBytes = kPvCompressionWorkingTargetBytes;
    int videoBitrateKbps = 0;
};

PvCompressionPlan makePvCompressionPlan(double durationSeconds);
PvCompressionPlan adjustedPvCompressionPlan(
    const PvCompressionPlan& previous,
    qint64 actualOutputBytes);

bool isAcceptablePvCompressionOutput(qint64 originalBytes, qint64 outputBytes);

QStringList makePvCompressionPassArguments(
    const QString& inputPath,
    const QString& outputPath,
    const QString& passLogPath,
    const PvCompressionPlan& plan,
    int passNumber);

}  // namespace miacode::media
