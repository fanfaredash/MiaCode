#pragma once

#include "core/video/PreviewRenderSettings.h"

#include <QRectF>
#include <QSize>

namespace miacode::preview::scene {

struct PreviewMediaPlacement {
    QRectF targetRect;
    QRectF sourceRect;
};

QRectF stageRectForSize(const QSize& renderSize);
QRectF playfieldRectForStage(const QRectF& stageRect, double layoutSquareScale);
QRectF centeredMaxSquareRectForStage(const QRectF& stageRect);
PreviewMediaPlacement mediaPlacement(
    const QSize& mediaSize,
    const QRectF& stageRect,
    PreviewBackgroundScaleMode scaleMode
);
QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    bool fitContain
);
QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    PreviewBackgroundScaleMode scaleMode
);
QRectF mediaSourceRect(
    const QSize& mediaSize,
    PreviewBackgroundScaleMode scaleMode
);

}  // namespace miacode::preview::scene
