#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "preview/scene/PreviewActiveMarkerView.h"
#include "preview/scene/PreviewMuriActionLayerState.h"
#include "preview/scene/PreviewPreparedSceneCache.h"
#include "preview/scene/PreviewSceneConstants.h"
#include "preview/scene/PreviewSceneMath.h"
#include "preview/scene/PreviewTrackLayerState.h"

namespace {

using miacode::preview::scene::PreviewActiveMarkerView;
using miacode::preview::scene::PreviewCircleDescriptor;
using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewMuriActionLayerState;
using miacode::preview::scene::PreviewPreparedLayerWindow;
using miacode::preview::scene::PreviewPreparedMarkerEntry;
using miacode::preview::scene::PreviewPreparedSceneCache;
using miacode::preview::scene::PreviewTrackLayerState;

constexpr qreal kEpsilon = 1e-3;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool requireNear(qreal actual, qreal expected, const QString& label, QTextStream& err)
{
    if (qAbs(actual - expected) <= kEpsilon) {
        return true;
    }
    err << label << " expected " << expected << " but got " << actual << Qt::endl;
    return false;
}

QImage solidImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(qRgba(255, 255, 255, 255));
    return image;
}

TimelineNoteMarker makeMarker(double second, const QString& type)
{
    TimelineNoteMarker marker;
    marker.second = second;
    marker.type = type;
    marker.lane = 1;
    marker.endLane = 1;
    return marker;
}

QString markerAnalysisBaseKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(marker.type)
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.endLane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.slideTrackKey);
}

bool requireCircleNear(
    const PreviewCircleDescriptor& actual,
    const PreviewCircleDescriptor& expected,
    const QString& label,
    QTextStream& err)
{
    return requireNear(actual.center.x(), expected.center.x(), label + QStringLiteral(" center.x"), err)
        && requireNear(actual.center.y(), expected.center.y(), label + QStringLiteral(" center.y"), err)
        && requireNear(actual.radiusX, expected.radiusX, label + QStringLiteral(" radiusX"), err)
        && requireNear(actual.radiusY, expected.radiusY, label + QStringLiteral(" radiusY"), err)
        && require(actual.fillColor == expected.fillColor, label + QStringLiteral(" fillColor"), err);
}

bool verifyActiveMarkerViewMatchesCollectMarkers(QTextStream& err)
{
    QVector<TimelineNoteMarker> markers = {
        makeMarker(1.0, QStringLiteral("tap")),
        makeMarker(2.0, QStringLiteral("hold")),
        makeMarker(3.0, QStringLiteral("slide")),
    };

    PreviewPreparedLayerWindow<PreviewPreparedMarkerEntry> layer;
    layer.entries.append(PreviewPreparedMarkerEntry{2, 0.0, 4.0, -1});
    layer.entries.append(PreviewPreparedMarkerEntry{0, 1.0, 2.0, -1});
    layer.entries.append(PreviewPreparedMarkerEntry{1, 1.5, 3.5, -1});
    miacode::preview::scene::finalizePreparedLayerWindow(&layer);
    layer.drawOrder = {1, 0, 2};

    miacode::preview::scene::PreviewLayerWindowCursor cursor;
    miacode::preview::scene::syncPreviewLayerWindowCursor(layer, 1.75, &cursor);

    PreviewPreparedSceneCache preparedCache;
    QVector<TimelineNoteMarker> collectedMarkers;
    QVector<int> collectedMarkerIndices;
    preparedCache.collectMarkers(markers, layer, cursor.activePreparedIndices, &collectedMarkers, &collectedMarkerIndices);

    const PreviewActiveMarkerView view(markers, layer, cursor);
    if (!require(view.size() == collectedMarkers.size(), QStringLiteral("active marker view size matches collected markers"), err)) {
        return false;
    }
    if (!require(view.preparedDrawOrder() == layer.drawOrder, QStringLiteral("active marker view exposes prepared draw order"), err)) {
        return false;
    }
    for (int index = 0; index < view.size(); ++index) {
        if (!require(view.sourceIndexAt(index) == collectedMarkerIndices.at(index), QStringLiteral("active marker view source index matches collected marker index"), err)) {
            return false;
        }
        if (!require(view.markerAt(index).type == collectedMarkers.at(index).type, QStringLiteral("active marker view marker type matches collected marker"), err)) {
            return false;
        }
    }
    if (!require(view.isPreparedIndexActive(0), QStringLiteral("first prepared marker is active"), err)) {
        return false;
    }
    if (!require(view.isPreparedIndexActive(1), QStringLiteral("second prepared marker is active"), err)) {
        return false;
    }
    if (!require(view.isPreparedIndexActive(2), QStringLiteral("third prepared marker is active"), err)) {
        return false;
    }

    miacode::preview::scene::syncPreviewLayerWindowCursor(layer, 3.25, &cursor);
    miacode::preview::scene::syncPreviewLayerWindowCursor(layer, 1.25, &cursor);
    preparedCache.collectMarkers(markers, layer, cursor.activePreparedIndices, &collectedMarkers, &collectedMarkerIndices);
    const PreviewActiveMarkerView seekResetView(markers, layer, cursor);
    if (!require(seekResetView.size() == collectedMarkers.size(), QStringLiteral("seek reset active marker view size matches collected markers"), err)) {
        return false;
    }
    for (int index = 0; index < seekResetView.size(); ++index) {
        if (!require(seekResetView.sourceIndexAt(index) == collectedMarkerIndices.at(index), QStringLiteral("seek reset source index matches collected marker index"), err)) {
            return false;
        }
        if (!require(seekResetView.markerAt(index).type == collectedMarkers.at(index).type, QStringLiteral("seek reset marker type matches collected marker"), err)) {
            return false;
        }
    }

    return true;
}

PreviewFrameState buildDxTrackState(bool withFallbackMarkerState)
{
    PreviewFrameState state;
    state.playheadSeconds = 1.0;
    state.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    state.muriRenderOptions.showSlideTracks = true;
    state.skin.slideTrackImage = solidImage(64, 16);

    TimelineNoteMarker marker;
    marker.type = QStringLiteral("slide");
    marker.second = 0.0;
    marker.slideTraceSecond = 0.2;
    marker.endSecond = 2.0;
    marker.availableSecond = 0.0;
    marker.lane = 1;
    marker.endLane = 3;
    marker.sourceLine = 4;
    marker.sourceCol = 7;
    marker.slideTrackKey = QStringLiteral("1-3");
    marker.slideTrackAreaPoints = {{{QPointF(0.0, 0.0), QPointF(18.0, 0.0), QPointF(36.0, 0.0)}}};
    marker.slideTrackAreaRotations = {{{0.0, 0.0, 0.0}}};
    state.noteMarkers.append(marker);

    if (withFallbackMarkerState) {
        MuriSegmentState segmentState;
        segmentState.areaCheckpoints = {QVector<MuriCheckpointState>()};
        segmentState.completedSecond = 0.5;

        MarkerMuriState markerState;
        markerState.markerType = QStringLiteral("slide");
        markerState.slideSegments.append(segmentState);
        state.muriAnalysisReport.markerStates.insert(markerAnalysisBaseKey(marker) + QStringLiteral("|branch0"), markerState);
    }

    return state;
}

PreviewFrameState buildDxTrackStateWithExactAndFallback()
{
    PreviewFrameState state = buildDxTrackState(false);
    const TimelineNoteMarker& marker = state.noteMarkers.constFirst();

    MuriSegmentState exactSegmentState;
    exactSegmentState.areaCheckpoints = {QVector<MuriCheckpointState>()};
    exactSegmentState.completedSecond = -1.0;

    MarkerMuriState exactMarkerState;
    exactMarkerState.markerType = QStringLiteral("slide");
    exactMarkerState.slideSegments.append(exactSegmentState);
    state.muriAnalysisReport.markerStates.insert(makeMarkerAnalysisKey(marker), exactMarkerState);

    MuriSegmentState fallbackSegmentState;
    fallbackSegmentState.areaCheckpoints = {QVector<MuriCheckpointState>()};
    fallbackSegmentState.completedSecond = 0.5;

    MarkerMuriState fallbackMarkerState;
    fallbackMarkerState.markerType = QStringLiteral("slide");
    fallbackMarkerState.slideSegments.append(fallbackSegmentState);
    state.muriAnalysisReport.markerStates.insert(markerAnalysisBaseKey(marker) + QStringLiteral("|branch0"), fallbackMarkerState);

    return state;
}

PreviewFrameState buildDxWifiTrackState(bool withFallbackMarkerState)
{
    PreviewFrameState state;
    state.playheadSeconds = 1.0;
    state.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    state.muriRenderOptions.showSlideTracks = true;
    state.skin.wifiImages = {solidImage(48, 16)};

    TimelineNoteMarker marker;
    marker.type = QStringLiteral("wifi");
    marker.second = 0.0;
    marker.slideTraceSecond = 0.2;
    marker.endSecond = 2.0;
    marker.availableSecond = 0.0;
    marker.lane = 1;
    marker.endLane = 5;
    marker.sourceLine = 5;
    marker.sourceCol = 9;
    marker.slideTrackKey = QStringLiteral("1w5");
    marker.wifiTrackAreaPoints = {
        {QPointF(0.0, 0.0), QPointF(12.0, 0.0)},
        {QPointF(24.0, 0.0), QPointF(36.0, 0.0)}
    };
    marker.wifiTrackAreaRotations = {
        {0.0, 0.0},
        {0.0, 0.0}
    };
    marker.wifiTrackAreaImageIndices = {
        {0, 0},
        {0, 0}
    };
    state.noteMarkers.append(marker);

    if (withFallbackMarkerState) {
        MarkerMuriState markerState;
        markerState.markerType = QStringLiteral("wifi");
        markerState.wifiLaneProgressSeconds = {{0.0, 0.4}};
        markerState.wifiCompletedSecond = 0.5;
        state.muriAnalysisReport.markerStates.insert(markerAnalysisBaseKey(marker) + QStringLiteral("|lane0"), markerState);
    }

    return state;
}

bool verifyTrackFallbackLookupAvoidsLinearScanRegression(QTextStream& err)
{
    const QRectF playfieldRect(
        0.0,
        0.0,
        miacode::preview::scene::kLogicalCanvasSize,
        miacode::preview::scene::kLogicalCanvasSize
    );

    const PreviewFrameState baselineState = buildDxTrackState(false);
    const PreviewTrackLayerState baselineLayerState =
        miacode::preview::scene::buildPreviewTrackLayerState(
            baselineState,
            PreviewActiveMarkerView(baselineState.noteMarkers),
            playfieldRect
        );
    if (!require(!baselineLayerState.sprites.isEmpty(), QStringLiteral("baseline dx slide track renders sprites without fallback state"), err)) {
        return false;
    }

    const PreviewFrameState fallbackState = buildDxTrackState(true);
    const PreviewTrackLayerState fallbackLayerState =
        miacode::preview::scene::buildPreviewTrackLayerState(
            fallbackState,
            PreviewActiveMarkerView(fallbackState.noteMarkers),
            playfieldRect
        );
    if (!require(fallbackLayerState.sprites.isEmpty(), QStringLiteral("fallback dx slide track suppresses sprites after completedSecond"), err)) {
        return false;
    }

    return true;
}

bool verifyTrackExactLookupWinsOverBaseFallback(QTextStream& err)
{
    const QRectF playfieldRect(
        0.0,
        0.0,
        miacode::preview::scene::kLogicalCanvasSize,
        miacode::preview::scene::kLogicalCanvasSize
    );
    const PreviewFrameState state = buildDxTrackStateWithExactAndFallback();
    const PreviewTrackLayerState layerState =
        miacode::preview::scene::buildPreviewTrackLayerState(
            state,
            PreviewActiveMarkerView(state.noteMarkers),
            playfieldRect
        );
    if (!require(!layerState.sprites.isEmpty(), QStringLiteral("exact marker-state lookup overrides base fallback suppression"), err)) {
        return false;
    }

    return true;
}

bool verifyWifiTrackFallbackLookupUsesBaseKey(QTextStream& err)
{
    const QRectF playfieldRect(
        0.0,
        0.0,
        miacode::preview::scene::kLogicalCanvasSize,
        miacode::preview::scene::kLogicalCanvasSize
    );

    const PreviewFrameState baselineState = buildDxWifiTrackState(false);
    const PreviewTrackLayerState baselineLayerState =
        miacode::preview::scene::buildPreviewTrackLayerState(
            baselineState,
            PreviewActiveMarkerView(baselineState.noteMarkers),
            playfieldRect
        );
    if (!require(!baselineLayerState.sprites.isEmpty(), QStringLiteral("baseline dx wifi track renders sprites without fallback state"), err)) {
        return false;
    }

    const PreviewFrameState fallbackState = buildDxWifiTrackState(true);
    const PreviewTrackLayerState fallbackLayerState =
        miacode::preview::scene::buildPreviewTrackLayerState(
            fallbackState,
            PreviewActiveMarkerView(fallbackState.noteMarkers),
            playfieldRect
        );
    if (!require(fallbackLayerState.sprites.isEmpty(), QStringLiteral("dx wifi track base-key fallback suppresses sprites after completion"), err)) {
        return false;
    }

    return true;
}

PreviewMuriActionLayerState buildNaiveDxMuriActionLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect)
{
    PreviewMuriActionLayerState layerState;
    if (!state.muriRenderOptions.showTouchTrail || state.muriAnalysisReport.actionTrails.isEmpty()) {
        return layerState;
    }

    constexpr qreal kPressEffectLifetimeSeconds = static_cast<qreal>(36.0 / miacode::muri::kJudgeTps);
    constexpr qreal kPressEffectTickSeconds = static_cast<qreal>(1.0 / miacode::muri::kJudgeTps);
    constexpr qreal kPressEffectSlideRadiusEndScale = 0.5;

    const qreal playheadSecond = static_cast<qreal>(state.playheadSeconds);
    const qreal sampleStartSecond = playheadSecond - kPressEffectLifetimeSeconds;
    QVector<const MuriActionTrail*> visibleTrails;
    visibleTrails.reserve(state.muriAnalysisReport.actionTrails.size());
    for (const MuriActionTrail& trail : state.muriAnalysisReport.actionTrails) {
        if (trail.endSecond + 1e-6 < sampleStartSecond || trail.startSecond > playheadSecond + 1e-6) {
            continue;
        }
        if (trail.points.isEmpty()) {
            continue;
        }
        visibleTrails.append(&trail);
    }

    const int startTick = qMax(0, static_cast<int>(qFloor(sampleStartSecond * miacode::muri::kJudgeTps)));
    const int endTick = qMax(
        startTick,
        static_cast<int>(qFloor(playheadSecond * miacode::muri::kJudgeTps + 1e-6))
    );
    layerState.circles.reserve((endTick - startTick + 1) * qMax(1, visibleTrails.size()));
    for (int tick = startTick; tick <= endTick; ++tick) {
        const qreal sampleSecond = static_cast<qreal>(tick) * kPressEffectTickSeconds;
        QVector<const MuriActionTrail*> activeTrails;
        activeTrails.reserve(visibleTrails.size());
        for (const MuriActionTrail* trail : visibleTrails) {
            if (trail == nullptr) {
                continue;
            }
            if (trail->startSecond <= sampleSecond + 1e-6
                && trail->endSecond + 1e-6 >= sampleSecond) {
                activeTrails.append(trail);
            }
        }
        if (activeTrails.isEmpty()) {
            continue;
        }

        const qreal ageSeconds = playheadSecond - sampleSecond;
        if (ageSeconds < 0.0 || ageSeconds > kPressEffectLifetimeSeconds) {
            continue;
        }

        const qreal t = qBound<qreal>(0.0, ageSeconds / kPressEffectLifetimeSeconds, 1.0);
        QColor fillColor = activeTrails.size() > 2
            ? QColor(224, 108, 117)
            : QColor(255, 255, 255);
        fillColor.setAlpha(qBound(0, qRound((1.0 - t) * 255.0), 255));
        for (const MuriActionTrail* trail : activeTrails) {
            qreal proportion = 0.0;
            if (trail->endSecond > trail->startSecond) {
                proportion = qBound<qreal>(
                    0.0,
                    static_cast<qreal>((sampleSecond - trail->startSecond) / (trail->endSecond - trail->startSecond)),
                    1.0
                );
            }
            QPointF logicalPoint = trail->points.constFirst();
            if (trail->points.size() > 1) {
                const qreal scaledIndex = qBound<qreal>(0.0, proportion, 1.0) * (trail->points.size() - 1);
                const int startIndex = qFloor(scaledIndex);
                const int endIndex = qMin(trail->points.size() - 1, startIndex + 1);
                const qreal localT = scaledIndex - startIndex;
                logicalPoint = trail->points[startIndex] * (1.0 - localT) + trail->points[endIndex] * localT;
            }
            qreal logicalRadius = static_cast<qreal>(trail->radius);
            if (trail->sourceType == QLatin1String("slide")
                || trail->sourceType == QLatin1String("wifi")) {
                logicalRadius *= (1.0 - (1.0 - kPressEffectSlideRadiusEndScale) * t);
            }

            PreviewCircleDescriptor circle;
            circle.center = miacode::preview::scene::mapLogicalPointToRect(logicalPoint, playfieldRect);
            circle.radiusX = qMax<qreal>(1.0, miacode::preview::scene::mapLogicalLengthToRect(logicalRadius, playfieldRect));
            circle.radiusY = circle.radiusX;
            circle.fillColor = fillColor;
            layerState.circles.append(circle);
        }
    }
    return layerState;
}

PreviewFrameState buildDxMuriActionState()
{
    PreviewFrameState state;
    state.playheadSeconds = 1.4;
    state.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    state.muriRenderOptions.showTouchTrail = true;

    MuriActionTrail tapTrail;
    tapTrail.sourceType = QStringLiteral("tap");
    tapTrail.startSecond = 0.9;
    tapTrail.endSecond = 1.4;
    tapTrail.radius = 18.0;
    tapTrail.points = {QPointF(270.0, 180.0), QPointF(270.0, 200.0)};

    MuriActionTrail slideTrail;
    slideTrail.sourceType = QStringLiteral("slide");
    slideTrail.startSecond = 1.0;
    slideTrail.endSecond = 1.5;
    slideTrail.radius = 24.0;
    slideTrail.points = {QPointF(220.0, 260.0), QPointF(260.0, 260.0), QPointF(300.0, 260.0)};

    MuriActionTrail wifiTrail;
    wifiTrail.sourceType = QStringLiteral("wifi");
    wifiTrail.startSecond = 1.1;
    wifiTrail.endSecond = 1.45;
    wifiTrail.radius = 20.0;
    wifiTrail.points = {QPointF(320.0, 320.0), QPointF(300.0, 300.0)};

    state.muriAnalysisReport.actionTrails = {tapTrail, slideTrail, wifiTrail};
    return state;
}

bool verifyDxMuriActionSweepLineMatchesNaiveReferenceForState(
    const PreviewFrameState& state,
    const QString& label,
    QTextStream& err)
{
    const QRectF playfieldRect(
        0.0,
        0.0,
        miacode::preview::scene::kLogicalCanvasSize,
        miacode::preview::scene::kLogicalCanvasSize
    );

    const PreviewMuriActionLayerState actual =
        miacode::preview::scene::buildPreviewMuriActionLayerState(state, playfieldRect);
    const PreviewMuriActionLayerState expected = buildNaiveDxMuriActionLayerState(state, playfieldRect);

    if (!require(actual.circles.size() == expected.circles.size(), label + QStringLiteral(" circle count matches naive reference"), err)) {
        return false;
    }
    for (int index = 0; index < actual.circles.size(); ++index) {
        if (!requireCircleNear(actual.circles.at(index), expected.circles.at(index), label + QStringLiteral(" circle %1").arg(index), err)) {
            return false;
        }
    }

    return true;
}

bool verifyDxMuriActionSweepLineMatchesNaiveReference(QTextStream& err)
{
    return verifyDxMuriActionSweepLineMatchesNaiveReferenceForState(
        buildDxMuriActionState(),
        QStringLiteral("dx muri action mixed trails"),
        err
    );
}

bool verifyDxMuriActionSweepLineHandlesEdgeCases(QTextStream& err)
{
    PreviewFrameState emptyState;
    emptyState.playheadSeconds = 1.0;
    emptyState.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    emptyState.muriRenderOptions.showTouchTrail = true;
    if (!verifyDxMuriActionSweepLineMatchesNaiveReferenceForState(emptyState, QStringLiteral("dx muri action empty"), err)) {
        return false;
    }

    PreviewFrameState singleState;
    singleState.playheadSeconds = 1.2;
    singleState.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    singleState.muriRenderOptions.showTouchTrail = true;
    MuriActionTrail singleTrail;
    singleTrail.sourceType = QStringLiteral("tap");
    singleTrail.startSecond = 0.9;
    singleTrail.endSecond = 1.2;
    singleTrail.radius = 14.0;
    singleTrail.points = {QPointF(250.0, 250.0), QPointF(270.0, 270.0)};
    singleState.muriAnalysisReport.actionTrails = {singleTrail};
    if (!verifyDxMuriActionSweepLineMatchesNaiveReferenceForState(singleState, QStringLiteral("dx muri action single trail"), err)) {
        return false;
    }

    PreviewFrameState overlapState;
    overlapState.playheadSeconds = 1.3;
    overlapState.muriRenderOptions.renderMode = RenderMode::MaimuriDxStyle;
    overlapState.muriRenderOptions.showTouchTrail = true;
    MuriActionTrail overlapA;
    overlapA.sourceType = QStringLiteral("slide");
    overlapA.startSecond = 0.95;
    overlapA.endSecond = 1.35;
    overlapA.radius = 20.0;
    overlapA.points = {QPointF(210.0, 210.0), QPointF(240.0, 240.0)};
    MuriActionTrail overlapB;
    overlapB.sourceType = QStringLiteral("wifi");
    overlapB.startSecond = 1.0;
    overlapB.endSecond = 1.35;
    overlapB.radius = 18.0;
    overlapB.points = {QPointF(320.0, 200.0), QPointF(300.0, 220.0)};
    MuriActionTrail overlapC;
    overlapC.sourceType = QStringLiteral("tap");
    overlapC.startSecond = 1.05;
    overlapC.endSecond = 1.3;
    overlapC.radius = 16.0;
    overlapC.points = {QPointF(270.0, 320.0)};
    overlapState.muriAnalysisReport.actionTrails = {overlapA, overlapB, overlapC};
    if (!verifyDxMuriActionSweepLineMatchesNaiveReferenceForState(overlapState, QStringLiteral("dx muri action overlap"), err)) {
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyActiveMarkerViewMatchesCollectMarkers(err)) {
        return 1;
    }
    if (!verifyTrackFallbackLookupAvoidsLinearScanRegression(err)) {
        return 1;
    }
    if (!verifyTrackExactLookupWinsOverBaseFallback(err)) {
        return 1;
    }
    if (!verifyWifiTrackFallbackLookupUsesBaseKey(err)) {
        return 1;
    }
    if (!verifyDxMuriActionSweepLineMatchesNaiveReference(err)) {
        return 1;
    }
    if (!verifyDxMuriActionSweepLineHandlesEdgeCases(err)) {
        return 1;
    }

    out << "preview_realtime_object_hot_path_spec ok" << Qt::endl;
    return 0;
}
