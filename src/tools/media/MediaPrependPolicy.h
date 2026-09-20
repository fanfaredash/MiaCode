#pragma once

#include "PvCompressionPolicy.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace miacode::media {

inline constexpr double kAudioPrependFadeSeconds = 0.005;
inline constexpr double kPvPrependContainerAllowanceRatio = 1.02;
inline constexpr double kPvPrependOutputToleranceRatio = 1.02;

QStringList makeAudioPrependArguments(
    const QString& inputPath,
    const QString& outputPath,
    const QString& outputSuffix,
    double silenceSeconds);

PvCompressionPlan makePvPrependCompressionPlan(
    qint64 originalBytes,
    double inputDurationSeconds,
    double blackSeconds);

QStringList makePvPrependPassArguments(
    const QString& inputPath,
    const QString& outputPath,
    const QString& passLogPath,
    const PvCompressionPlan& plan,
    double blackSeconds,
    int passNumber);

bool isAcceptablePvPrependOutput(
    const PvCompressionPlan& plan,
    qint64 outputBytes);

}  // namespace miacode::media
