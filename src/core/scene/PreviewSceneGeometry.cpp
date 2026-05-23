#include "core/scene/PreviewSceneGeometry.h"

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

QRectF centeredMaxSquareRectForStage(const QRectF& stageRect)
{
    const qreal side = qMax<qreal>(1.0, qMin(stageRect.width(), stageRect.height()));
    return QRectF(
        stageRect.center().x() - side / 2.0,
        stageRect.center().y() - side / 2.0,
        side,
        side
    );
}

QRectF mediaSourceRect(
    const QSize& mediaSize,
    PreviewBackgroundScaleMode scaleMode
)
{
    if (mediaSize.isEmpty()) {
        return QRectF();
    }
    return QRectF(0.0, 0.0, mediaSize.width(), mediaSize.height());
}

PreviewMediaPlacement mediaPlacement(
    const QSize& mediaSize,
    const QRectF& stageRect,
    PreviewBackgroundScaleMode scaleMode
)
{
    PreviewMediaPlacement placement;
    if (mediaSize.isEmpty()) {
        return placement;
    }

    if (scaleMode == PreviewBackgroundScaleMode::SquareFitContain) {
        const QRectF squareRect = centeredMaxSquareRectForStage(stageRect);
        QSize fittedSize = mediaSize;
        fittedSize.scale(
            QSize(qMax(1, qRound(squareRect.width())), qMax(1, qRound(squareRect.height()))),
            Qt::KeepAspectRatio
        );
        if (fittedSize.isEmpty()) {
            return PreviewMediaPlacement();
        }
        placement.targetRect = QRectF(
            squareRect.center().x() - fittedSize.width() / 2.0,
            squareRect.center().y() - fittedSize.height() / 2.0,
            fittedSize.width(),
            fittedSize.height()
        );
        placement.sourceRect = mediaSourceRect(mediaSize, scaleMode);
        return placement;
    }

    QSize fittedSize = mediaSize;
    fittedSize.scale(
        QSize(qMax(1, qRound(stageRect.width())), qMax(1, qRound(stageRect.height()))),
        scaleMode == PreviewBackgroundScaleMode::FitContain
            ? Qt::KeepAspectRatio
            : Qt::KeepAspectRatioByExpanding
    );
    if (fittedSize.isEmpty()) {
        return PreviewMediaPlacement();
    }

    placement.targetRect = QRectF(
        stageRect.center().x() - fittedSize.width() / 2.0,
        stageRect.center().y() - fittedSize.height() / 2.0,
        fittedSize.width(),
        fittedSize.height()
    );
    placement.sourceRect = mediaSourceRect(mediaSize, scaleMode);
    return placement;
}

QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    bool fitContain
)
{
    return mediaTargetRect(
        mediaSize,
        stageRect,
        fitContain ? PreviewBackgroundScaleMode::FitContain : PreviewBackgroundScaleMode::FillCrop
    );
}

QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    PreviewBackgroundScaleMode scaleMode
)
{
    return mediaPlacement(mediaSize, stageRect, scaleMode).targetRect;
}

}  // namespace miacode::preview::scene
