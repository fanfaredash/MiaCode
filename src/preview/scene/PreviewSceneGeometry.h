#pragma once

#include <QRectF>
#include <QSize>

namespace miacode::preview::scene {

QRectF stageRectForSize(const QSize& renderSize);
QRectF playfieldRectForStage(const QRectF& stageRect, double layoutSquareScale);
QRectF outlineRectForPlayfield(const QRectF& playfieldRect);
QRectF mediaTargetRect(
    const QSize& mediaSize,
    const QRectF& stageRect,
    bool fitContain
);

}  // namespace miacode::preview::scene
