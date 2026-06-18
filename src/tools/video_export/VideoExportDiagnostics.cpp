#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/IntroConfig.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "common/PreviewSfxTimeline.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// VideoExportDiagnostics.cpp — render-backend fallback detail, diagnostic tap/slide sampling, visible-object trace, per-layer activity estimation, FNV hashing, and frame-signature / mean-abs-diff comparisons.
//
// Definitions extracted verbatim from the original VideoExportController.cpp
// during the god-file split. All helpers live in the shared
// miacode::video_export::detail namespace (declared in
// VideoExportControllerInternal.h).
using namespace miacode::video_export::detail;

namespace miacode::video_export::detail {

void appendRenderBackendFallbackDetail(QString* detail, const QString& entry)
{
    if (detail == nullptr || entry.isEmpty()) {
        return;
    }
    if (detail->isEmpty()) {
        *detail = entry;
        return;
    }
    *detail += QStringLiteral("; ") + entry;
}

DiagTapApproachSample diagSampleTapApproach(double deltaSeconds)
{
    DiagTapApproachSample sample;
    if (deltaSeconds <= -kDiagTapLifecycleDurationSeconds) {
        return sample;
    }
    if (deltaSeconds < -kDiagTapFlyDurationSeconds) {
        const double progress = qBound(
            0.0,
            (deltaSeconds + kDiagTapLifecycleDurationSeconds) / qMax(0.001, kDiagTapSpawnDurationSeconds),
            1.0
        );
        sample.scale = progress;
        return sample;
    }
    if (deltaSeconds < 0.0) {
        const double flightProgress = qBound(
            0.0,
            (deltaSeconds + kDiagTapFlyDurationSeconds) / qMax(0.001, kDiagTapFlyDurationSeconds),
            1.0
        );
        sample.distance = kDiagLogicalDistanceTap + (kDiagLogicalDistanceEdge - kDiagLogicalDistanceTap) * flightProgress;
        sample.scale = 1.0;
        return sample;
    }
    sample.distance = kDiagLogicalDistanceEdge;
    sample.scale = 1.0;
    return sample;
}

bool diagTapSpriteVisible(double deltaSeconds)
{
    if (deltaSeconds > 0.0) {
        return false;
    }
    return diagSampleTapApproach(deltaSeconds).scale > 0.0;
}

bool diagSlideHeadVisible(double deltaSeconds)
{
    if (deltaSeconds >= 0.0) {
        return false;
    }
    return diagSampleTapApproach(deltaSeconds).scale > 0.0;
}

QPointF diagLaneUnitVector(int lane)
{
    if (lane < 1 || lane > 8) {
        return QPointF(0.0, 0.0);
    }
    const double angleDeg = -67.5 + (lane - 1) * 45.0;
    const double angleRad = qDegreesToRadians(angleDeg);
    return QPointF(qCos(angleRad), qSin(angleRad));
}

QRectF diagPlayfieldRect(int width, int height, double layoutSquareScale)
{
    const QRectF stageRect(0.0, 0.0, qMax(1, width), qMax(1, height));
    return miacode::preview_video::centeredLayoutRectForStage(stageRect, layoutSquareScale);
}

QPointF diagMapLogicalPointToPlayfield(const QPointF& logicalPoint, const QRectF& playfieldRect)
{
    const qreal scale = playfieldRect.width() / kDiagLogicalCanvasSize;
    return QPointF(
        playfieldRect.left() + logicalPoint.x() * scale,
        playfieldRect.top() + logicalPoint.y() * scale
    );
}

QPointF diagInterpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1) {
        return points.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (points.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), points.size() - 2);
    const qreal t = scaled - index;
    const QPointF& a = points[index];
    const QPointF& b = points[index + 1];
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

qreal diagInterpolateAngle(const QVector<double>& angles, qreal proportion)
{
    if (angles.isEmpty()) {
        return 0.0;
    }
    if (angles.size() == 1) {
        return angles.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (angles.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), angles.size() - 2);
    const qreal t = scaled - index;
    const qreal a = angles[index];
    const qreal b = angles[index + 1];
    qreal delta = std::fmod(b - a + 540.0, 360.0) - 180.0;
    return a + delta * t;
}

QString markerTraceKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("L%1C%2")
        .arg(qMax(1, marker.sourceLine))
        .arg(qMax(1, marker.sourceCol));
}

QVector<ObjectTraceItem> collectVisibleObjectTrace(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond,
    int frameWidth,
    int frameHeight,
    double layoutSquareScale
)
{
    QVector<ObjectTraceItem> items;
    const QRectF playfield = diagPlayfieldRect(frameWidth, frameHeight, layoutSquareScale);
    for (const TimelineNoteMarker& marker : markers) {
        const QString keyBase = markerTraceKey(marker);

        if (marker.type == QLatin1String("tap")) {
            const double delta = playheadSecond - marker.second;
            if (delta > 0.0) {
                continue;
            }
            const DiagTapApproachSample approach = diagSampleTapApproach(delta);
            if (approach.scale <= 0.0) {
                continue;
            }
            const QPointF lane = diagLaneUnitVector(marker.lane);
            const QPointF logical(
                kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                kDiagLogicalCanvasCenter + lane.y() * approach.distance
            );
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("tap");
            item.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
            item.extra = QStringLiteral("lane=%1").arg(marker.lane);
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond < marker.second) {
                continue;
            }
            const double delta = playheadSecond - marker.second;
            const double deltaEnd = playheadSecond - marker.endSecond;
            if (deltaEnd > 0.0) {
                continue;
            }
            const DiagTapApproachSample headApproach = diagSampleTapApproach(delta);
            if (headApproach.scale <= 0.0) {
                continue;
            }
            const QPointF lane = diagLaneUnitVector(marker.lane);
            if (delta < -kDiagTapFlyDurationSeconds) {
                const QPointF logical(
                    kDiagLogicalCanvasCenter + lane.x() * kDiagLogicalDistanceTap,
                    kDiagLogicalCanvasCenter + lane.y() * kDiagLogicalDistanceTap
                );
                ObjectTraceItem item;
                item.key = keyBase + QStringLiteral(":spawn");
                item.type = QStringLiteral("hold");
                item.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                item.extra = QStringLiteral("lane=%1,phase=spawn").arg(marker.lane);
                items.append(item);
                continue;
            }
            const double distance = headApproach.distance;
            double distanceEnd = deltaEnd * kDiagTapUnitsPerSecond + kDiagLogicalDistanceEdge;
            distanceEnd = qBound(kDiagLogicalDistanceTap, distanceEnd, kDiagLogicalDistanceEdge);
            const QPointF logicalHead(
                kDiagLogicalCanvasCenter + lane.x() * distance,
                kDiagLogicalCanvasCenter + lane.y() * distance
            );
            const QPointF logicalTail(
                kDiagLogicalCanvasCenter + lane.x() * distanceEnd,
                kDiagLogicalCanvasCenter + lane.y() * distanceEnd
            );
            ObjectTraceItem head;
            head.key = keyBase + QStringLiteral(":head");
            head.type = QStringLiteral("hold");
            head.posPx = diagMapLogicalPointToPlayfield(logicalHead, playfield);
            head.extra = QStringLiteral("lane=%1,role=head").arg(marker.lane);
            items.append(head);

            ObjectTraceItem tail;
            tail.key = keyBase + QStringLiteral(":tail");
            tail.type = QStringLiteral("hold");
            tail.posPx = diagMapLogicalPointToPlayfield(logicalTail, playfield);
            tail.extra = QStringLiteral("lane=%1,role=tail").arg(marker.lane);
            items.append(tail);
            continue;
        }

        if (marker.type == QLatin1String("slide")) {
            if (marker.hasHeadStar) {
                const double delta = playheadSecond - marker.second;
                if (delta < 0.0) {
                    const DiagTapApproachSample approach = diagSampleTapApproach(delta);
                    if (approach.scale > 0.0) {
                        const QPointF lane = diagLaneUnitVector(marker.lane);
                        const QPointF logical(
                            kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                            kDiagLogicalCanvasCenter + lane.y() * approach.distance
                        );
                        ObjectTraceItem headStar;
                        headStar.key = keyBase + QStringLiteral(":head_pre");
                        headStar.type = QStringLiteral("slide_head");
                        headStar.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                        headStar.extra = QStringLiteral("lane=%1").arg(marker.lane);
                        items.append(headStar);
                    }
                }
            }
            if (marker.slideTraceSecond <= marker.second || marker.slideSegmentPoints.isEmpty()) {
                continue;
            }
            if (playheadSecond < marker.second || playheadSecond > marker.endSecond) {
                continue;
            }
            QPointF logical;
            qreal angle = 0.0;
            if (playheadSecond < marker.slideTraceSecond) {
                if (!marker.hasHeadStar || marker.slideSegmentPoints.constFirst().isEmpty()) {
                    continue;
                }
                const QVector<QPointF>& points = marker.slideSegmentPoints.constFirst();
                const QVector<double>& angles = marker.slideSegmentAngles.constFirst();
                logical = points.constFirst();
                angle = angles.isEmpty() ? 0.0 : angles.constFirst();
            } else {
                int segmentIndex = 0;
                if (!marker.slideSegmentShootSeconds.isEmpty()
                    && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()) {
                    for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                        if (playheadSecond >= marker.slideSegmentShootSeconds[i]) {
                            segmentIndex = i;
                            break;
                        }
                    }
                }
                segmentIndex = qBound(0, segmentIndex, marker.slideSegmentPoints.size() - 1);
                const QVector<QPointF>& points = marker.slideSegmentPoints[segmentIndex];
                const QVector<double>& angles = marker.slideSegmentAngles.value(segmentIndex);
                if (points.isEmpty()) {
                    continue;
                }
                qreal proportion = 1.0;
                if (segmentIndex < marker.slideSegmentShootSeconds.size()
                    && segmentIndex < marker.slideSegmentDurations.size()) {
                    const qreal duration = qMax(0.001, marker.slideSegmentDurations[segmentIndex]);
                    proportion = qBound<qreal>(
                        0.0,
                        (playheadSecond - marker.slideSegmentShootSeconds[segmentIndex]) / duration,
                        1.0
                    );
                }
                logical = diagInterpolatePoint(points, proportion);
                angle = diagInterpolateAngle(angles, proportion);
            }
            ObjectTraceItem item;
            item.key = keyBase + QStringLiteral(":star");
            item.type = QStringLiteral("slide_star");
            item.posPx = diagMapLogicalPointToPlayfield(
                QPointF(kDiagLogicalCanvasCenter + logical.x(), kDiagLogicalCanvasCenter + logical.y()),
                playfield
            );
            item.extra = QStringLiteral("lane=%1,angle=%2").arg(marker.lane).arg(angle, 0, 'f', 2);
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                const double delta = playheadSecond - marker.second;
                if (delta < 0.0) {
                    const DiagTapApproachSample approach = diagSampleTapApproach(delta);
                    if (approach.scale > 0.0) {
                        const QPointF lane = diagLaneUnitVector(marker.lane);
                        const QPointF logical(
                            kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                            kDiagLogicalCanvasCenter + lane.y() * approach.distance
                        );
                        ObjectTraceItem headStar;
                        headStar.key = keyBase + QStringLiteral(":head_pre");
                        headStar.type = QStringLiteral("wifi_head");
                        headStar.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                        headStar.extra = QStringLiteral("lane=%1").arg(marker.lane);
                        items.append(headStar);
                    }
                }
            }
            if (marker.slideTraceSecond <= marker.second || marker.wifiLanePoints.isEmpty()) {
                continue;
            }
            if (playheadSecond < marker.second || playheadSecond > marker.endSecond) {
                continue;
            }
            bool waiting = playheadSecond < marker.slideTraceSecond;
            qreal proportion = 0.0;
            if (!waiting && !marker.slideSegmentDurations.isEmpty()) {
                const qreal duration = qMax(0.001, marker.slideSegmentDurations.constFirst());
                proportion = qBound<qreal>(0.0, (playheadSecond - marker.slideTraceSecond) / duration, 1.0);
            } else if (!waiting && marker.endSecond > marker.slideTraceSecond) {
                const qreal duration = qMax(0.001, marker.endSecond - marker.slideTraceSecond);
                proportion = qBound<qreal>(0.0, (playheadSecond - marker.slideTraceSecond) / duration, 1.0);
            }
            for (int laneIndex = 0; laneIndex < marker.wifiLanePoints.size(); ++laneIndex) {
                const QVector<QPointF>& points = marker.wifiLanePoints[laneIndex];
                const QVector<double>& angles = marker.wifiLaneAngles.value(laneIndex);
                if (points.isEmpty()) {
                    continue;
                }
                const QPointF logical = waiting ? points.constFirst() : diagInterpolatePoint(points, proportion);
                const qreal angle = waiting
                    ? (angles.isEmpty() ? 0.0 : angles.constFirst())
                    : diagInterpolateAngle(angles, proportion);
                ObjectTraceItem item;
                item.key = keyBase + QStringLiteral(":lane%1").arg(laneIndex);
                item.type = QStringLiteral("wifi_star");
                item.posPx = diagMapLogicalPointToPlayfield(
                    QPointF(kDiagLogicalCanvasCenter + logical.x(), kDiagLogicalCanvasCenter + logical.y()),
                    playfield
                );
                item.extra = QStringLiteral("lane=%1,angle=%2").arg(marker.lane).arg(angle, 0, 'f', 2);
                items.append(item);
            }
            continue;
        }

        if (marker.type == QLatin1String("touch")) {
            if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
                continue;
            }
            const qreal delta = playheadSecond - marker.second;
            if (delta <= -kDiagTouchDurationSeconds || delta >= 0.0) {
                continue;
            }
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("touch");
            item.posPx = diagMapLogicalPointToPlayfield(marker.touchPoint, playfield);
            item.extra = marker.touchPad;
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("touch_hold")) {
            if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
                continue;
            }
            if (marker.endSecond <= marker.second) {
                continue;
            }
            const qreal delta = playheadSecond - marker.second;
            const qreal duration = qMax<qreal>(0.001, marker.endSecond - marker.second);
            if (delta <= -kDiagTouchDurationSeconds || delta >= duration) {
                continue;
            }
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("touch_hold");
            item.posPx = diagMapLogicalPointToPlayfield(marker.touchPoint, playfield);
            item.extra = marker.touchPad;
            items.append(item);
            continue;
        }
    }

    std::sort(items.begin(), items.end(), [](const ObjectTraceItem& a, const ObjectTraceItem& b) {
        if (a.type != b.type) {
            return a.type < b.type;
        }
        return a.key < b.key;
    });
    return items;
}

FrameLayerActivityStats estimateFrameLayerActivity(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond
)
{
    FrameLayerActivityStats stats;
    for (const TimelineNoteMarker& marker : markers) {
        if (marker.type == QLatin1String("tap")) {
            const double delta = playheadSecond - marker.second;
            const DiagTapApproachSample approach = diagSampleTapApproach(delta);
            if (delta <= 0.0 && approach.scale > 0.0) {
                ++stats.tapVisible;
                if (approach.distance <= kDiagLogicalDistanceTap) {
                    ++stats.tapParked;
                }
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectDurationSeconds) {
                ++stats.judgeTapVisible;
            }
            continue;
        }

        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond >= marker.second) {
                const double delta = playheadSecond - marker.second;
                const double deltaEnd = playheadSecond - marker.endSecond;
                if (deltaEnd <= 0.0 && diagTapSpriteVisible(delta)) {
                    ++stats.holdVisible;
                }
                if (playheadSecond >= marker.second && playheadSecond < marker.endSecond) {
                    ++stats.holdSustainVisible;
                }
                if (deltaEnd >= 0.0 && deltaEnd <= kDiagJudgeEffectDurationSeconds) {
                    ++stats.judgeTapVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            const bool isWifi = marker.type == QLatin1String("wifi");
            const double delta = playheadSecond - marker.second;
            if (marker.hasHeadStar && diagSlideHeadVisible(delta)) {
                ++stats.tapVisible;
                if (delta < -kDiagTapFlyDurationSeconds) {
                    ++stats.tapParked;
                }
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectDurationSeconds && marker.hasHeadStar) {
                ++stats.judgeTapVisible;
            }
            if (playheadSecond >= marker.second && playheadSecond <= marker.endSecond) {
                if (isWifi) {
                    ++stats.wifiMotionVisible;
                } else {
                    ++stats.slideMotionVisible;
                }
            }
            if (marker.availableSecond >= 0.0
                && playheadSecond >= marker.second - kDiagSlideTrackAppearLeadInSeconds
                && !(marker.endSecond > marker.slideTraceSecond && playheadSecond >= marker.endSecond)) {
                if (isWifi) {
                    ++stats.wifiTrackVisible;
                } else {
                    ++stats.slideTrackVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("touch")) {
            const double delta = playheadSecond - marker.second;
            if (delta > -kDiagTouchDurationSeconds && delta < 0.0) {
                ++stats.touchVisible;
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectTouchDurationSeconds) {
                ++stats.judgeTouchVisible;
            }
            if (marker.isFirework) {
                const double fireworkElapsed = delta - kDiagJudgeEffectFireworkTouchTriggerDelaySeconds;
                if (fireworkElapsed >= 0.0 && fireworkElapsed <= kDiagJudgeEffectFireworkDurationSeconds) {
                    ++stats.judgeFireworkVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("touch_hold")) {
            if (marker.endSecond <= marker.second) {
                continue;
            }
            const double delta = playheadSecond - marker.second;
            const double holdDuration = marker.endSecond - marker.second;
            if (delta > -kDiagTouchDurationSeconds && delta < holdDuration) {
                ++stats.touchHoldVisible;
            }
            if (playheadSecond >= marker.second && playheadSecond < marker.endSecond) {
                ++stats.holdSustainVisible;
            }
            const double deltaEnd = playheadSecond - marker.endSecond;
            if (deltaEnd >= 0.0 && deltaEnd <= kDiagJudgeEffectDurationSeconds) {
                ++stats.judgeTapVisible;
            }
            continue;
        }
    }
    return stats;
}

quint64 fnv1a64Bytes(const char* data, qint64 size)
{
    if (data == nullptr || size <= 0) {
        return 0;
    }
    quint64 hash = 1469598103934665603ULL;
    for (qint64 i = 0; i < size; ++i) {
        hash ^= static_cast<quint64>(static_cast<unsigned char>(data[i]));
        hash *= 1099511628211ULL;
    }
    return hash;
}

double meanAbsDiffNormalized(const QImage& lhs, const QImage& rhs)
{
    QImage a = lhs;
    if (a.format() != QImage::Format_RGBA8888) {
        a = lhs.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage b = rhs;
    if (b.format() != QImage::Format_RGBA8888) {
        b = rhs.convertToFormat(QImage::Format_RGBA8888);
    }
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return -1.0;
    }

    qint64 totalAbs = 0;
    const int width = a.width();
    const int height = a.height();
    const int rowBytes = width * 4;
    for (int y = 0; y < height; ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < rowBytes; ++x) {
            totalAbs += qAbs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
        }
    }
    const double denom = static_cast<double>(width) * height * 4.0 * 255.0;
    return denom > 0.0 ? static_cast<double>(totalAbs) / denom : -1.0;
}

double meanAbsDiffNormalizedRect(const QImage& lhs, const QImage& rhs, const QRect& rect)
{
    QImage a = lhs;
    if (a.format() != QImage::Format_RGBA8888) {
        a = lhs.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage b = rhs;
    if (b.format() != QImage::Format_RGBA8888) {
        b = rhs.convertToFormat(QImage::Format_RGBA8888);
    }
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return -1.0;
    }

    const int width = a.width();
    const int height = a.height();
    const int x0 = qBound(0, rect.left(), width);
    const int y0 = qBound(0, rect.top(), height);
    const int x1 = qBound(0, rect.right() + 1, width);
    const int y1 = qBound(0, rect.bottom() + 1, height);
    if (x1 <= x0 || y1 <= y0) {
        return -1.0;
    }

    qint64 totalAbs = 0;
    for (int y = y0; y < y1; ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = x0; x < x1; ++x) {
            const int p = x * 4;
            totalAbs += qAbs(static_cast<int>(rowA[p + 0]) - static_cast<int>(rowB[p + 0]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 1]) - static_cast<int>(rowB[p + 1]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 2]) - static_cast<int>(rowB[p + 2]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 3]) - static_cast<int>(rowB[p + 3]));
        }
    }
    const double denom = static_cast<double>(x1 - x0) * (y1 - y0) * 4.0 * 255.0;
    return denom > 0.0 ? static_cast<double>(totalAbs) / denom : -1.0;
}

double meanAbsDiffAroundTraceItems(
    const QImage& lhs,
    const QImage& rhs,
    const QVector<ObjectTraceItem>& traceItems,
    int radius,
    double* maxDiffOut
)
{
    if (maxDiffOut != nullptr) {
        *maxDiffOut = -1.0;
    }
    if (traceItems.isEmpty()) {
        return -1.0;
    }

    const int clampedRadius = qBound(2, radius, 512);
    double sumDiff = 0.0;
    double maxDiff = -1.0;
    int count = 0;
    for (const ObjectTraceItem& item : traceItems) {
        const int cx = qRound(item.posPx.x());
        const int cy = qRound(item.posPx.y());
        const QRect roi(
            cx - clampedRadius,
            cy - clampedRadius,
            clampedRadius * 2 + 1,
            clampedRadius * 2 + 1
        );
        const double diff = meanAbsDiffNormalizedRect(lhs, rhs, roi);
        if (diff < 0.0) {
            continue;
        }
        sumDiff += diff;
        maxDiff = qMax(maxDiff, diff);
        ++count;
    }
    if (maxDiffOut != nullptr) {
        *maxDiffOut = maxDiff;
    }
    if (count <= 0) {
        return -1.0;
    }
    return sumDiff / count;
}

quint64 sampledFrameSignature(const QImage& frame)
{
    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888) {
        rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    }
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const int stepX = qMax(1, width / 16);
    const int stepY = qMax(1, height / 16);

    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < height; y += stepY) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < width; x += stepX) {
            const uchar* px = row + (x * 4);
            hash ^= static_cast<quint64>(px[0]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[1]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[2]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[3]);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<quint64>(width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(height);
    return hash;
}

quint64 fullFrameSignature(const QImage& frame, int cropBottom)
{
    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888) {
        rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    }
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const int clampedCrop = qBound(0, cropBottom, height - 1);
    const int hashHeight = height - clampedCrop;
    const int packedWidthBytes = width * 4;
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < hashHeight; ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < packedWidthBytes; ++x) {
            hash ^= static_cast<quint64>(row[x]);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<quint64>(width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(hashHeight);
    return hash;
}

quint64 objectOnlyFrameSignature(
    const QImage& frameWithObjects,
    const QImage& frameWithoutObjects,
    int diffThreshold,
    int* activePixelCount
)
{
    if (activePixelCount != nullptr) {
        *activePixelCount = 0;
    }

    QImage withRgba = frameWithObjects;
    if (withRgba.format() != QImage::Format_RGBA8888) {
        withRgba = frameWithObjects.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage withoutRgba = frameWithoutObjects;
    if (withoutRgba.format() != QImage::Format_RGBA8888) {
        withoutRgba = frameWithoutObjects.convertToFormat(QImage::Format_RGBA8888);
    }
    if (withRgba.size() != withoutRgba.size()) {
        return 0;
    }

    const int width = withRgba.width();
    const int height = withRgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const int clampedThreshold = qBound(0, diffThreshold, 4 * 255);
    quint64 hash = 1469598103934665603ULL;
    int active = 0;
    for (int y = 0; y < height; ++y) {
        const uchar* rowWith = withRgba.constScanLine(y);
        const uchar* rowWithout = withoutRgba.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int p = x * 4;
            const int d0 = qAbs(static_cast<int>(rowWith[p + 0]) - static_cast<int>(rowWithout[p + 0]));
            const int d1 = qAbs(static_cast<int>(rowWith[p + 1]) - static_cast<int>(rowWithout[p + 1]));
            const int d2 = qAbs(static_cast<int>(rowWith[p + 2]) - static_cast<int>(rowWithout[p + 2]));
            const int d3 = qAbs(static_cast<int>(rowWith[p + 3]) - static_cast<int>(rowWithout[p + 3]));
            const int score = d0 + d1 + d2 + d3;
            if (score <= clampedThreshold) {
                continue;
            }

            ++active;
            hash ^= static_cast<quint64>(x);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(y);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 0]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 1]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 2]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 3]);
            hash *= 1099511628211ULL;
        }
    }
    if (activePixelCount != nullptr) {
        *activePixelCount = active;
    }
    if (active == 0) {
        return 0;
    }
    hash ^= static_cast<quint64>(active);
    hash *= 1099511628211ULL;
    return hash;
}

}  // namespace miacode::video_export::detail
