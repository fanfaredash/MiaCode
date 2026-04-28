#include "core/scene/PreviewMuriPadLayerState.h"

#include "common/MuriConfig.h"
#include "core/scene/PreviewSceneMath.h"

#include <QHash>

namespace miacode::preview::scene {

PreviewMuriPadLayerState buildPreviewMuriPadLayerState(
    const PreviewFrameState& state,
    const QRectF& playfieldRect
)
{
    PreviewMuriPadLayerState layerState;
    if (!state.muriRenderOptions.showJudgeMarkers || state.muriAnalysisReport.padWindows.isEmpty()) {
        return layerState;
    }

    QHash<QString, int> activePadCounts;
    activePadCounts.reserve(state.muriAnalysisReport.padWindows.size());
    for (const MuriPadWindow& window : state.muriAnalysisReport.padWindows) {
        if (window.startSecond <= state.playheadSeconds + 1e-6
            && window.endSecond + 1e-6 >= state.playheadSeconds) {
            activePadCounts[window.pad] += 1;
        }
    }
    if (activePadCounts.isEmpty()) {
        return layerState;
    }

    layerState.circles.reserve(activePadCounts.size());
    const bool maimuriDxStyle = state.muriRenderOptions.renderMode == RenderMode::MaimuriDxStyle;
    for (auto it = activePadCounts.cbegin(); it != activePadCounts.cend(); ++it) {
        const QPointF logicalCenter = miacode::muri::padCenter(it.key());
        const qreal logicalRadius = static_cast<qreal>(miacode::muri::padRadius(it.key()));
        if (logicalRadius <= 0.0) {
            continue;
        }

        PreviewCircleDescriptor circle;
        circle.center = mapLogicalPointToRect(logicalCenter, playfieldRect);
        circle.radiusX = qMax<qreal>(2.0, mapLogicalLengthToRect(logicalRadius, playfieldRect));
        circle.radiusY = circle.radiusX;
        if (maimuriDxStyle) {
            circle.fillColor = QColor(255, 255, 0);
        } else {
            circle.fillColor = QColor(255, 225, 84, qBound(90, 110 + it.value() * 26, 200));
            circle.strokeColor = QColor(255, 248, 187, qBound(130, 170 + it.value() * 18, 255));
            circle.strokeWidth = qMax<qreal>(2.0, circle.radiusX * 0.08);
        }
        layerState.circles.append(circle);
    }

    return layerState;
}

}  // namespace miacode::preview::scene
