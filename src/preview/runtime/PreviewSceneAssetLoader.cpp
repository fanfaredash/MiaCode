#include "preview/runtime/PreviewSceneAssetLoader.h"

#include "common/AssetPaths.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewSkinConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace miacode::preview::runtime {
namespace {

constexpr qreal kSkinAssetScale = static_cast<qreal>(miacode::preview_skin::kTapHeadScale);
constexpr qreal kOutlineTargetToPlayfieldRatio =
    (static_cast<qreal>(miacode::preview_gameplay::kLogicalCanvasSize)
        - miacode::layout_ring::kOutlineInsetLogical * 2.0)
    / static_cast<qreal>(miacode::preview_gameplay::kLogicalCanvasSize);
constexpr qreal kJudgeEffectHoldSustainAlphaTightenGamma = 1.0;
const QColor kJudgeEffectTouchCircleTint = QColor::fromRgbF(1.0, 0.9943893, 0.4669811, 1.0);
const QColor kJudgeEffectTouchPartTint = QColor::fromRgbF(1.0, 0.9000474, 0.4666667, 1.0);

QImage loadImageIfExists(const QString& path)
{
    return QFileInfo::exists(path) ? QImage(path) : QImage();
}

QImage loadFirstImageIfExists(const QStringList& paths)
{
    for (const QString& path : paths) {
        const QImage image = loadImageIfExists(path);
        if (!image.isNull()) {
            return image;
        }
    }
    return QImage();
}

QImage loadGuideImageScaled(const QDir& noteGuideDir, const QString& name)
{
    const QString path = noteGuideDir.filePath(name);
    if (!QFileInfo::exists(path)) {
        return QImage();
    }
    QImage image(path);
    if (image.isNull()) {
        return QImage();
    }
    const int width = qMax(1, qRound(image.width() * kSkinAssetScale));
    const int height = qMax(1, qRound(image.height() * kSkinAssetScale));
    if (width != image.width() || height != image.height()) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

QRectF nonTransparentBounds(const QImage& image)
{
    if (image.isNull()) {
        return QRectF();
    }
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) <= 0) {
                continue;
            }
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) {
        return QRectF();
    }
    return QRectF(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

QImage tintedSpriteImage(const QImage& source, const QColor& tint)
{
    if (source.isNull()) {
        return QImage();
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int tr = tint.red();
    const int tg = tint.green();
    const int tb = tint.blue();
    for (int y = 0; y < result.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a == 0) {
                continue;
            }
            row[x] = qRgba(qRed(row[x]) * tr / 255, qGreen(row[x]) * tg / 255, qBlue(row[x]) * tb / 255, a);
        }
    }
    return result;
}

QImage alphaTightenedSpriteImage(const QImage& source, qreal tightenGamma)
{
    if (source.isNull()) {
        return QImage();
    }
    const qreal gamma = qMax<qreal>(1e-4, tightenGamma);
    if (qFuzzyCompare(gamma, 1.0)) {
        return source;
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < result.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a == 0) {
                continue;
            }
            const qreal alpha01 = static_cast<qreal>(a) / 255.0;
            row[x] = qRgba(
                qRed(row[x]),
                qGreen(row[x]),
                qBlue(row[x]),
                qBound(0, qRound(qPow(alpha01, gamma) * 255.0), 255));
        }
    }
    return result;
}

QImage buildJudgeEffectTapFallbackImage()
{
    constexpr int kSize = 192;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = kSize * 0.32;
    QPolygonF hex;
    for (int i = 0; i < 6; ++i) {
        const qreal angle = qDegreesToRadians(60.0 * i - 30.0);
        hex.append(QPointF(center.x() + qCos(angle) * radius, center.y() + qSin(angle) * radius));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 248, 184, 230), radius * 0.12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(hex);
    painter.setPen(QPen(QColor(255, 243, 156, 240), radius * 0.078, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(hex);
    return image;
}

QImage buildJudgeEffectTapBreakFallbackImage()
{
    constexpr int kSize = 192;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal outerRadius = kSize * 0.34;
    const qreal innerRadius = outerRadius * 0.46;
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
        star.append(QPointF(center.x() + qCos(angle) * radius, center.y() + qSin(angle) * radius));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 248, 184, 230), outerRadius * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(star);
    painter.setPen(QPen(QColor(255, 243, 156, 240), outerRadius * 0.095, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(star);
    return image;
}

QImage buildJudgeEffectFireworkColorBallFallbackImage()
{
    constexpr int kSize = 512;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = kSize * 0.46;
    QRadialGradient gradient(center, radius);
    gradient.setColorAt(0.0, QColor(255, 245, 130, 255));
    gradient.setColorAt(0.33, QColor(255, 72, 210, 235));
    gradient.setColorAt(0.62, QColor(83, 185, 255, 215));
    gradient.setColorAt(1.0, QColor(255, 238, 96, 0));
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, radius, radius);
    return image;
}

void populateSkinAssets(const QString& skinDirectory, miacode::preview::scene::PreviewSkinAssets* skin)
{
    if (skin == nullptr || skinDirectory.isEmpty()) {
        return;
    }
    const QDir dir(skinDirectory);
    const QDir noteGuideDir(miacode::assets::assetPath("noteguide"));

    skin->tapImage = loadImageIfExists(dir.filePath("tap.png"));
    skin->tapEachImage = loadImageIfExists(dir.filePath("tap_each.png"));
    skin->tapBreakImage = loadImageIfExists(dir.filePath("tap_break.png"));
    skin->tapExImage = loadImageIfExists(dir.filePath("tap_ex.png"));
    skin->slideTrackImage = loadImageIfExists(dir.filePath("slide.png"));
    skin->slideTrackEachImage = loadImageIfExists(dir.filePath("slide_each.png"));
    skin->slideTrackBreakImage = loadImageIfExists(dir.filePath("slide_break.png"));
    skin->starImage = loadImageIfExists(dir.filePath("star.png"));
    skin->starEachImage = loadImageIfExists(dir.filePath("star_each.png"));
    skin->starBreakImage = loadImageIfExists(dir.filePath("star_break.png"));
    skin->starBreakDoubleImage = loadImageIfExists(dir.filePath("star_break_double.png"));
    skin->starDoubleImage = loadImageIfExists(dir.filePath("star_double.png"));
    skin->starEachDoubleImage = loadImageIfExists(dir.filePath("star_each_double.png"));
    skin->starExImage = loadImageIfExists(dir.filePath("star_ex.png"));
    skin->starExDoubleImage = loadImageIfExists(dir.filePath("star_ex_double.png"));
    skin->holdImage = loadImageIfExists(dir.filePath("hold.png"));
    skin->holdOnImage = loadImageIfExists(dir.filePath("hold_on.png"));
    skin->holdOffImage = loadImageIfExists(dir.filePath("hold_off.png"));
    skin->holdEachImage = loadImageIfExists(dir.filePath("hold_each.png"));
    skin->holdEachOnImage = loadImageIfExists(dir.filePath("hold_each_on.png"));
    skin->holdBreakImage = loadImageIfExists(dir.filePath("hold_break.png"));
    skin->holdBreakOnImage = loadImageIfExists(dir.filePath("hold_break_on.png"));
    skin->holdExImage = loadImageIfExists(dir.filePath("hold_ex.png"));
    skin->touchCornerImage = loadImageIfExists(dir.filePath("touch.png"));
    skin->touchCornerEachImage = loadImageIfExists(dir.filePath("touch_each.png"));
    skin->touchCornerBreakImage = loadImageIfExists(dir.filePath("touch_break.png"));
    skin->touchBorder2Image = loadImageIfExists(dir.filePath("touch_border_2.png"));
    skin->touchBorder2EachImage = loadImageIfExists(dir.filePath("touch_border_2_each.png"));
    skin->touchBorder2BreakImage = loadImageIfExists(dir.filePath("touch_break_border_2.png"));
    skin->touchBorder3Image = loadImageIfExists(dir.filePath("touch_border_3.png"));
    skin->touchBorder3EachImage = loadImageIfExists(dir.filePath("touch_border_3_each.png"));
    skin->touchBorder3BreakImage = loadImageIfExists(dir.filePath("touch_break_border_3.png"));
    skin->touchPointImage = loadImageIfExists(dir.filePath("touch_point.png"));
    skin->touchPointEachImage = loadImageIfExists(dir.filePath("touch_point_each.png"));
    skin->touchPointBreakImage = loadImageIfExists(dir.filePath("touch_break_point.png"));
    skin->touchHold0Image = loadImageIfExists(dir.filePath("touchhold_0.png"));
    skin->touchHold1Image = loadImageIfExists(dir.filePath("touchhold_1.png"));
    skin->touchHold2Image = loadImageIfExists(dir.filePath("touchhold_2.png"));
    skin->touchHold3Image = loadImageIfExists(dir.filePath("touchhold_3.png"));
    skin->touchHoldBorderImage = loadImageIfExists(dir.filePath("touchhold_border.png"));
    skin->touchHoldBreak0Image = loadImageIfExists(dir.filePath("touchhold_break_0.png"));
    skin->touchHoldBreak1Image = loadImageIfExists(dir.filePath("touchhold_break_1.png"));
    skin->touchHoldBreak2Image = loadImageIfExists(dir.filePath("touchhold_break_2.png"));
    skin->touchHoldBreak3Image = loadImageIfExists(dir.filePath("touchhold_break_3.png"));
    skin->touchHoldBreakBorderImage = loadImageIfExists(dir.filePath("touchhold_break_border.png"));
    skin->touchHoldOffImage = loadImageIfExists(dir.filePath("touchhold_off.png"));

    skin->noteGuideNormalImage = loadGuideImageScaled(noteGuideDir, "Normal.png");
    skin->noteGuideBreakImage = loadGuideImageScaled(noteGuideDir, "Break.png");
    if (skin->noteGuideBreakImage.isNull()) {
        skin->noteGuideBreakImage = skin->noteGuideNormalImage;
    }
    skin->noteGuideEachImage = loadGuideImageScaled(noteGuideDir, "Each.png");
    if (skin->noteGuideEachImage.isNull()) {
        skin->noteGuideEachImage = skin->noteGuideNormalImage;
    }
    skin->noteGuideEachLine1Image = loadGuideImageScaled(noteGuideDir, "EachLine1.png");
    skin->noteGuideEachLine2Image = loadGuideImageScaled(noteGuideDir, "EachLine2.png");
    skin->noteGuideEachLine3Image = loadGuideImageScaled(noteGuideDir, "EachLine3.png");
    skin->noteGuideEachLine4Image = loadGuideImageScaled(noteGuideDir, "EachLine4.png");
    skin->noteGuideHoldEndImage = loadGuideImageScaled(noteGuideDir, "Hold_End.png");
    skin->noteGuideHoldEachEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Each_End.png");
    if (skin->noteGuideHoldEachEndImage.isNull()) {
        skin->noteGuideHoldEachEndImage = skin->noteGuideHoldEndImage;
    }
    skin->noteGuideHoldBreakEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Break_End.png");
    if (skin->noteGuideHoldBreakEndImage.isNull()) {
        skin->noteGuideHoldBreakEndImage = skin->noteGuideHoldEndImage;
    }
    skin->noteGuideSlideImage = loadGuideImageScaled(noteGuideDir, "Slide.png");
    if (skin->noteGuideSlideImage.isNull()) {
        skin->noteGuideSlideImage = skin->noteGuideNormalImage;
    }

    for (int i = 0; i <= 10; ++i) {
        const QImage wifiImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_%1.png").arg(i)));
        const QImage wifiEachImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_each_%1.png").arg(i)));
        const QImage wifiBreakImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_break_%1.png").arg(i)));
        if (!wifiImage.isNull()) {
            skin->wifiImages.append(wifiImage);
        }
        if (!wifiEachImage.isNull()) {
            skin->wifiEachImages.append(wifiEachImage);
        }
        if (!wifiBreakImage.isNull()) {
            skin->wifiBreakImages.append(wifiBreakImage);
        }
    }
}

void populateJudgeOverlayAssets(const QString& skinDirectory, miacode::preview::scene::PreviewJudgeOverlayAssets* overlay)
{
    if (overlay == nullptr || skinDirectory.isEmpty()) {
        return;
    }
    const QDir dir(skinDirectory);
    overlay->reviewJudgeSimpleNormalImage = loadFirstImageIfExists(QStringList{
        dir.filePath("JudgeTextSkins/judge_text_normal.png"),
        dir.filePath("judge_text_normal.png")});
    overlay->reviewJudgeSimpleNormalSourceRect = nonTransparentBounds(overlay->reviewJudgeSimpleNormalImage);
    overlay->reviewJudgeSimpleBreakImage = loadFirstImageIfExists(QStringList{
        dir.filePath("JudgeTextSkins/judge_text_break.png"),
        dir.filePath("judge_text_break.png")});
    overlay->reviewJudgeSimpleBreakSourceRect = nonTransparentBounds(overlay->reviewJudgeSimpleBreakImage);
    overlay->reviewJudgeStraightLeftImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_str_l.png"), dir.filePath("SlideOKSkins/just_str_l.png")});
    overlay->reviewJudgeStraightRightImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_str_r.png"), dir.filePath("SlideOKSkins/just_str_r.png")});
    overlay->reviewJudgeCircleLeftImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_curv_l.png"), dir.filePath("SlideOKSkins/just_curv_l.png")});
    overlay->reviewJudgeCircleRightImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_curv_r.png"), dir.filePath("SlideOKSkins/just_curv_r.png")});
    overlay->reviewJudgeWifiUpImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_wifi_u.png"), dir.filePath("SlideOKSkins/just_wifi_u.png")});
    overlay->reviewJudgeWifiDownImage = loadFirstImageIfExists(QStringList{
        dir.filePath("just_wifi_d.png"), dir.filePath("SlideOKSkins/just_wifi_d.png")});
    overlay->muriJudgeSimpleImage = loadFirstImageIfExists(QStringList{
        dir.filePath("JudgeTextSkins/judge_text_good.png"), dir.filePath("judge_text_good.png")});
    overlay->muriJudgeStraightLeftImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_str_l_fast_gd.png")});
    overlay->muriJudgeStraightRightImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_str_r_fast_gd.png")});
    overlay->muriJudgeCircleLeftImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_curv_l_fast_gd.png")});
    overlay->muriJudgeCircleRightImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_curv_r_fast_gd.png")});
    overlay->muriJudgeWifiUpImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_wifi_u_fast_gd.png")});
    overlay->muriJudgeWifiDownImage = loadFirstImageIfExists(QStringList{dir.filePath("SlideOKSkins/just_wifi_d_fast_gd.png")});
}

void populateJudgeEffectAssets(const QString& skinDirectory, miacode::preview::scene::PreviewJudgeEffectAssets* effect)
{
    if (effect == nullptr) {
        return;
    }
    const QDir dir(skinDirectory);
    effect->tapImage = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_tap.png"));
    if (effect->tapImage.isNull()) {
        effect->tapImage = buildJudgeEffectTapFallbackImage();
    }
    effect->tapSourceRect = nonTransparentBounds(effect->tapImage);
    effect->tapBreakImage = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_tap_break.png"));
    if (effect->tapBreakImage.isNull()) {
        effect->tapBreakImage = buildJudgeEffectTapBreakFallbackImage();
    }
    effect->tapBreakSourceRect = nonTransparentBounds(effect->tapBreakImage);

    QImage sustain = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_hold_sustain_circle.png"));
    if (sustain.isNull() && !skinDirectory.isEmpty()) {
        sustain = loadImageIfExists(dir.filePath("circle.png"));
    }
    effect->holdSustainCircleImage = alphaTightenedSpriteImage(sustain, kJudgeEffectHoldSustainAlphaTightenGamma);
    const QImage touchCircle = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_touch_circle.png"));
    const QImage touchPart01 = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_touch_part_01.png"));
    const QImage touchPart02 = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_touch_part_02.png"));
    effect->touchCircleImage = touchCircle.isNull() ? QImage() : tintedSpriteImage(touchCircle, kJudgeEffectTouchCircleTint);
    effect->touchPart01Image = touchPart01.isNull() ? QImage() : tintedSpriteImage(touchPart01, kJudgeEffectTouchPartTint);
    effect->touchPart02Image = touchPart02.isNull() ? QImage() : tintedSpriteImage(touchPart02, kJudgeEffectTouchPartTint);
    effect->fireworkColorBallImage = skinDirectory.isEmpty() ? QImage() : loadImageIfExists(dir.filePath("judge_effect_firework_color_ball.png"));
    if (effect->fireworkColorBallImage.isNull() && !skinDirectory.isEmpty()) {
        effect->fireworkColorBallImage = loadImageIfExists(dir.filePath("ColorBall.png"));
    }
    if (effect->fireworkColorBallImage.isNull()) {
        effect->fireworkColorBallImage = buildJudgeEffectFireworkColorBallFallbackImage();
    }
    effect->fireworkColorBallSourceRect = nonTransparentBounds(effect->fireworkColorBallImage);
}

}  // namespace

PreviewSceneAssetLoadResult PreviewSceneAssetLoader::load(
    const QString& skinDirectory,
    bool stageMediaAvailable,
    quint64 generation)
{
    PreviewSceneAssetLoadResult result;
    result.generation = generation;
    result.skinDirectory = skinDirectory;
    result.assetState = loadAssetState(stageMediaAvailable);
    populateSkinAssets(skinDirectory, &result.skinAssets);
    populateJudgeOverlayAssets(skinDirectory, &result.judgeOverlayAssets);
    populateJudgeEffectAssets(skinDirectory, &result.judgeEffectAssets);
    return result;
}

miacode::preview::scene::PreviewAssetState PreviewSceneAssetLoader::loadAssetState(bool stageMediaAvailable)
{
    miacode::preview::scene::PreviewAssetState assets;
    const QString outlinePath = miacode::assets::outlinePathForStageMedia(stageMediaAvailable);
    assets.outlineImage = outlinePath.isEmpty() ? QImage() : QImage(outlinePath);
    const double textureRingRatio = detectLayoutRingDiameterRatio(assets.outlineImage);
    assets.layoutRingDiameterRatio = qBound(
        miacode::layout_ring::kPlayfieldRatioMin,
        static_cast<double>(kOutlineTargetToPlayfieldRatio) * textureRingRatio,
        miacode::layout_ring::kPlayfieldRatioMax);
    return assets;
}

double PreviewSceneAssetLoader::detectLayoutRingDiameterRatio(const QImage& source)
{
    if (source.isNull() || source.width() <= 2 || source.height() <= 2) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }
    QImage image = source.format() == QImage::Format_RGBA8888
        ? source
        : source.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }
    const int width = image.width();
    const int height = image.height();
    const int maxRadius = qMax(1, qMin(width, height) / 2);
    QVector<double> histogram(maxRadius + 1, 0.0);
    const double cx = (static_cast<double>(width) - 1.0) * 0.5;
    const double cy = (static_cast<double>(height) - 1.0) * 0.5;
    for (int y = 0; y < height; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int offset = x * 4;
            const int a = row[offset + 3];
            if (a < miacode::layout_ring::kDetectMinAlpha) {
                continue;
            }
            const int luminance = (row[offset + 0] * 3 + row[offset + 1] * 4 + row[offset + 2]) / 8;
            if (luminance < miacode::layout_ring::kDetectMinLuminance) {
                continue;
            }
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const int radius = qBound(0, qRound(std::sqrt(dx * dx + dy * dy)), maxRadius);
            histogram[radius] += static_cast<double>(a) * static_cast<double>(luminance);
        }
    }
    QVector<double> smooth(histogram.size(), 0.0);
    for (int i = 0; i < histogram.size(); ++i) {
        double sum = histogram[i] * 2.0;
        double weight = 2.0;
        if (i > 0) {
            sum += histogram[i - 1];
            weight += 1.0;
        }
        if (i + 1 < histogram.size()) {
            sum += histogram[i + 1];
            weight += 1.0;
        }
        smooth[i] = sum / qMax(1.0, weight);
    }
    const int searchStart = qBound(0, qRound(maxRadius * miacode::layout_ring::kDetectSearchStartRadiusRatio), maxRadius);
    const int searchEnd = qBound(searchStart, qRound(maxRadius * miacode::layout_ring::kDetectSearchEndRadiusRatio), maxRadius);
    int peakIndex = -1;
    double peakValue = 0.0;
    for (int i = searchStart; i <= searchEnd; ++i) {
        if (smooth[i] > peakValue) {
            peakValue = smooth[i];
            peakIndex = i;
        }
    }
    if (peakIndex < 0 || peakValue <= 1.0) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }
    const double edgeThreshold = peakValue * miacode::layout_ring::kDetectEdgeThresholdRatio;
    int innerRadius = peakIndex;
    while (innerRadius > searchStart && smooth[innerRadius - 1] >= edgeThreshold) {
        --innerRadius;
    }
    int outerRadius = peakIndex;
    while (outerRadius < searchEnd && smooth[outerRadius + 1] >= edgeThreshold) {
        ++outerRadius;
    }
    const double averageRadius = (static_cast<double>(innerRadius) + static_cast<double>(outerRadius)) * 0.5;
    const double diameterRatio = (averageRadius * 2.0) / static_cast<double>(qMax(1, qMin(width, height)));
    return qBound(
        miacode::layout_ring::kDetectDiameterRatioMin,
        diameterRatio,
        miacode::layout_ring::kDetectDiameterRatioMax);
}

}  // namespace miacode::preview::runtime
