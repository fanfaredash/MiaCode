#include "preview/scene/PreviewMuriActionLayerState.h"

#include "common/MuriConfig.h"
#include "preview/scene/PreviewSceneMath.h"

#include <algorithm>
#include <QtMath>

namespace {

QPointF interpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1 || proportion <= 0.0) {
        return points.constFirst();
    }
    if (proportion >= 1.0) {
        return points.constLast();
    }

    const qreal scaledIndex = qBound<qreal>(0.0, proportion, 1.0) * (points.size() - 1);
    const int startIndex = qFloor(scaledIndex);
    const int endIndex = qMin(points.size() - 1, startIndex + 1);
    const qreal localT = scaledIndex - startIndex;
    return points[startIndex] * (1.0 - localT) + points[endIndex] * localT;
}

}  // namespace

namespace miacode::preview::scene {

PreviewMuriActionLayerState buildPreviewMuriActionLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewMuriActionLayerState layerState;
    if (!state.muriRenderOptions.showTouchTrail || state.muriAnalysisReport.actionTrails.isEmpty()) {
        return layerState;
    }

    if (state.muriRenderOptions.renderMode == RenderMode::MaimuriDxStyle) {
        constexpr qreal kPressEffectLifetimeSeconds = static_cast<qreal>(36.0 / miacode::muri::kJudgeTps);
        constexpr qreal kPressEffectTickSeconds = static_cast<qreal>(1.0 / miacode::muri::kJudgeTps);
        constexpr qreal kPressEffectSlideRadiusEndScale = 0.5;
        constexpr qreal kMembershipEpsilon = 1e-6;

        struct VisibleTrailEntry {
            const MuriActionTrail* trail = nullptr;
            int visibleIndex = -1;
            int startTick = 0;
            int endTick = -1;
        };

        const qreal playheadSecond = static_cast<qreal>(state.playheadSeconds);
        const qreal sampleStartSecond = playheadSecond - kPressEffectLifetimeSeconds;
        QVector<VisibleTrailEntry> visibleTrails;
        visibleTrails.reserve(state.muriAnalysisReport.actionTrails.size());
        int visibleIndex = 0;
        for (const MuriActionTrail& trail : state.muriAnalysisReport.actionTrails) {
            if (trail.endSecond + 1e-6 < sampleStartSecond || trail.startSecond > playheadSecond + 1e-6) {
                continue;
            }
            if (trail.points.isEmpty()) {
                continue;
            }
            const int trailStartTick = qMax(
                0,
                static_cast<int>(qCeil((trail.startSecond - kMembershipEpsilon) * miacode::muri::kJudgeTps))
            );
            const int trailEndTick = qMax(
                trailStartTick,
                static_cast<int>(qFloor((trail.endSecond + kMembershipEpsilon) * miacode::muri::kJudgeTps))
            );
            visibleTrails.append(VisibleTrailEntry{&trail, visibleIndex, trailStartTick, trailEndTick});
            visibleIndex += 1;
        }
        if (visibleTrails.isEmpty()) {
            return layerState;
        }

        const int startTick = qMax(0, static_cast<int>(qFloor(sampleStartSecond * miacode::muri::kJudgeTps)));
        const int endTick = qMax(
            startTick,
            static_cast<int>(qFloor(playheadSecond * miacode::muri::kJudgeTps + 1e-6))
        );
        layerState.circles.reserve((endTick - startTick + 1) * qMax(1, visibleTrails.size()));
        QVector<int> activationOrder;
        QVector<int> deactivationOrder;
        activationOrder.reserve(visibleTrails.size());
        deactivationOrder.reserve(visibleTrails.size());
        for (int index = 0; index < visibleTrails.size(); ++index) {
            activationOrder.append(index);
            deactivationOrder.append(index);
        }
        std::stable_sort(activationOrder.begin(), activationOrder.end(), [&visibleTrails](int a, int b) {
            const VisibleTrailEntry& left = visibleTrails.at(a);
            const VisibleTrailEntry& right = visibleTrails.at(b);
            if (left.startTick != right.startTick) {
                return left.startTick < right.startTick;
            }
            return left.visibleIndex < right.visibleIndex;
        });
        std::stable_sort(deactivationOrder.begin(), deactivationOrder.end(), [&visibleTrails](int a, int b) {
            const VisibleTrailEntry& left = visibleTrails.at(a);
            const VisibleTrailEntry& right = visibleTrails.at(b);
            if (left.endTick != right.endTick) {
                return left.endTick < right.endTick;
            }
            return left.visibleIndex < right.visibleIndex;
        });
        QVector<int> activeVisibleIndices;
        activeVisibleIndices.reserve(visibleTrails.size());
        int activationCursor = 0;
        int deactivationCursor = 0;
        for (int tick = startTick; tick <= endTick; ++tick) {
            const qreal sampleSecond = static_cast<qreal>(tick) * kPressEffectTickSeconds;
            while (deactivationCursor < deactivationOrder.size()
                   && visibleTrails.at(deactivationOrder.at(deactivationCursor)).endTick < tick) {
                const int expiredVisibleIndex = visibleTrails.at(deactivationOrder.at(deactivationCursor)).visibleIndex;
                const auto activeIt = std::lower_bound(
                    activeVisibleIndices.begin(),
                    activeVisibleIndices.end(),
                    expiredVisibleIndex);
                if (activeIt != activeVisibleIndices.end() && *activeIt == expiredVisibleIndex) {
                    activeVisibleIndices.erase(activeIt);
                }
                deactivationCursor += 1;
            }
            while (activationCursor < activationOrder.size()
                   && visibleTrails.at(activationOrder.at(activationCursor)).startTick <= tick) {
                const int nextVisibleIndex = visibleTrails.at(activationOrder.at(activationCursor)).visibleIndex;
                const auto insertIt = std::lower_bound(
                    activeVisibleIndices.begin(),
                    activeVisibleIndices.end(),
                    nextVisibleIndex);
                if (insertIt == activeVisibleIndices.end() || *insertIt != nextVisibleIndex) {
                    activeVisibleIndices.insert(insertIt, nextVisibleIndex);
                }
                activationCursor += 1;
            }
            if (activeVisibleIndices.isEmpty()) {
                continue;
            }

            const qreal ageSeconds = playheadSecond - sampleSecond;
            if (ageSeconds < 0.0 || ageSeconds > kPressEffectLifetimeSeconds) {
                continue;
            }

            const qreal t = qBound<qreal>(0.0, ageSeconds / kPressEffectLifetimeSeconds, 1.0);
            QColor fillColor = activeVisibleIndices.size() > 2
                ? QColor(224, 108, 117)
                : QColor(255, 255, 255);
            fillColor.setAlpha(qBound(0, qRound((1.0 - t) * 255.0), 255));
            for (int activeVisibleIndex : activeVisibleIndices) {
                const MuriActionTrail* trail = visibleTrails.at(activeVisibleIndex).trail;
                if (trail == nullptr) {
                    continue;
                }
                qreal proportion = 0.0;
                if (trail->endSecond > trail->startSecond) {
                    proportion = qBound<qreal>(
                        0.0,
                        static_cast<qreal>((sampleSecond - trail->startSecond) / (trail->endSecond - trail->startSecond)),
                        1.0
                    );
                }
                const QPointF logicalPoint = trail->points.size() == 1
                    ? trail->points.constFirst()
                    : interpolatePoint(trail->points, proportion);
                qreal logicalRadius = static_cast<qreal>(trail->radius);
                if (trail->sourceType == QLatin1String("slide")
                    || trail->sourceType == QLatin1String("wifi")) {
                    logicalRadius *= (1.0 - (1.0 - kPressEffectSlideRadiusEndScale) * t);
                }

                PreviewCircleDescriptor circle;
                circle.center = mapLogicalPointToRect(logicalPoint, playfieldRect);
                circle.radiusX = qMax<qreal>(1.0, mapLogicalLengthToRect(logicalRadius, playfieldRect));
                circle.radiusY = circle.radiusX;
                circle.fillColor = fillColor;
                layerState.circles.append(circle);
            }
        }
        return layerState;
    }

    layerState.circles.reserve(state.muriAnalysisReport.actionTrails.size());
    for (const MuriActionTrail& trail : state.muriAnalysisReport.actionTrails) {
        if (trail.startSecond > state.playheadSeconds + 1e-6 || trail.endSecond + 1e-6 < state.playheadSeconds) {
            continue;
        }
        if (trail.points.isEmpty()) {
            continue;
        }

        qreal proportion = 0.0;
        if (trail.endSecond > trail.startSecond) {
            proportion = qBound<qreal>(
                0.0,
                static_cast<qreal>((state.playheadSeconds - trail.startSecond) / (trail.endSecond - trail.startSecond)),
                1.0
            );
        }
        const QPointF logicalPoint = trail.points.size() == 1
            ? trail.points.constFirst()
            : interpolatePoint(trail.points, proportion);

        PreviewCircleDescriptor circle;
        circle.center = mapLogicalPointToRect(logicalPoint, playfieldRect);
        circle.radiusX = qMax<qreal>(2.0, mapLogicalLengthToRect(static_cast<qreal>(trail.radius), playfieldRect));
        circle.radiusY = circle.radiusX;
        circle.fillColor = trail.sourceType == QLatin1String("wifi")
            ? QColor(75, 218, 255, 60)
            : QColor(255, 112, 84, 64);
        circle.strokeColor = trail.sourceType == QLatin1String("wifi")
            ? QColor(150, 240, 255, 220)
            : QColor(255, 182, 120, 220);
        circle.strokeWidth = qMax<qreal>(2.0, circle.radiusX * 0.08);
        layerState.circles.append(circle);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
