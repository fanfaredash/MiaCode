#include "preview/quick_scene/PreviewQuickTouchHoverLayer.h"

#include "preview/quick_scene/PreviewQuickCircleNodes.h"
#include "core/scene/PreviewCircleDescriptor.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "core/scene/PreviewSceneMath.h"
#include "core/scene/TouchPadAuthoringState.h"

QSGNode* PreviewQuickTouchHoverLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    Q_UNUSED(window);
    Q_UNUSED(textures);

    const QString hoveredPad = state.hoveredTouchPad.trimmed().toUpper();
    if (hoveredPad.isEmpty()) {
        return nullptr;
    }

    const QRectF playfieldRect = miacode::preview::scene::playfieldRectForStage(
        miacode::preview::scene::stageRectForSize(renderSize),
        state.render.layoutSquareScale
    );
    const QPointF logicalPoint = miacode::preview::scene::touchPadLogicalPoint(hoveredPad);
    if (qFuzzyIsNull(logicalPoint.x()) && qFuzzyIsNull(logicalPoint.y())) {
        return nullptr;
    }

    const qreal logicalRadius = hoveredPad == QLatin1String("C")
        ? miacode::preview::scene::kTouchPadCenterHitRadiusLogical
        : miacode::preview::scene::kTouchPadHitRadiusLogical;
    const qreal radius = miacode::preview::scene::mapLogicalLengthToRect(logicalRadius, playfieldRect);

    miacode::preview::scene::PreviewCircleDescriptor circle;
    circle.center = miacode::preview::scene::mapLogicalPointToRect(logicalPoint, playfieldRect);
    circle.radiusX = radius;
    circle.radiusY = radius;
    const bool pressed = state.pressedTouchPad.trimmed().toUpper() == hoveredPad;
    const auto style = miacode::preview::scene::touchPadAuthoringVisualStyle(pressed);
    circle.fillColor = style.fill;
    circle.strokeColor = style.stroke;
    circle.strokeWidth = qMax<qreal>(1.0, miacode::preview::scene::mapLogicalLengthToRect(1.4, playfieldRect));

    return buildPreviewCircleNodeTree(
        oldNode,
        miacode::preview::scene::PreviewCircleDescriptors{circle}
    );
}
