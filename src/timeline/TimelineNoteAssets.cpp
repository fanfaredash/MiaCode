#include "timeline/TimelineNoteAssets.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QTransform>

#include "common/AssetPaths.h"
#include "common/PreviewSkinConfig.h"

namespace {

constexpr int kNoteSize = 14;
constexpr int kSlideTrackBasePixelSize =
    static_cast<int>((static_cast<double>(kNoteSize) * miacode::preview_skin::kSlideTrackLongSideRelativeToTap) + 0.5);

const QPixmap& iconForType(const miacode::timeline::TimelineNoteAssetSet& assets, const QString& type)
{
    auto directIt = assets.noteIcons.constFind(type);
    if (directIt != assets.noteIcons.constEnd()) {
        return directIt.value();
    }
    const QString lower = type.toLower();
    if (lower != type) {
        auto lowerIt = assets.noteIcons.constFind(lower);
        if (lowerIt != assets.noteIcons.constEnd()) {
            return lowerIt.value();
        }
    }
    auto fallbackIt = assets.noteIcons.constFind(QStringLiteral("tap"));
    if (fallbackIt != assets.noteIcons.constEnd()) {
        return fallbackIt.value();
    }

    static const QPixmap kEmptyPixmap;
    return kEmptyPixmap;
}

}  // namespace

namespace miacode::timeline {

TimelineNoteAssetSet loadTimelineNoteAssets()
{
    TimelineNoteAssetSet assets;
    const QString notesDir = miacode::assets::assetPath("skin/skinSTD");
    if (!QFileInfo::exists(QDir(notesDir).filePath("tap.png"))) {
        return assets;
    }

    const auto loadRawIcon = [notesDir](const QStringList& fileNames) -> QPixmap {
        for (const QString& fileName : fileNames) {
            const QString path = QDir(notesDir).filePath(fileName);
            QPixmap pix(path);
            if (!pix.isNull()) {
                return pix;
            }
        }
        return QPixmap();
    };

    const auto putIcon = [&assets](const QString& key, const QPixmap& pix, int basePixelSize) {
        if (pix.isNull()) {
            return;
        }
        assets.noteIcons.insert(key, pix);
        assets.noteIconBasePixelSizes.insert(key, qMax(1, basePixelSize));
    };

    const auto loadIcon = [&loadRawIcon, &putIcon](const QString& key, const QStringList& fileNames, int basePixelSize) {
        putIcon(key, loadRawIcon(fileNames), basePixelSize);
    };

    const auto buildCenteredCompositeIcon = [](std::initializer_list<const QPixmap*> layers) -> QPixmap {
        int canvasWidth = 0;
        int canvasHeight = 0;
        for (const QPixmap* layer : layers) {
            if (layer == nullptr || layer->isNull()) {
                continue;
            }
            canvasWidth = qMax(canvasWidth, layer->width());
            canvasHeight = qMax(canvasHeight, layer->height());
        }
        if (canvasWidth <= 0 || canvasHeight <= 0) {
            return QPixmap();
        }

        QPixmap canvas(canvasWidth, canvasHeight);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (const QPixmap* layer : layers) {
            if (layer == nullptr || layer->isNull()) {
                continue;
            }
            painter.drawPixmap((canvasWidth - layer->width()) / 2, (canvasHeight - layer->height()) / 2, *layer);
        }
        painter.end();
        return canvas;
    };

    // 4-triangle touch icon — mirrors the in-game touch object's corner layout
    // (PreviewTouchLayerState::buildPreviewTouchLayerState): four copies of the corner
    // sprite (touch.png / its each/break variants) placed up/right/down/left and rotated
    // 180/-90/0/90, each offset from the centre by kTouchClosedOffset(12)/kTouchAssetScale(0.5)
    // = 24 native px — i.e. the "closed"/at-hit pose the four pieces snap into.
    const auto buildTouchCornerCompositeIcon = [](const QPixmap& corner) -> QPixmap {
        if (corner.isNull()) {
            return QPixmap();
        }
        constexpr qreal kCornerOffset = 24.0;  // kTouchClosedOffset / kTouchAssetScale
        // Nudge the four pieces outward a touch so the closed pose leaves a small
        // centre gap instead of the corners meeting flush.
        constexpr qreal kCornerGap = 6.0;
        constexpr qreal kEffectiveOffset = kCornerOffset + kCornerGap;
        const struct {
            qreal dx;
            qreal dy;
            int angle;
        } layout[] = {
            {0.0, -kEffectiveOffset, 180},  // up
            {kEffectiveOffset, 0.0, -90},   // right
            {0.0, kEffectiveOffset, 0},     // down
            {-kEffectiveOffset, 0.0, 90},   // left
        };
        // Square canvas tightly bounding the rotated, offset corners (symmetric by layout).
        const qreal halfExtent =
            qMax(corner.width() / 2.0, kEffectiveOffset + corner.height() / 2.0);
        const int side = qMax(1, qRound(2.0 * halfExtent));

        QPixmap canvas(side, side);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QPointF center(side / 2.0, side / 2.0);
        for (const auto& piece : layout) {
            painter.save();
            painter.translate(center.x() + piece.dx, center.y() + piece.dy);
            painter.rotate(piece.angle);
            painter.drawPixmap(QPointF(-corner.width() / 2.0, -corner.height() / 2.0), corner);
            painter.restore();
        }
        painter.end();
        return canvas;
    };

    const auto buildTouchHoldCompositeIcon =
        [&buildCenteredCompositeIcon](const QPixmap& borderBase, const QPixmap& holdBodyBase, const QPixmap& pointBase) {
            if (borderBase.isNull() || holdBodyBase.isNull()) {
                return QPixmap();
            }
            if (pointBase.isNull()) {
                return buildCenteredCompositeIcon({&borderBase, &holdBodyBase});
            }
            return buildCenteredCompositeIcon({&borderBase, &holdBodyBase, &pointBase});
        };

    const auto buildOverlayCompositeIcon =
        [&buildCenteredCompositeIcon](const QPixmap& base, const QPixmap& overlay) {
            if (base.isNull()) {
                return QPixmap();
            }
            return overlay.isNull() ? buildCenteredCompositeIcon({&base}) : buildCenteredCompositeIcon({&base, &overlay});
        };

    const auto putOverlayCompositeIcon =
        [&buildOverlayCompositeIcon, &loadRawIcon, &loadIcon, &putIcon](
            const QString& key,
            const QStringList& baseFileNames,
            const QStringList& overlayFileNames,
            int basePixelSize) {
            const QPixmap composite = buildOverlayCompositeIcon(loadRawIcon(baseFileNames), loadRawIcon(overlayFileNames));
            if (!composite.isNull()) {
                putIcon(key, composite, basePixelSize);
            } else {
                loadIcon(key, baseFileNames, basePixelSize);
            }
        };

    loadIcon("tap", {"tap.png"}, kNoteSize);
    loadIcon("tap_break", {"tap_break.png", "tap.png"}, kNoteSize);
    loadIcon("tap_each", {"tap_each.png", "each.png", "tap.png"}, kNoteSize);
    loadIcon("hold", {"hold.png"}, kNoteSize);
    loadIcon("hold_break", {"hold_break.png", "hold.png"}, kNoteSize);
    loadIcon("hold_each", {"hold_each.png", "hold.png"}, kNoteSize);
    loadIcon("slide", {"star.png"}, kNoteSize + 3);
    loadIcon("wifi", {"star.png"}, kNoteSize + 3);
    loadIcon("star_break", {"star_break.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_break_double", {"star_break_double.png", "star_break.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_each", {"star_each.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_double", {"star_double.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_each_double", {"star_each_double.png", "star_double.png", "star_each.png", "star.png"}, kNoteSize + 3);
    loadIcon("slide_track", {"slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("slide_track_each", {"slide_each.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("slide_track_break", {"slide_break.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track", {"wifi_0.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track_each", {"wifi_each_0.png", "wifi_0.png", "slide_each.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track_break", {"wifi_break_0.png", "wifi_0.png", "slide_break.png", "slide.png"}, kSlideTrackBasePixelSize);
    // Mine-note timeline icons (simai `m`). Fall back to the normal note icon
    // when a skin ships no mine art.
    loadIcon("tap_mine", {"tap_mine.png", "tap.png"}, kNoteSize);
    loadIcon("hold_mine", {"hold_mine.png", "hold.png"}, kNoteSize);
    loadIcon("star_mine", {"star_mine.png", "star.png"}, kNoteSize + 3);
    loadIcon("slide_track_mine", {"slide_mine.png", "slide.png"}, kSlideTrackBasePixelSize);

    putOverlayCompositeIcon("tap_ex", {"tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("tap_break_ex", {"tap_break.png", "tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("tap_each_ex", {"tap_each.png", "each.png", "tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_ex", {"hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_break_ex", {"hold_break.png", "hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_each_ex", {"hold_each.png", "hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("star_ex", {"star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon("star_break_ex", {"star_break.png", "star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon("star_each_ex", {"star_each.png", "star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon("star_ex_double", {"star_double.png", "star.png"}, {"star_ex_double.png", "star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon(
        "star_break_ex_double",
        {"star_break_double.png", "star_break.png", "star.png"},
        {"star_ex_double.png", "star_ex.png"},
        kNoteSize + 3);
    putOverlayCompositeIcon(
        "star_each_ex_double",
        {"star_each_double.png", "star_double.png", "star_each.png", "star.png"},
        {"star_ex_double.png", "star_ex.png"},
        kNoteSize + 3);

    // The touch icons are built by tiling the corner sprite (touch.png + each/break
    // variants) into the four-triangle in-game touch shape — see buildTouchCornerCompositeIcon.
    const QPixmap touchCorner = loadRawIcon({"touch.png", "touch_each.png", "each.png", "tap.png"});
    const QPixmap touchBreakCorner =
        loadRawIcon({"touch_break.png", "touch.png", "touch_each.png", "each.png", "tap.png"});
    const QPixmap touchEachCorner =
        loadRawIcon({"touch_each.png", "touch.png", "each.png", "tap.png"});

    const QPixmap touchComposite = buildTouchCornerCompositeIcon(touchCorner);
    const QPixmap touchBreakComposite = buildTouchCornerCompositeIcon(touchBreakCorner);
    const QPixmap touchEachComposite = buildTouchCornerCompositeIcon(touchEachCorner);
    if (!touchComposite.isNull()) {
        putIcon("touch", touchComposite, kNoteSize + 3);
    } else {
        loadIcon("touch", {"touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize + 3);
    }
    if (!touchEachComposite.isNull()) {
        putIcon("touch_each", touchEachComposite, kNoteSize + 3);
    } else {
        loadIcon("touch_each", {"touch_each.png", "touch.png", "each.png", "tap.png"}, kNoteSize + 3);
    }
    if (!touchBreakComposite.isNull()) {
        putIcon("touch_break", touchBreakComposite, kNoteSize + 3);
    } else {
        loadIcon("touch_break", {"touch_break.png", "touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize + 3);
    }

    const QPixmap touchHoldComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point.png", "tap.png"}));
    if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold", touchHoldComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldBorderOnly =
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"});
    if (!touchHoldBorderOnly.isNull()) {
        putIcon("touch_hold_border_only", touchHoldBorderOnly, kNoteSize + 3);
        putIcon("touch_hold_border_only_each", touchHoldBorderOnly, kNoteSize + 3);
    }
    const QPixmap touchHoldEachComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point_each.png", "touch_point.png", "tap.png"}));
    if (!touchHoldEachComposite.isNull()) {
        putIcon("touch_hold_each", touchHoldEachComposite, kNoteSize + 3);
    } else if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold_each", touchHoldComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldBreakComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_break.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_break_point.png", "touch_point.png", "tap.png"}));
    if (!touchHoldBreakComposite.isNull()) {
        putIcon("touch_hold_break", touchHoldBreakComposite, kNoteSize + 3);
    } else if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold_break", touchHoldComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldBreakBorderOnly =
        loadRawIcon({"touchhold_border.png", "touch_break.png", "touch_border_2.png", "touch.png", "tap.png"});
    if (!touchHoldBreakBorderOnly.isNull()) {
        putIcon("touch_hold_border_only_break", touchHoldBreakBorderOnly, kNoteSize + 3);
    } else if (!touchHoldBorderOnly.isNull()) {
        putIcon("touch_hold_border_only_break", touchHoldBorderOnly, kNoteSize + 3);
    }

    // Mine-note touch / touch-hold icons (simai `m`); fall back to the normal
    // composite when the skin ships no mine art.
    const QPixmap touchMineComposite =
        buildTouchCornerCompositeIcon(loadRawIcon({"touch_mine.png", "touch.png", "tap.png"}));
    if (!touchMineComposite.isNull()) {
        putIcon("touch_mine", touchMineComposite, kNoteSize + 3);
    } else if (!touchComposite.isNull()) {
        putIcon("touch_mine", touchComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldMineComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border_mine.png", "touchhold_border.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1_mine.png", "touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point_mine.png", "touch_point.png", "tap.png"}));
    if (!touchHoldMineComposite.isNull()) {
        putIcon("touch_hold_mine", touchHoldMineComposite, kNoteSize + 3);
    } else if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold_mine", touchHoldComposite, kNoteSize + 3);
    }

    return assets;
}

int transformedPixmapScalePermille(qreal scale)
{
    return qMax(1, qRound(scale * 1000.0));
}

int transformedPixmapRotationTenths(qreal rotationDegrees)
{
    int tenths = qRound(rotationDegrees * 10.0);
    tenths %= 3600;
    if (tenths < 0) {
        tenths += 3600;
    }
    return tenths;
}

QString transformedPixmapCacheKey(const QString& type, qreal scale, qreal rotationDegrees, bool mirrorX)
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(type)
        .arg(transformedPixmapScalePermille(scale))
        .arg(transformedPixmapRotationTenths(rotationDegrees))
        .arg(mirrorX ? 1 : 0);
}

QString transformedPixmapCacheKey(const QString& type, const QSize& targetSize, qreal rotationDegrees, bool mirrorX)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(type)
        .arg(qMax(1, targetSize.width()))
        .arg(qMax(1, targetSize.height()))
        .arg(transformedPixmapRotationTenths(rotationDegrees))
        .arg(mirrorX ? 1 : 0);
}

QString holdPixmapCacheKey(const QString& type, qreal scale)
{
    return QStringLiteral("%1|%2").arg(type).arg(transformedPixmapScalePermille(scale));
}

int noteIconBasePixelSizeForType(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    int fallbackPixelSize)
{
    auto directIt = assets.noteIconBasePixelSizes.constFind(type);
    if (directIt != assets.noteIconBasePixelSizes.constEnd()) {
        return directIt.value();
    }
    const QString lower = type.toLower();
    if (lower != type) {
        auto lowerIt = assets.noteIconBasePixelSizes.constFind(lower);
        if (lowerIt != assets.noteIconBasePixelSizes.constEnd()) {
            return lowerIt.value();
        }
    }
    auto fallbackIt = assets.noteIconBasePixelSizes.constFind(QStringLiteral("tap"));
    if (fallbackIt != assets.noteIconBasePixelSizes.constEnd()) {
        return fallbackIt.value();
    }
    return qMax(1, fallbackPixelSize);
}

QSize targetSizeForNoteType(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale,
    int fallbackPixelSize)
{
    const QPixmap& base = iconForType(assets, type);
    if (base.isNull()) {
        return QSize();
    }

    const qreal normalizedScale = scale > 0.0 ? scale : 1.0;
    const int targetBox = qMax(
        1,
        qRound(static_cast<qreal>(noteIconBasePixelSizeForType(assets, type, fallbackPixelSize)) * normalizedScale));
    QSize targetSize(base.width(), base.height());
    targetSize.scale(targetBox, targetBox, Qt::KeepAspectRatio);
    targetSize.setWidth(qMax(1, targetSize.width()));
    targetSize.setHeight(qMax(1, targetSize.height()));
    return targetSize;
}

QPixmap transformNotePixmapToTargetSize(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    const QSize& targetSize,
    qreal rotationDegrees,
    bool mirrorX)
{
    const QPixmap& base = iconForType(assets, type);
    if (base.isNull()) {
        return QPixmap();
    }

    QPixmap transformed = targetSize.isValid()
        ? base.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        : base;
    if (mirrorX) {
        transformed = transformed.transformed(QTransform().scale(-1.0, 1.0), Qt::SmoothTransformation);
    }
    if (transformedPixmapRotationTenths(rotationDegrees) != 0) {
        transformed = transformed.transformed(QTransform().rotate(rotationDegrees), Qt::SmoothTransformation);
    }
    return transformed;
}

QPixmap transformNotePixmap(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale,
    qreal rotationDegrees,
    bool mirrorX,
    int fallbackPixelSize)
{
    return transformNotePixmapToTargetSize(
        assets,
        type,
        targetSizeForNoteType(assets, type, scale, fallbackPixelSize),
        rotationDegrees,
        mirrorX);
}

qreal holdScaleForBaseIconScale(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal baseIconScale,
    int fallbackPixelSize)
{
    const qreal normalizedBaseScale = baseIconScale > 0.0 ? baseIconScale : 1.0;
    const QPixmap tapReference =
        transformNotePixmap(assets, QStringLiteral("tap"), normalizedBaseScale, 0.0, false, fallbackPixelSize);
    const QPixmap holdReference = transformNotePixmap(
        assets,
        type,
        normalizedBaseScale,
        90.0,
        false,
        fallbackPixelSize);
    if (tapReference.isNull() || holdReference.isNull() || holdReference.height() <= 0) {
        return normalizedBaseScale;
    }

    const qreal desiredThickness =
        static_cast<qreal>(tapReference.height()) * static_cast<qreal>(miacode::preview_skin::kHoldWidthRelativeToTap);
    return normalizedBaseScale * (desiredThickness / static_cast<qreal>(holdReference.height()));
}

TimelineHoldPixmapParts buildHoldPixmapParts(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale,
    int fallbackPixelSize)
{
    return buildHoldPixmapPartsForTargetSize(
        assets,
        type,
        targetSizeForNoteType(assets, type, scale, fallbackPixelSize),
        fallbackPixelSize);
}

TimelineHoldPixmapParts buildHoldPixmapPartsForTargetSize(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    const QSize& targetSize,
    int /*fallbackPixelSize*/)
{
    TimelineHoldPixmapParts parts;
    if (!targetSize.isValid()) {
        return parts;
    }
    const QPixmap holdCapPixmap = transformNotePixmapToTargetSize(
        assets,
        type,
        targetSize,
        90.0,
        false);
    if (holdCapPixmap.isNull()) {
        return parts;
    }

    const QImage capImage = holdCapPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int midX = qBound(0, capImage.width() / 2, capImage.width() - 1);
    const int maxCapWidth = qMax(1, capImage.width() / 2);
    const int capWidth = qMax(
        1,
        qMin(
            maxCapWidth,
            qRound(
                static_cast<qreal>(capImage.width())
                * static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioNumerator)
                / static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioDenominator))));
    parts.bodySlice = capImage.copy(midX, 0, 1, capImage.height());
    parts.leftCap = holdCapPixmap.copy(0, 0, capWidth, holdCapPixmap.height());
    parts.rightCap = holdCapPixmap.copy(
        qMax(0, holdCapPixmap.width() - capWidth),
        0,
        capWidth,
        holdCapPixmap.height());
    return parts;
}

}  // namespace miacode::timeline
