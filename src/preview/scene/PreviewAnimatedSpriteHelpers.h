#pragma once

#include <QImage>
#include <QVector>

namespace miacode::preview::scene {

enum class PreviewAnimatedSpriteEffect {
    None = 0,
    HoldShine = 1,
    BreakAnimate = 2,
};

QImage composeOverlayImage(
    const QImage& base,
    const QImage& overlay,
    qreal mix,
    qreal lighten,
    const QColor* accentOverride = nullptr
);

QImage buildAnimatedSpriteImage(
    const QImage& image,
    PreviewAnimatedSpriteEffect effect,
    double playheadSeconds
);

}  // namespace miacode::preview::scene
