#include "preview/scene/PreviewAnimatedSpriteHelpers.h"

#include <QHash>
#include <QMutex>
#include <QPainter>
#include <QtMath>

namespace {

struct AnimatedSpriteAdjustParams {
    qreal brightness = 1.0;
    qreal saturation = 1.0;
    qreal contrast = 1.0;
};

constexpr qreal kMaterialAnimationTimeScale = 1.0 / 20.0;
constexpr qreal kMaterialAnimationPhaseScale = (kMaterialAnimationTimeScale / 0.0008) * 0.2;
constexpr qreal kOverlayCacheScalarQuantization = 4096.0;
constexpr qsizetype kOverlayCacheMaxEntries = 128;

struct OverlayCacheKey {
    quint64 baseImageKey = 0;
    quint64 overlayImageKey = 0;
    qint64 mixKey = 0;
    qint64 lightenKey = 0;
    QRgb tintRgba = 0;

    bool operator==(const OverlayCacheKey& other) const
    {
        return baseImageKey == other.baseImageKey
            && overlayImageKey == other.overlayImageKey
            && mixKey == other.mixKey
            && lightenKey == other.lightenKey
            && tintRgba == other.tintRgba;
    }
};

size_t hashCombine(size_t seed, quint64 value) noexcept
{
    return seed ^ (static_cast<size_t>(value) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

size_t qHash(const OverlayCacheKey& key, size_t seed = 0) noexcept
{
    seed = hashCombine(seed, key.baseImageKey);
    seed = hashCombine(seed, key.overlayImageKey);
    seed = hashCombine(seed, static_cast<quint64>(key.mixKey));
    seed = hashCombine(seed, static_cast<quint64>(key.lightenKey));
    seed = hashCombine(seed, key.tintRgba);
    return seed;
}

QMutex& animatedSpriteCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<OverlayCacheKey, QImage>& overlayImageCache()
{
    static QHash<OverlayCacheKey, QImage> cache;
    return cache;
}

qint64 quantizeOverlayCacheScalar(qreal value)
{
    return qRound64(value * kOverlayCacheScalarQuantization);
}

AnimatedSpriteAdjustParams animatedSpriteAdjustParams(
    miacode::preview::scene::PreviewAnimatedSpriteEffect effect,
    double playheadSeconds
)
{
    AnimatedSpriteAdjustParams params;
    if (effect == miacode::preview::scene::PreviewAnimatedSpriteEffect::None) {
        return params;
    }

    const qreal wave = miacode::preview::scene::previewAnimatedSpriteWave(playheadSeconds);
    if (effect == miacode::preview::scene::PreviewAnimatedSpriteEffect::HoldShine) {
        params.brightness = 0.95 + qAbs(wave) * 0.5;
        return params;
    }

    params.brightness = 0.95 + qMax<qreal>(wave * 0.65, 0.0);
    params.contrast = 1.0 + qMin<qreal>(wave * -0.55, 0.0);
    return params;
}

QImage applyAnimatedSpriteAdjustments(const QImage& source, const AnimatedSpriteAdjustParams& params)
{
    if (source.isNull()) {
        return QImage();
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int alpha = qAlpha(row[x]);
            if (alpha == 0) {
                continue;
            }
            const qreal sourceRed = static_cast<qreal>(qRed(row[x])) / 255.0;
            const qreal sourceGreen = static_cast<qreal>(qGreen(row[x])) / 255.0;
            const qreal sourceBlue = static_cast<qreal>(qBlue(row[x])) / 255.0;

            qreal finalRed = sourceRed * params.brightness;
            qreal finalGreen = sourceGreen * params.brightness;
            qreal finalBlue = sourceBlue * params.brightness;

            const qreal gray = 0.2125 * sourceRed + 0.7154 * sourceGreen + 0.0721 * sourceBlue;
            finalRed = gray + (finalRed - gray) * params.saturation;
            finalGreen = gray + (finalGreen - gray) * params.saturation;
            finalBlue = gray + (finalBlue - gray) * params.saturation;

            finalRed = 0.5 + (finalRed - 0.5) * params.contrast;
            finalGreen = 0.5 + (finalGreen - 0.5) * params.contrast;
            finalBlue = 0.5 + (finalBlue - 0.5) * params.contrast;

            row[x] = qRgba(
                qBound(0, qRound(finalRed * 255.0), 255),
                qBound(0, qRound(finalGreen * 255.0), 255),
                qBound(0, qRound(finalBlue * 255.0), 255),
                alpha
            );
        }
    }
    return result;
}

}  // namespace

namespace miacode::preview::scene {

qreal previewAnimatedSpriteWave(double playheadSeconds)
{
    return qSin(static_cast<qreal>(playheadSeconds) * kMaterialAnimationPhaseScale);
}

QImage composeOverlayImage(
    const QImage& base,
    const QImage& overlay,
    qreal mix,
    qreal lighten,
    const QColor* accentOverride
)
{
    if (base.isNull() || overlay.isNull()) {
        return base;
    }

    const QColor tint = accentOverride != nullptr ? *accentOverride : QColor(255, 255, 255);
    const OverlayCacheKey cacheKey{
        static_cast<quint64>(base.cacheKey()),
        static_cast<quint64>(overlay.cacheKey()),
        quantizeOverlayCacheScalar(mix),
        quantizeOverlayCacheScalar(lighten),
        tint.rgba()
    };
    {
        QMutexLocker locker(&animatedSpriteCacheMutex());
        if (const auto it = overlayImageCache().constFind(cacheKey); it != overlayImageCache().constEnd()) {
            const QImage& cached = it.value();
            return cached;
        }
    }

    QImage overlayTinted = overlay.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < overlayTinted.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(overlayTinted.scanLine(y));
        for (int x = 0; x < overlayTinted.width(); ++x) {
            QColor c = QColor::fromRgba(line[x]);
            if (c.alpha() == 0) {
                continue;
            }
            const int alpha = c.alpha();
            const int nr = qBound(0, qRound(c.red() * (1.0 - mix) + tint.red() * mix), 255);
            const int ng = qBound(0, qRound(c.green() * (1.0 - mix) + tint.green() * mix), 255);
            const int nb = qBound(0, qRound(c.blue() * (1.0 - mix) + tint.blue() * mix), 255);
            const int outR = qBound(0, qRound(nr + (255 - nr) * lighten), 255);
            const int outG = qBound(0, qRound(ng + (255 - ng) * lighten), 255);
            const int outB = qBound(0, qRound(nb + (255 - nb) * lighten), 255);
            line[x] = qRgba(outR, outG, outB, alpha);
        }
    }

    const int width = qMax(base.width(), overlayTinted.width());
    const int height = qMax(base.height(), overlayTinted.height());
    QImage composed(width, height, QImage::Format_ARGB32);
    composed.fill(Qt::transparent);
    QPainter p(&composed);
    const QPoint baseTopLeft((width - base.width()) / 2, (height - base.height()) / 2);
    const QPoint overlayTopLeft((width - overlayTinted.width()) / 2, (height - overlayTinted.height()) / 2);
    p.drawImage(baseTopLeft, base);
    p.drawImage(overlayTopLeft, overlayTinted);
    p.end();

    {
        QMutexLocker locker(&animatedSpriteCacheMutex());
        if (const auto it = overlayImageCache().constFind(cacheKey); it != overlayImageCache().constEnd()) {
            const QImage& cached = it.value();
            return cached;
        }
        if (overlayImageCache().size() >= kOverlayCacheMaxEntries) {
            overlayImageCache().clear();
        }
        overlayImageCache().insert(cacheKey, composed);
    }
    return composed;
}

QImage buildAnimatedSpriteImage(
    const QImage& image,
    PreviewAnimatedSpriteEffect effect,
    double playheadSeconds
)
{
    if (effect == PreviewAnimatedSpriteEffect::None || image.isNull()) {
        return image;
    }
    return applyAnimatedSpriteAdjustments(image, animatedSpriteAdjustParams(effect, playheadSeconds));
}

}  // namespace miacode::preview::scene
