#pragma once

#include <QPointF>
#include <QString>
#include <QtMath>

namespace miacode::muri {

constexpr double kJudgeTps = 180.0;
constexpr int kStaticTapOnSlideThresholdMinMs = 150;
constexpr int kStaticTapOnSlideThresholdMaxMs = 250;
constexpr int kStaticTapOnSlideThresholdDefaultMs = 200;
constexpr double kReleaseDelaySeconds = 3.0 / kJudgeTps;
constexpr double kExtraPadDownDelaySeconds = 9.0 / kJudgeTps;
constexpr double kTapCriticalSeconds = 3.0 / kJudgeTps;
constexpr double kTapAvailableSeconds = 27.0 / kJudgeTps;
constexpr double kTouchCriticalSeconds = 27.0 / kJudgeTps;
constexpr double kTouchAvailableSeconds = 27.0 / kJudgeTps;
constexpr double kSlideCriticalSeconds = 42.0 / kJudgeTps;
constexpr double kSlideAvailableSeconds = 24.0 * 60.0 * 60.0;
constexpr double kSlideDeltaShiftSeconds = 9.0 / kJudgeTps;
constexpr double kSlideLeadingSeconds = 15.0 / kJudgeTps;
constexpr double kTapOnSlideThresholdSeconds = 1.0 / kJudgeTps;
constexpr double kTapOnSlideWarningCutoffSeconds = 0.150;
constexpr double kTouchOnSlideThresholdSeconds = 24.0 / kJudgeTps;
constexpr double kSlideTooFastThresholdSeconds = 1.0 / kJudgeTps;
constexpr double kSlideHeadTapNoTapWarningCutoffSeconds = 0.050;
constexpr double kSlideHeadTapLateWarningCutoffSeconds = 0.150;

constexpr double kLogicalCanvasSize = 540.0;
constexpr double kLogicalCanvasCenter = kLogicalCanvasSize / 2.0;
constexpr double kPadRadiusA = kLogicalCanvasSize * 100.0 / 1080.0;
constexpr double kPadRadiusB = kLogicalCanvasSize * 75.0 / 1080.0;
constexpr double kPadRadiusC = kLogicalCanvasSize * 105.0 / 1080.0;
constexpr double kPadRadiusD = kLogicalCanvasSize * 65.0 / 1080.0;
constexpr double kPadRadiusE = kLogicalCanvasSize * 60.0 / 1080.0;
constexpr double kPadDistanceA = kLogicalCanvasSize * 410.0 / 1080.0;
constexpr double kPadDistanceB = kLogicalCanvasSize * 220.0 / 1080.0;
constexpr double kPadDistanceD = kLogicalCanvasSize * 440.0 / 1080.0;
constexpr double kPadDistanceE = kLogicalCanvasSize * 310.0 / 1080.0;
constexpr double kPadAngleBaseDegrees = -67.5;
constexpr double kPadAngleStepDegrees = 45.0;
constexpr double kHandRadiusNormal = kLogicalCanvasSize * 60.0 / 1080.0;
constexpr double kHandRadiusWifi = kLogicalCanvasSize * 100.0 / 1080.0;
constexpr double kHandRadiusMax = kLogicalCanvasSize * 180.0 / 1080.0;
constexpr double kSlideMergeDistance = kLogicalCanvasSize * 20.0 / 1080.0;
constexpr double kSlideMergeTangentDelta = 0.05235389661574631;

inline bool padTokenIsValid(const QString& pad)
{
    if (pad == QLatin1String("C")) {
        return true;
    }
    if (pad.size() != 2) {
        return false;
    }
    const QChar ring = pad.at(0).toUpper();
    const QChar laneToken = pad.at(1);
    return (ring == QLatin1Char('A')
            || ring == QLatin1Char('B')
            || ring == QLatin1Char('D')
            || ring == QLatin1Char('E'))
        && laneToken >= QLatin1Char('1')
        && laneToken <= QLatin1Char('8');
}

inline double padRadius(const QString& pad)
{
    if (pad.isEmpty()) {
        return 0.0;
    }
    const QChar ring = pad.at(0).toUpper();
    if (pad == QLatin1String("C")) {
        return kPadRadiusC;
    }
    switch (ring.toLatin1()) {
    case 'A':
        return kPadRadiusA;
    case 'B':
        return kPadRadiusB;
    case 'D':
        return kPadRadiusD;
    case 'E':
        return kPadRadiusE;
    default:
        return 0.0;
    }
}

inline double padAngleDegrees(const QString& pad)
{
    if (!padTokenIsValid(pad) || pad == QLatin1String("C")) {
        return 0.0;
    }
    const int lane = pad.mid(1).toInt();
    const QChar ring = pad.at(0).toUpper();
    if (ring == QLatin1Char('D') || ring == QLatin1Char('E')) {
        return -90.0 + (lane - 1) * kPadAngleStepDegrees;
    }
    return kPadAngleBaseDegrees + (lane - 1) * kPadAngleStepDegrees;
}

inline QPointF padCenter(const QString& pad)
{
    if (pad == QLatin1String("C")) {
        return QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter);
    }
    if (!padTokenIsValid(pad)) {
        return QPointF();
    }

    double distance = 0.0;
    switch (pad.at(0).toUpper().toLatin1()) {
    case 'A':
        distance = kPadDistanceA;
        break;
    case 'B':
        distance = kPadDistanceB;
        break;
    case 'D':
        distance = kPadDistanceD;
        break;
    case 'E':
        distance = kPadDistanceE;
        break;
    default:
        return QPointF();
    }

    const double radians = qDegreesToRadians(padAngleDegrees(pad));
    return QPointF(
        kLogicalCanvasCenter + qCos(radians) * distance,
        kLogicalCanvasCenter + qSin(radians) * distance
    );
}

}  // namespace miacode::muri
