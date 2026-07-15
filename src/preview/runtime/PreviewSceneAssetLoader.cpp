#include "preview/runtime/PreviewSceneAssetLoader.h"

#include "common/AssetPaths.h"
#include "common/LayoutRingConfig.h"
#include "common/OperationLog.h"
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
constexpr qreal kJudgeEffectHoldSustainAlphaTightenGamma = 1.0;
constexpr qreal kPausedJudgeAreaLabelBrightness = 0.25;
const QColor kJudgeEffectTouchCircleTint = QColor::fromRgbF(1.0, 0.9943893, 0.4669811, 1.0);
const QColor kJudgeEffectTouchPartTint = QColor::fromRgbF(1.0, 0.9000474, 0.4666667, 1.0);
constexpr const char* kJudgeEffectResourceRoot = ":/preview/judge_effects";

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

QImage brightnessAdjustedImage(const QImage& source, qreal brightness)
{
    if (source.isNull()) {
        return QImage();
    }
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const qreal clampedBrightness = qBound<qreal>(0.0, brightness, 1.0);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            line[x] = qRgba(
                qBound(0, qRound(qRed(pixel) * clampedBrightness), 255),
                qBound(0, qRound(qGreen(pixel) * clampedBrightness), 255),
                qBound(0, qRound(qBlue(pixel) * clampedBrightness), 255),
                qAlpha(pixel)
            );
        }
    }
    return image;
}

QImage buildPausedJudgeAreaComposite(const QString& customOutlinePath)
{
    const QImage customOutline = loadImageIfExists(customOutlinePath);
    const QImage judgeArea = loadImageIfExists(miacode::assets::outlineJudgeAreaPath());
    const QImage defaultOutline = loadImageIfExists(miacode::assets::outlineLinePath());
    const QImage labelsOverlay = loadImageIfExists(miacode::assets::outlineRegionLabelsOverlayPath());
    if (customOutline.isNull() || judgeArea.isNull() || defaultOutline.isNull() || labelsOverlay.isNull()) {
        return QImage();
    }

    QImage composite(judgeArea.size(), QImage::Format_ARGB32_Premultiplied);
    composite.fill(Qt::transparent);

    const QRect targetRect(QPoint(0, 0), judgeArea.size());
    QPainter painter(&composite);
    painter.drawImage(targetRect, customOutline);

    QImage judgeAreaWithoutDefaultOutline(judgeArea.size(), QImage::Format_ARGB32_Premultiplied);
    judgeAreaWithoutDefaultOutline.fill(Qt::transparent);
    {
        QPainter areaPainter(&judgeAreaWithoutDefaultOutline);
        areaPainter.drawImage(targetRect, judgeArea);
        areaPainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        areaPainter.drawImage(targetRect, defaultOutline);
    }

    painter.drawImage(targetRect, judgeAreaWithoutDefaultOutline);
    painter.drawImage(targetRect, brightnessAdjustedImage(labelsOverlay, kPausedJudgeAreaLabelBrightness));
    painter.end();
    return composite;
}

QRectF nonTransparentBounds(const QImage& image);

miacode::preview::scene::PreviewJudgeTextSprite loadJudgeTextSprite(const QStringList& paths)
{
    miacode::preview::scene::PreviewJudgeTextSprite sprite;
    sprite.image = loadFirstImageIfExists(paths);
    sprite.sourceRect = nonTransparentBounds(sprite.image);
    return sprite;
}

QStringList rootThenLegacyJudgeTextPaths(const QDir& dir, const QString& fileName)
{
    return QStringList{
        dir.filePath(fileName),
        dir.filePath(QStringLiteral("JudgeTextSkins/%1").arg(fileName)),
    };
}

QStringList rootThenLegacySlideJudgePaths(const QDir& dir, const QString& fileName)
{
    return QStringList{
        dir.filePath(fileName),
        dir.filePath(QStringLiteral("SlideOKSkins/%1").arg(fileName)),
    };
}

QStringList rootThenLegacyFileNames(const QDir& dir, const QString& primaryFile, const QString& legacyFile)
{
    return QStringList{
        dir.filePath(primaryFile),
        dir.filePath(legacyFile),
    };
}

QString judgeEffectResourcePath(const QString& fileName)
{
    return QStringLiteral("%1/%2").arg(QLatin1String(kJudgeEffectResourceRoot), fileName);
}

void loadDirectionalSpriteSet(
    const QDir& dir,
    const QString& straightLeftFile,
    const QString& straightRightFile,
    const QString& circleLeftFile,
    const QString& circleRightFile,
    const QString& wifiUpFile,
    const QString& wifiDownFile,
    miacode::preview::scene::PreviewJudgeDirectionalSpriteSet* set
)
{
    if (set == nullptr) {
        return;
    }
    set->straightLeftImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, straightLeftFile));
    set->straightRightImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, straightRightFile));
    set->circleLeftImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, circleLeftFile));
    set->circleRightImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, circleRightFile));
    set->wifiUpImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, wifiUpFile));
    set->wifiDownImage = loadFirstImageIfExists(rootThenLegacySlideJudgePaths(dir, wifiDownFile));
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
    skin->touchBorder2BreakImage = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touch_break_border_2.png", "touch_border_2_break.png"));
    skin->touchBorder3Image = loadImageIfExists(dir.filePath("touch_border_3.png"));
    skin->touchBorder3EachImage = loadImageIfExists(dir.filePath("touch_border_3_each.png"));
    skin->touchBorder3BreakImage = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touch_break_border_3.png", "touch_border_3_break.png"));
    skin->touchPointImage = loadImageIfExists(dir.filePath("touch_point.png"));
    skin->touchPointEachImage = loadImageIfExists(dir.filePath("touch_point_each.png"));
    skin->touchPointBreakImage = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touch_break_point.png", "touch_point_break.png"));
    skin->touchHold0Image = loadImageIfExists(dir.filePath("touchhold_0.png"));
    skin->touchHold1Image = loadImageIfExists(dir.filePath("touchhold_1.png"));
    skin->touchHold2Image = loadImageIfExists(dir.filePath("touchhold_2.png"));
    skin->touchHold3Image = loadImageIfExists(dir.filePath("touchhold_3.png"));
    skin->touchHoldBorderImage = loadImageIfExists(dir.filePath("touchhold_border.png"));
    skin->touchHoldBreak0Image = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touchhold_break_0.png", "touchhold_0_break.png"));
    skin->touchHoldBreak1Image = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touchhold_break_1.png", "touchhold_1_break.png"));
    skin->touchHoldBreak2Image = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touchhold_break_2.png", "touchhold_2_break.png"));
    skin->touchHoldBreak3Image = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touchhold_break_3.png", "touchhold_3_break.png"));
    skin->touchHoldBreakBorderImage = loadFirstImageIfExists(
        rootThenLegacyFileNames(dir, "touchhold_break_border.png", "touchhold_border_break.png"));

    // Mine-note sprites (simai `m`). Convention: <base>_mine.png. Both built-in
    // skins (skinSD, skinDX) ship these; user skins may omit them, in which case
    // the images stay null and the selectors fall back to the normal sprite.
    skin->tapMineImage = loadImageIfExists(dir.filePath("tap_mine.png"));
    skin->holdMineImage = loadImageIfExists(dir.filePath("hold_mine.png"));
    skin->starMineImage = loadImageIfExists(dir.filePath("star_mine.png"));
    skin->starMineDoubleImage = loadImageIfExists(dir.filePath("star_mine_double.png"));
    skin->slideTrackMineImage = loadImageIfExists(dir.filePath("slide_mine.png"));
    skin->touchCornerMineImage = loadImageIfExists(dir.filePath("touch_mine.png"));
    skin->touchBorder2MineImage = loadImageIfExists(dir.filePath("touch_border_2_mine.png"));
    skin->touchBorder3MineImage = loadImageIfExists(dir.filePath("touch_border_3_mine.png"));
    skin->touchPointMineImage = loadImageIfExists(dir.filePath("touch_point_mine.png"));
    skin->touchHoldMine0Image = loadImageIfExists(dir.filePath("touchhold_0_mine.png"));
    skin->touchHoldMine1Image = loadImageIfExists(dir.filePath("touchhold_1_mine.png"));
    skin->touchHoldMine2Image = loadImageIfExists(dir.filePath("touchhold_2_mine.png"));
    skin->touchHoldMine3Image = loadImageIfExists(dir.filePath("touchhold_3_mine.png"));
    skin->touchHoldBorderMineImage = loadImageIfExists(dir.filePath("touchhold_border_mine.png"));

    skin->noteGuideNormalImage = loadGuideImageScaled(noteGuideDir, "Normal.png");
    skin->noteGuideBreakImage = loadGuideImageScaled(noteGuideDir, "Break.png");
    if (skin->noteGuideBreakImage.isNull()) {
        skin->noteGuideBreakImage = skin->noteGuideNormalImage;
    }
    skin->noteGuideEachImage = loadGuideImageScaled(noteGuideDir, "Each.png");
    if (skin->noteGuideEachImage.isNull()) {
        skin->noteGuideEachImage = skin->noteGuideNormalImage;
    }
    // Mine approach guide (distinct from the normal/break/each guide). Source:
    // MajdataMine_View Mine.png (the mine note's approach ring). Falls back to
    // the normal guide if the skin/noteguide set has no mine guide.
    skin->noteGuideMineImage = loadGuideImageScaled(noteGuideDir, "Mine.png");
    if (skin->noteGuideMineImage.isNull()) {
        skin->noteGuideMineImage = skin->noteGuideNormalImage;
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
    skin->noteGuideHoldMineEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Mine_End.png");
    if (skin->noteGuideHoldMineEndImage.isNull()) {
        skin->noteGuideHoldMineEndImage = skin->noteGuideHoldEndImage;
    }
    skin->noteGuideSlideImage = loadGuideImageScaled(noteGuideDir, "Slide.png");
    if (skin->noteGuideSlideImage.isNull()) {
        skin->noteGuideSlideImage = skin->noteGuideNormalImage;
    }

    for (int i = 0; i <= 10; ++i) {
        const QImage wifiImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_%1.png").arg(i)));
        const QImage wifiEachImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_each_%1.png").arg(i)));
        const QImage wifiBreakImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_break_%1.png").arg(i)));
        const QImage wifiMineImage = loadImageIfExists(dir.filePath(QStringLiteral("wifi_mine_%1.png").arg(i)));
        if (!wifiImage.isNull()) {
            skin->wifiImages.append(wifiImage);
        }
        if (!wifiEachImage.isNull()) {
            skin->wifiEachImages.append(wifiEachImage);
        }
        if (!wifiBreakImage.isNull()) {
            skin->wifiBreakImages.append(wifiBreakImage);
        }
        if (!wifiMineImage.isNull()) {
            skin->wifiMineImages.append(wifiMineImage);
        }
    }
}

void populateJudgeOverlayAssets(const QString& skinDirectory, miacode::preview::scene::PreviewJudgeOverlayAssets* overlay)
{
    if (overlay == nullptr || skinDirectory.isEmpty()) {
        return;
    }
    const QDir dir(skinDirectory);
    overlay->simpleText.normal = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_normal.png"));
    overlay->simpleText.breakText = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_break.png"));
    overlay->simpleText.good = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_good.png"));
    overlay->simpleText.great = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_great.png"));
    overlay->simpleText.perfect = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_perfect.png"));
    overlay->simpleText.cPerfect = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_cPerfect.png"));
    overlay->simpleText.miss = loadJudgeTextSprite(rootThenLegacyJudgeTextPaths(dir, "judge_text_miss.png"));
    overlay->simpleText.fast = loadJudgeTextSprite(QStringList{dir.filePath("fast.png")});
    overlay->simpleText.late = loadJudgeTextSprite(QStringList{dir.filePath("late.png")});

    loadDirectionalSpriteSet(
        dir,
        "just_str_l.png",
        "just_str_r.png",
        "just_curv_l.png",
        "just_curv_r.png",
        "just_wifi_u.png",
        "just_wifi_d.png",
        &overlay->neutral
    );
    loadDirectionalSpriteSet(
        dir,
        "just_str_l_fast_gd.png",
        "just_str_r_fast_gd.png",
        "just_curv_l_fast_gd.png",
        "just_curv_r_fast_gd.png",
        "just_wifi_u_fast_gd.png",
        "just_wifi_d_fast_gd.png",
        &overlay->fastGood
    );
    loadDirectionalSpriteSet(
        dir,
        "just_str_l_fast_gr.png",
        "just_str_r_fast_gr.png",
        "just_curv_l_fast_gr.png",
        "just_curv_r_fast_gr.png",
        "just_wifi_u_fast_gr.png",
        "just_wifi_d_fast_gr.png",
        &overlay->fastGreat
    );
    loadDirectionalSpriteSet(
        dir,
        "just_str_l_late_gd.png",
        "just_str_r_late_gd.png",
        "just_curv_l_late_gd.png",
        "just_curv_r_late_gd.png",
        "just_wifi_u_late_gd.png",
        "just_wifi_d_late_gd.png",
        &overlay->lateGood
    );
    loadDirectionalSpriteSet(
        dir,
        "just_str_l_late_gr.png",
        "just_str_r_late_gr.png",
        "just_curv_l_late_gr.png",
        "just_curv_r_late_gr.png",
        "just_wifi_u_late_gr.png",
        "just_wifi_d_late_gr.png",
        &overlay->lateGreat
    );
    loadDirectionalSpriteSet(
        dir,
        "miss_str_l.png",
        "miss_str_r.png",
        "miss_curv_l.png",
        "miss_curv_r.png",
        "miss_wifi_u.png",
        "miss_wifi_d.png",
        &overlay->miss
    );
}

void populateJudgeEffectAssets(miacode::preview::scene::PreviewJudgeEffectAssets* effect)
{
    if (effect == nullptr) {
        return;
    }
    effect->tapImage = loadImageIfExists(judgeEffectResourcePath("judge_effect_tap.png"));
    if (effect->tapImage.isNull()) {
        effect->tapImage = buildJudgeEffectTapFallbackImage();
    }
    effect->tapSourceRect = nonTransparentBounds(effect->tapImage);
    effect->tapBreakImage = loadImageIfExists(judgeEffectResourcePath("judge_effect_tap_break.png"));
    if (effect->tapBreakImage.isNull()) {
        effect->tapBreakImage = buildJudgeEffectTapBreakFallbackImage();
    }
    effect->tapBreakSourceRect = nonTransparentBounds(effect->tapBreakImage);
    effect->tapBreakDxImage = loadImageIfExists(judgeEffectResourcePath("judge_effect_tap_break_DX.png"));
    if (effect->tapBreakDxImage.isNull()) {
        effect->tapBreakDxImage = effect->tapBreakImage;
    }
    effect->tapBreakDxSourceRect = nonTransparentBounds(effect->tapBreakDxImage);

    QImage sustain = loadImageIfExists(judgeEffectResourcePath("judge_effect_hold_sustain_circle.png"));
    effect->holdSustainCircleImage = alphaTightenedSpriteImage(sustain, kJudgeEffectHoldSustainAlphaTightenGamma);
    QImage sustainDx = loadImageIfExists(judgeEffectResourcePath("judge_effect_hold_sustain_circle_DX.png"));
    effect->holdSustainCircleDxImage = alphaTightenedSpriteImage(sustainDx, kJudgeEffectHoldSustainAlphaTightenGamma);
    if (effect->holdSustainCircleDxImage.isNull()) {
        effect->holdSustainCircleDxImage = effect->holdSustainCircleImage;
    }
    const QImage touchCircle = loadImageIfExists(judgeEffectResourcePath("judge_effect_touch_circle.png"));
    const QImage touchPart01 = loadImageIfExists(judgeEffectResourcePath("judge_effect_touch_part_01.png"));
    const QImage touchPart02 = loadImageIfExists(judgeEffectResourcePath("judge_effect_touch_part_02.png"));
    const QImage touchPart02Dx = loadImageIfExists(judgeEffectResourcePath("judge_effect_touch_part_02_DX.png"));
    effect->touchCircleImage = touchCircle.isNull() ? QImage() : tintedSpriteImage(touchCircle, kJudgeEffectTouchCircleTint);
    effect->touchPart01Image = touchPart01.isNull() ? QImage() : tintedSpriteImage(touchPart01, kJudgeEffectTouchPartTint);
    effect->touchPart02Image = touchPart02.isNull() ? QImage() : tintedSpriteImage(touchPart02, kJudgeEffectTouchPartTint);
    effect->touchPart02DxImage = touchPart02Dx.isNull()
        ? effect->touchPart02Image
        : touchPart02Dx.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    effect->fireworkColorBallImage = loadImageIfExists(judgeEffectResourcePath("judge_effect_firework_color_ball.png"));
    if (effect->fireworkColorBallImage.isNull()) {
        effect->fireworkColorBallImage = buildJudgeEffectFireworkColorBallFallbackImage();
    }
    effect->fireworkColorBallSourceRect = nonTransparentBounds(effect->fireworkColorBallImage);
}

}  // namespace

PreviewSceneAssetLoadResult PreviewSceneAssetLoader::load(
    const QString& skinDirectory,
    PreviewOutlineVariant outlineVariant,
    quint64 generation,
    const QString& outlineImagePath,
    PreviewOutlineImageMode outlineImageMode)
{
    MC_OP("PreviewSceneAssetLoader::load");
    _mc_op_.note(QStringLiteral("skin=%1 generation=%2")
                     .arg(skinDirectory)
                     .arg(generation));
    PreviewSceneAssetLoadResult result;
    result.generation = generation;
    result.skinDirectory = skinDirectory;
    result.assetState = loadAssetState(outlineVariant, outlineImagePath, outlineImageMode);
    populateSkinAssets(skinDirectory, &result.skinAssets);
    populateJudgeOverlayAssets(skinDirectory, &result.judgeOverlayAssets);
    populateJudgeEffectAssets(&result.judgeEffectAssets);
    return result;
}

miacode::preview::scene::PreviewAssetState PreviewSceneAssetLoader::loadAssetState(
    PreviewOutlineVariant outlineVariant,
    const QString& outlineImagePath,
    PreviewOutlineImageMode outlineImageMode)
{
    miacode::preview::scene::PreviewAssetState assets;
    if (outlineImageMode == PreviewOutlineImageMode::PausedJudgeAreaComposite) {
        assets.outlineImage = buildPausedJudgeAreaComposite(outlineImagePath);
        if (assets.outlineImage.isNull()) {
            const QString fallbackPath = miacode::assets::outlinePathForVariant(PreviewOutlineVariant::JudgeAreaLabeled);
            assets.outlineImage = fallbackPath.isEmpty() ? QImage() : QImage(fallbackPath);
        }
    } else {
        const QString outlinePath = miacode::assets::outlinePathForVariantOrCustom(outlineVariant, outlineImagePath);
        assets.outlineImage = outlinePath.isEmpty() ? QImage() : QImage(outlinePath);
    }
    assets.layoutRingDiameterRatio = miacode::layout_ring::kOutlinePlayfieldDiameterRatio;
    return assets;
}

}  // namespace miacode::preview::runtime
