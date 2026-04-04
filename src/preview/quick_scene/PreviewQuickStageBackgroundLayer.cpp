#include "preview/quick_scene/PreviewQuickStageBackgroundLayer.h"

#include "common/PreviewVideoGeometryConfig.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QColor>
#include <QImage>
#include <QQuickWindow>
#include <QSGNode>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>

#include <cmath>

namespace {

QImage stageMediaImage(const miacode::preview::scene::PreviewFrameState& state)
{
#ifdef HAVE_QT_MULTIMEDIA
    if (state.media.videoFrame.isValid()) {
        const QImage image = state.media.videoFrame.toImage();
        if (!image.isNull()) {
            return image;
        }
    }
#endif
    if (!state.media.mediaFrame.isNull()) {
        return state.media.mediaFrame;
    }
    return state.media.retainedVideoFallbackFrame;
}

bool stageMediaUsesCachedTexture(const miacode::preview::scene::PreviewFrameState& state)
{
#ifdef HAVE_QT_MULTIMEDIA
    if (state.media.videoFrame.isValid()) {
        return false;
    }
#endif
    return !state.media.mediaFrame.isNull();
}

QImage buildBrightnessMask(
    const QSize& renderSize,
    const QRectF& stageRect,
    double outerDarkAlpha,
    double innerDarkAlpha,
    double layoutSquareScale,
    double ringRatio,
    bool smoothBrightness
)
{
    QImage mask(renderSize, QImage::Format_RGBA8888);
    mask.fill(Qt::transparent);
    const double layoutSide = miacode::preview_video::layoutSquareSideForCanvasHeight(
        static_cast<double>(renderSize.height()),
        layoutSquareScale
    );
    const double centerX = stageRect.center().x();
    const double centerY = stageRect.center().y();
    for (int y = 0; y < renderSize.height(); ++y) {
        uchar* row = mask.scanLine(y);
        const double dy = static_cast<double>(y) - centerY;
        for (int x = 0; x < renderSize.width(); ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double radius = std::sqrt(dx * dx + dy * dy);
            const int alpha = qBound(
                0,
                qRound(
                    miacode::preview_video::dimAlphaForRadius(
                        radius,
                        outerDarkAlpha,
                        innerDarkAlpha,
                        layoutSide,
                        ringRatio,
                        smoothBrightness
                    ) * 255.0
                ),
                255
            );
            const int offset = x * 4;
            row[offset + 0] = 0;
            row[offset + 1] = 0;
            row[offset + 2] = 0;
            row[offset + 3] = static_cast<uchar>(alpha);
        }
    }
    return mask;
}

}  // namespace

QSGNode* PreviewQuickStageBackgroundLayer::updateNode(
    QSGNode* oldNode,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& renderSize,
    QQuickWindow* window,
    PreviewTextureRepository* textures
) const
{
    delete oldNode;
    auto* root = new QSGNode();
    const QRectF stageRect = miacode::preview::scene::stageRectForSize(renderSize);
    const QImage mediaImage = stageMediaImage(state);
    const bool hasMedia = !mediaImage.isNull();
    const double outerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessOuter, 1.0);
    const double innerDarkAlpha = qBound(0.0, 1.0 - state.render.backgroundBrightnessInner, 1.0);

    auto* baseNode = new QSGSimpleRectNode();
    baseNode->setRect(stageRect);
    baseNode->setColor(hasMedia ? QColor(QStringLiteral("#000000")) : QColor(QStringLiteral("#1F2833")));
    root->appendChildNode(baseNode);

    if (hasMedia && window != nullptr && textures != nullptr) {
        auto* mediaNode = new QSGSimpleTextureNode();
        const bool cacheable = stageMediaUsesCachedTexture(state);
        mediaNode->setOwnsTexture(!cacheable);
        const QSGTexture* texture = cacheable
            ? textures->textureForImage(mediaImage, true)
            : textures->createOwnedTexture(mediaImage);
        mediaNode->setTexture(const_cast<QSGTexture*>(texture));
        mediaNode->setRect(
            miacode::preview::scene::mediaTargetRect(
                mediaImage.size(),
                stageRect,
                state.render.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain
            )
        );
        mediaNode->setFiltering(QSGTexture::Linear);
        root->appendChildNode(mediaNode);
    }

    if (outerDarkAlpha > 1e-6 || innerDarkAlpha > 1e-6) {
        const double normalizedLayoutScale = miacode::preview_video::normalizedLayoutSquareScale(state.render.layoutSquareScale);
        const double ringRatio = miacode::preview_video::effectiveLayoutRingDiameterRatio(state.assets.layoutRingDiameterRatio);
        if (brightnessMaskCache_.isNull()
            || brightnessMaskCacheSize_ != renderSize
            || qAbs(brightnessMaskCacheOuter_ - outerDarkAlpha) > 1e-6
            || qAbs(brightnessMaskCacheInner_ - innerDarkAlpha) > 1e-6
            || qAbs(brightnessMaskCacheLayoutScale_ - normalizedLayoutScale) > 1e-6
            || qAbs(brightnessMaskCacheRingRatio_ - ringRatio) > 1e-6
            || brightnessMaskCacheSmooth_ != state.render.smoothBrightness) {
            brightnessMaskCache_ = buildBrightnessMask(
                renderSize,
                stageRect,
                outerDarkAlpha,
                innerDarkAlpha,
                normalizedLayoutScale,
                ringRatio,
                state.render.smoothBrightness
            );
            brightnessMaskCacheSize_ = renderSize;
            brightnessMaskCacheOuter_ = outerDarkAlpha;
            brightnessMaskCacheInner_ = innerDarkAlpha;
            brightnessMaskCacheLayoutScale_ = normalizedLayoutScale;
            brightnessMaskCacheRingRatio_ = ringRatio;
            brightnessMaskCacheSmooth_ = state.render.smoothBrightness;
        }

        auto* maskNode = new QSGSimpleTextureNode();
        maskNode->setOwnsTexture(true);
        maskNode->setTexture(textures->createOwnedTexture(brightnessMaskCache_));
        maskNode->setRect(QRectF(QPointF(0.0, 0.0), QSizeF(renderSize)));
        maskNode->setFiltering(QSGTexture::Linear);
        root->appendChildNode(maskNode);
    }
    return root;
}
