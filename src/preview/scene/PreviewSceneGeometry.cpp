#include "preview/scene/PreviewSceneGeometry.h"

#include "common/PreviewVideoGeometryConfig.h"

namespace {

constexpr int kMargin = 0;

}  // namespace

namespace miacode::preview::scene {

QRectF stageRectForSize(const QSize& renderSize)
{
    const int renderWidth = qMax(1, renderSize.width());
    const int renderHeight = qMax(1, renderSize.height());
    return QRectF(
        kMargin,
        kMargin,
        qMax<qreal>(1.0, renderWidth - kMargin * 2),
        qMax<qreal>(1.0, renderHeight - kMargin * 2)
    );
}

QRectF playfieldRectForStage(const QRectF& stageRect, double layoutSquareScale)
{
    return miacode::preview_video::centeredLayoutRectForStage(stageRect, layoutSquareScale);
}

QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    bool fitContain
)
{
    if (mediaSize.isEmpty()) {
        return QRectF();
    }
    QSize fittedSize = mediaSize;
    fittedSize.scale(
        QSize(qMax(1, qRound(stageRect.width())), qMax(1, qRound(stageRect.height()))),
        fitContain ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding
    );
    if (fittedSize.isEmpty()) {
        return QRectF();
    }
    return QRectF(
        stageRect.center().x() - fittedSize.width() / 2.0,
        stageRect.center().y() - fittedSize.height() / 2.0,
        fittedSize.width(),
        fittedSize.height()
    );
}

}  // namespace miacode::preview::scene
