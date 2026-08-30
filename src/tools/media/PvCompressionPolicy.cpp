#include "PvCompressionPolicy.h"

#include <QProcess>

#include <algorithm>
#include <cmath>

namespace miacode::media {

PvCompressionPlan makePvCompressionPlan(double durationSeconds)
{
    PvCompressionPlan plan;
    if (!(durationSeconds > 0.0)) {
        return plan;
    }

    const double targetBits = static_cast<double>(plan.targetBytes) * 8.0;
    plan.videoBitrateKbps = std::max(
        1,
        static_cast<int>(std::floor(targetBits / durationSeconds / 1000.0)));
    return plan;
}

PvCompressionPlan adjustedPvCompressionPlan(
    const PvCompressionPlan& previous,
    qint64 actualOutputBytes)
{
    PvCompressionPlan adjusted = previous;
    if (previous.videoBitrateKbps <= 0 || actualOutputBytes <= 0) {
        return adjusted;
    }

    const double corrected = static_cast<double>(previous.videoBitrateKbps)
        * static_cast<double>(previous.targetBytes)
        / static_cast<double>(actualOutputBytes)
        * kPvCompressionRetrySafetyRatio;
    adjusted.videoBitrateKbps = std::max(1, static_cast<int>(std::floor(corrected)));
    return adjusted;
}

bool isAcceptablePvCompressionOutput(qint64 originalBytes, qint64 outputBytes)
{
    return outputBytes > 0
        && outputBytes < kPvCompressionHardLimitBytes
        && (originalBytes <= 0 || outputBytes < originalBytes);
}

QStringList makePvCompressionPassArguments(
    const QString& inputPath,
    const QString& outputPath,
    const QString& passLogPath,
    const PvCompressionPlan& plan,
    int passNumber)
{
    QStringList args{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-nostdin"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), inputPath,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-map_metadata"), QStringLiteral("-1"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"),
        QStringLiteral("-preset"), QStringLiteral("slow"),
        QStringLiteral("-b:v"), QStringLiteral("%1k").arg(plan.videoBitrateKbps),
        QStringLiteral("-vf"), QStringLiteral("scale='min(1280,iw)':-2"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
        QStringLiteral("-an"),
        QStringLiteral("-pass"), QString::number(passNumber),
        QStringLiteral("-passlogfile"), passLogPath,
    };

    if (passNumber == 1) {
        args << QStringLiteral("-f") << QStringLiteral("null") << QProcess::nullDevice();
    } else {
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart") << outputPath;
    }
    return args;
}

}  // namespace miacode::media
