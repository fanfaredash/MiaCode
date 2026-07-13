#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>

#include "common/AssetPaths.h"
#include "core/scene/PreviewFrameState.h"
#include "core/video/PreviewRenderSettings.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool copyFileTo(const QString& source, const QString& destination, QTextStream& err)
{
    if (!QFileInfo::exists(source)) {
        err << "missing source asset: " << source << Qt::endl;
        return false;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        err << "failed to copy " << source << " -> " << destination << Qt::endl;
        return false;
    }
    return true;
}

bool saveDummyPng(const QString& destination, const QSize& size, QTextStream& err)
{
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(qRgba(255, 0, 0, 255));
    if (!image.save(destination)) {
        err << "failed to save dummy image: " << destination << Qt::endl;
        return false;
    }
    return true;
}

bool saveSinglePixelPng(
    const QString& destination,
    const QSize& size,
    const QPoint& point,
    QRgb color,
    QTextStream& err)
{
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    if (point.x() >= 0 && point.x() < image.width() && point.y() >= 0 && point.y() < image.height()) {
        image.setPixel(point, color);
    }
    if (!image.save(destination)) {
        err << "failed to save single-pixel image: " << destination << Qt::endl;
        return false;
    }
    return true;
}

QImage imageForAlphaSearch(const QImage& source, const QSize& size)
{
    if (source.size() == size) {
        return source.convertToFormat(QImage::Format_ARGB32);
    }
    return source.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32);
}

template <typename Predicate>
QPoint findPixel(const QSize& size, Predicate predicate)
{
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            if (predicate(x, y)) {
                return QPoint(x, y);
            }
        }
    }
    return QPoint(-1, -1);
}

bool imagesEqualPixels(const QImage& left, const QImage& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    const QImage leftRgba = left.convertToFormat(QImage::Format_ARGB32);
    const QImage rightRgba = right.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < leftRgba.height(); ++y) {
        const auto* leftLine = reinterpret_cast<const QRgb*>(leftRgba.constScanLine(y));
        const auto* rightLine = reinterpret_cast<const QRgb*>(rightRgba.constScanLine(y));
        for (int x = 0; x < leftRgba.width(); ++x) {
            if (leftLine[x] != rightLine[x]) {
                return false;
            }
        }
    }
    return true;
}

bool verifyDxRootLayout(QTextStream& err)
{
    const QString dxDir = miacode::assets::assetPath(QStringLiteral("skin/skinDX"));
    if (!require(!dxDir.isEmpty(), QStringLiteral("skin/skinDX asset directory was not resolved"), err)) {
        return false;
    }

    const auto result = miacode::preview::runtime::PreviewSceneAssetLoader::load(dxDir, PreviewOutlineVariant::Line);
    if (!require(
            result.judgeOverlayAssets.neutral.straightLeftImage.size() == QSize(428, 140),
            QStringLiteral("DX just_str_l.png did not load as 428x140"),
            err)) {
        return false;
    }
    if (!require(
            result.judgeOverlayAssets.neutral.straightRightImage.size() == QSize(428, 140),
            QStringLiteral("DX just_str_r.png did not load as 428x140"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.fastGood.straightLeftImage.isNull(),
            QStringLiteral("DX just_str_l_fast_gd.png did not load from the root layout"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.fastGreat.straightLeftImage.isNull(),
            QStringLiteral("DX just_str_l_fast_gr.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.lateGood.straightLeftImage.isNull(),
            QStringLiteral("DX just_str_l_late_gd.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.lateGreat.straightLeftImage.isNull(),
            QStringLiteral("DX just_str_l_late_gr.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.miss.straightLeftImage.isNull(),
            QStringLiteral("DX miss_str_l.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.great.image.isNull(),
            QStringLiteral("DX judge_text_great.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.perfect.image.isNull(),
            QStringLiteral("DX judge_text_perfect.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.cPerfect.image.isNull(),
            QStringLiteral("DX judge_text_cPerfect.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.miss.image.isNull(),
            QStringLiteral("DX judge_text_miss.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.fast.image.isNull(),
            QStringLiteral("DX fast.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.judgeOverlayAssets.simpleText.late.image.isNull(),
            QStringLiteral("DX late.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            !result.skinAssets.noteGuideHoldMineEndImage.isNull()
                && result.skinAssets.noteGuideHoldMineEndImage.size() == result.skinAssets.noteGuideHoldEndImage.size()
                && result.skinAssets.noteGuideHoldMineEndImage.size() != result.skinAssets.noteGuideMineImage.size(),
            QStringLiteral("Hold_Mine_End.png did not load as a dedicated hold-tail noteguide"),
            err)) {
        return false;
    }
    return true;
}

bool verifyBuiltInTouchHoldBreakAssets(QTextStream& err)
{
    const QStringList skinDirs{
        miacode::assets::assetPath(QStringLiteral("skin/skinSD")),
        miacode::assets::assetPath(QStringLiteral("skin/skinDX")),
    };

    for (const QString& skinDir : skinDirs) {
        if (!require(!skinDir.isEmpty(), QStringLiteral("built-in skin asset directory was not resolved"), err)) {
            return false;
        }
        const auto result = miacode::preview::runtime::PreviewSceneAssetLoader::load(skinDir, PreviewOutlineVariant::Line);
        const QString label = QFileInfo(skinDir).fileName();
        if (!require(!result.skinAssets.touchHoldBreak0Image.isNull(), label + QStringLiteral(" touchhold_break_0.png did not load"), err)) {
            return false;
        }
        if (!require(!result.skinAssets.touchHoldBreak1Image.isNull(), label + QStringLiteral(" touchhold_break_1.png did not load"), err)) {
            return false;
        }
        if (!require(!result.skinAssets.touchHoldBreak2Image.isNull(), label + QStringLiteral(" touchhold_break_2.png did not load"), err)) {
            return false;
        }
        if (!require(!result.skinAssets.touchHoldBreak3Image.isNull(), label + QStringLiteral(" touchhold_break_3.png did not load"), err)) {
            return false;
        }
        if (!require(!result.skinAssets.touchHoldBreakBorderImage.isNull(), label + QStringLiteral(" touchhold_break_border.png did not load"), err)) {
            return false;
        }
    }

    return true;
}

bool verifyLegacyFallback(QTextStream& err)
{
    QTemporaryDir tempDir;
    if (!require(tempDir.isValid(), QStringLiteral("failed to allocate temporary directory"), err)) {
        return false;
    }
    const QDir dir(tempDir.path());

    if (!copyFileTo(
            miacode::assets::assetPath(QStringLiteral("skin/skinSD/just_str_l_fast_gd.png")),
            dir.filePath(QStringLiteral("SlideOKSkins/just_str_l_fast_gd.png")),
            err)) {
        return false;
    }

    const auto fallbackResult = miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    if (!require(
            !fallbackResult.judgeOverlayAssets.fastGood.straightLeftImage.isNull(),
            QStringLiteral("legacy SlideOKSkins fallback did not load just_str_l_fast_gd.png"),
            err)) {
        return false;
    }

    if (!saveDummyPng(dir.filePath(QStringLiteral("just_str_l_fast_gd.png")), QSize(11, 13), err)) {
        return false;
    }

    const auto rootPreferredResult = miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    return require(
        rootPreferredResult.judgeOverlayAssets.fastGood.straightLeftImage.size() == QSize(11, 13),
        QStringLiteral("root-level just_str_l_fast_gd.png was not preferred over SlideOKSkins fallback"),
        err);
}

bool verifyDirectCustomOutlineMode(QTextStream& err)
{
    QTemporaryDir tempDir;
    if (!require(tempDir.isValid(), QStringLiteral("failed to allocate temporary directory"), err)) {
        return false;
    }
    const QString customPath = QDir(tempDir.path()).filePath(QStringLiteral("custom_outline.png"));
    if (!saveDummyPng(customPath, QSize(13, 17), err)) {
        return false;
    }

    const auto directState = miacode::preview::runtime::PreviewSceneAssetLoader::loadAssetState(
        PreviewOutlineVariant::JudgeAreaLabeled,
        customPath,
        miacode::preview::runtime::PreviewOutlineImageMode::Direct);
    if (!require(
            directState.outlineImage.size() == QSize(13, 17),
            QStringLiteral("direct custom outline mode unexpectedly used the built-in labeled area"),
            err)) {
        return false;
    }
    const QRgb pixel = directState.outlineImage.convertToFormat(QImage::Format_ARGB32).pixel(0, 0);
    return require(
        qRed(pixel) == 255 && qGreen(pixel) == 0 && qBlue(pixel) == 0 && qAlpha(pixel) == 255,
        QStringLiteral("direct custom outline mode did not preserve the custom outline image"),
        err);
}

bool verifyPausedCustomOutlineComposite(QTextStream& err)
{
    const QImage judgeArea(miacode::assets::outlineJudgeAreaPath());
    const QImage defaultOutline(miacode::assets::outlineLinePath());
    const QImage labelsOverlay(miacode::assets::outlineRegionLabelsOverlayPath());
    if (!require(!judgeArea.isNull(), QStringLiteral("outline_area.png did not load"), err)) {
        return false;
    }
    if (!require(!defaultOutline.isNull(), QStringLiteral("outline_line.png did not load"), err)) {
        return false;
    }
    if (!require(!labelsOverlay.isNull(), QStringLiteral("region label overlay did not load"), err)) {
        return false;
    }

    const QImage areaSearch = imageForAlphaSearch(judgeArea, judgeArea.size());
    const QImage defaultOutlineSearch = imageForAlphaSearch(defaultOutline, judgeArea.size());
    const QImage labelsSearch = imageForAlphaSearch(labelsOverlay, judgeArea.size());
    const QPoint customPoint = findPixel(judgeArea.size(), [&areaSearch, &labelsSearch](int x, int y) {
        return qAlpha(areaSearch.pixel(x, y)) == 0 && qAlpha(labelsSearch.pixel(x, y)) == 0;
    });
    if (!require(customPoint.x() >= 0, QStringLiteral("could not find a transparent custom-outline test pixel"), err)) {
        return false;
    }
    const QPoint areaPoint = findPixel(judgeArea.size(), [&areaSearch, &defaultOutlineSearch, &labelsSearch](int x, int y) {
        return qAlpha(areaSearch.pixel(x, y)) > 0
            && qAlpha(defaultOutlineSearch.pixel(x, y)) == 0
            && qAlpha(labelsSearch.pixel(x, y)) == 0;
    });
    if (!require(areaPoint.x() >= 0, QStringLiteral("could not find an area-only test pixel"), err)) {
        return false;
    }
    const QPoint defaultOutlinePoint = findPixel(
        judgeArea.size(),
        [&areaSearch, &defaultOutlineSearch, &labelsSearch](int x, int y) {
            return qAlpha(areaSearch.pixel(x, y)) > 0
                && qAlpha(defaultOutlineSearch.pixel(x, y)) > 0
                && qAlpha(labelsSearch.pixel(x, y)) == 0;
        });
    if (!require(defaultOutlinePoint.x() >= 0, QStringLiteral("could not find a default-outline test pixel"), err)) {
        return false;
    }

    QTemporaryDir tempDir;
    if (!require(tempDir.isValid(), QStringLiteral("failed to allocate temporary directory"), err)) {
        return false;
    }
    const QString customPath = QDir(tempDir.path()).filePath(QStringLiteral("custom_outline.png"));
    if (!saveDummyPng(customPath, judgeArea.size(), err)) {
        return false;
    }

    const auto compositeState = miacode::preview::runtime::PreviewSceneAssetLoader::loadAssetState(
        PreviewOutlineVariant::JudgeAreaLabeled,
        customPath,
        miacode::preview::runtime::PreviewOutlineImageMode::PausedJudgeAreaComposite);
    const QImage composite = compositeState.outlineImage.convertToFormat(QImage::Format_ARGB32);
    if (!require(
            composite.size() == judgeArea.size(),
            QStringLiteral("paused custom outline composite did not use the judge-area canvas size"),
            err)) {
        return false;
    }

    const QRgb customPixel = composite.pixel(customPoint);
    if (!require(
            qRed(customPixel) > 200 && qGreen(customPixel) < 20 && qBlue(customPixel) < 20 && qAlpha(customPixel) == 255,
            QStringLiteral("paused custom outline composite did not include the custom outline base"),
            err)) {
        return false;
    }
    const QRgb defaultOutlinePixel = composite.pixel(defaultOutlinePoint);
    if (!require(
            qRed(defaultOutlinePixel) > 200
                && qGreen(defaultOutlinePixel) < 20
                && qBlue(defaultOutlinePixel) < 20
                && qAlpha(defaultOutlinePixel) == 255,
            QStringLiteral("paused custom outline composite included the default outline ring"),
            err)) {
        return false;
    }
    if (!require(
            composite.pixel(areaPoint) != qRgba(255, 0, 0, 255),
            QStringLiteral("paused custom outline composite did not include the judge-area region overlay"),
            err)) {
        return false;
    }

    QImage areaOnly(judgeArea.size(), QImage::Format_ARGB32_Premultiplied);
    areaOnly.fill(Qt::transparent);
    {
        QPainter painter(&areaOnly);
        painter.drawImage(QRect(QPoint(0, 0), judgeArea.size()), judgeArea);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.drawImage(QRect(QPoint(0, 0), judgeArea.size()), defaultOutline);
    }
    const QImage areaOnlyRgba = areaOnly.convertToFormat(QImage::Format_ARGB32);
    const QPoint labelPoint = findPixel(judgeArea.size(), [&labelsSearch, &composite, &areaOnlyRgba](int x, int y) {
        return qAlpha(labelsSearch.pixel(x, y)) > 0 && composite.pixel(x, y) != areaOnlyRgba.pixel(x, y);
    });
    if (!require(
            labelPoint.x() >= 0,
            QStringLiteral("paused custom outline composite did not include the region label overlay"),
            err)) {
        return false;
    }

    const QImage builtInLabeled(miacode::assets::outlineJudgeAreaLabeledPath());
    const auto fallbackState = miacode::preview::runtime::PreviewSceneAssetLoader::loadAssetState(
        PreviewOutlineVariant::JudgeAreaLabeled,
        QDir(tempDir.path()).filePath(QStringLiteral("missing.png")),
        miacode::preview::runtime::PreviewOutlineImageMode::PausedJudgeAreaComposite);
    return require(
        imagesEqualPixels(fallbackState.outlineImage, builtInLabeled),
        QStringLiteral("paused custom outline composite did not fall back to the built-in labeled area"),
        err);
}

bool verifyTouchBreakNamePriority(QTextStream& err)
{
    QTemporaryDir tempDir;
    if (!require(tempDir.isValid(), QStringLiteral("failed to allocate temporary directory"), err)) {
        return false;
    }
    const QDir dir(tempDir.path());

    using PreviewSkinAssets = miacode::preview::scene::PreviewSkinAssets;
    struct Mapping {
        const char* primary;
        const char* legacy;
        QImage PreviewSkinAssets::*member;
        QSize primarySize;
        QSize legacySize;
    };
    const std::array<Mapping, 8> mappings{{
        {"touch_break_border_2.png", "touch_border_2_break.png", &PreviewSkinAssets::touchBorder2BreakImage, QSize(11, 13), QSize(31, 37)},
        {"touch_break_border_3.png", "touch_border_3_break.png", &PreviewSkinAssets::touchBorder3BreakImage, QSize(12, 14), QSize(32, 38)},
        {"touch_break_point.png", "touch_point_break.png", &PreviewSkinAssets::touchPointBreakImage, QSize(13, 15), QSize(33, 39)},
        {"touchhold_break_0.png", "touchhold_0_break.png", &PreviewSkinAssets::touchHoldBreak0Image, QSize(14, 16), QSize(34, 40)},
        {"touchhold_break_1.png", "touchhold_1_break.png", &PreviewSkinAssets::touchHoldBreak1Image, QSize(15, 17), QSize(35, 41)},
        {"touchhold_break_2.png", "touchhold_2_break.png", &PreviewSkinAssets::touchHoldBreak2Image, QSize(16, 18), QSize(36, 42)},
        {"touchhold_break_3.png", "touchhold_3_break.png", &PreviewSkinAssets::touchHoldBreak3Image, QSize(17, 19), QSize(37, 43)},
        {"touchhold_break_border.png", "touchhold_border_break.png", &PreviewSkinAssets::touchHoldBreakBorderImage, QSize(18, 20), QSize(38, 44)},
    }};

    for (const Mapping& mapping : mappings) {
        if (!saveDummyPng(dir.filePath(QString::fromLatin1(mapping.primary)), mapping.primarySize, err)) {
            return false;
        }
        if (!saveDummyPng(dir.filePath(QString::fromLatin1(mapping.legacy)), mapping.legacySize, err)) {
            return false;
        }
    }

    const auto primaryResult = miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    for (const Mapping& mapping : mappings) {
        const QImage& image = primaryResult.skinAssets.*(mapping.member);
        if (!require(
                image.size() == mapping.primarySize,
                QStringLiteral("primary skin asset name was not preferred for %1").arg(QString::fromLatin1(mapping.primary)),
                err)) {
            return false;
        }
        QFile::remove(dir.filePath(QString::fromLatin1(mapping.primary)));
    }

    const auto fallbackResult = miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    for (const Mapping& mapping : mappings) {
        const QImage& image = fallbackResult.skinAssets.*(mapping.member);
        if (!require(
                image.size() == mapping.legacySize,
                QStringLiteral("legacy skin asset name did not load for %1").arg(QString::fromLatin1(mapping.legacy)),
                err)) {
            return false;
        }
    }
    return true;
}

bool verifyBundledJudgeEffects(QTextStream& err)
{
    QTemporaryDir tempDir;
    if (!require(tempDir.isValid(), QStringLiteral("failed to allocate temporary directory"), err)) {
        return false;
    }
    const QDir dir(tempDir.path());

    const auto emptySkinResult =
        miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    if (!require(
            emptySkinResult.judgeEffectAssets.tapImage.size() == QSize(256, 256),
            QStringLiteral("bundled judge_effect_tap.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.tapBreakImage.size() == QSize(256, 256),
            QStringLiteral("bundled judge_effect_tap_break.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.holdSustainCircleImage.size() == QSize(256, 256),
            QStringLiteral("bundled judge_effect_hold_sustain_circle.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.touchCircleImage.size() == QSize(512, 512),
            QStringLiteral("bundled judge_effect_touch_circle.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.touchPart01Image.size() == QSize(104, 104),
            QStringLiteral("bundled judge_effect_touch_part_01.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.touchPart02Image.size() == QSize(104, 104),
            QStringLiteral("bundled judge_effect_touch_part_02.png did not load"),
            err)) {
        return false;
    }
    if (!require(
            emptySkinResult.judgeEffectAssets.fireworkColorBallImage.size() == QSize(512, 512),
            QStringLiteral("bundled judge_effect_firework_color_ball.png did not load"),
            err)) {
        return false;
    }

    if (!saveDummyPng(dir.filePath(QStringLiteral("judge_effect_tap.png")), QSize(11, 13), err)) {
        return false;
    }
    if (!saveDummyPng(dir.filePath(QStringLiteral("judge_effect_firework_color_ball.png")), QSize(15, 17), err)) {
        return false;
    }

    const auto skinOverrideResult =
        miacode::preview::runtime::PreviewSceneAssetLoader::load(tempDir.path(), PreviewOutlineVariant::Line);
    if (!require(
            skinOverrideResult.judgeEffectAssets.tapImage.size() == QSize(256, 256),
            QStringLiteral("skin-level judge_effect_tap.png unexpectedly overrode bundled resource"),
            err)) {
        return false;
    }
    return require(
        skinOverrideResult.judgeEffectAssets.fireworkColorBallImage.size() == QSize(512, 512),
        QStringLiteral("skin-level judge_effect_firework_color_ball.png unexpectedly overrode bundled resource"),
        err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyDxRootLayout(err)) {
        return 1;
    }
    if (!verifyBuiltInTouchHoldBreakAssets(err)) {
        return 1;
    }
    if (!verifyLegacyFallback(err)) {
        return 1;
    }
    if (!verifyDirectCustomOutlineMode(err)) {
        return 1;
    }
    if (!verifyPausedCustomOutlineComposite(err)) {
        return 1;
    }
    if (!verifyTouchBreakNamePriority(err)) {
        return 1;
    }
    if (!verifyBundledJudgeEffects(err)) {
        return 1;
    }

    out << "preview_asset_loader_spec ok" << Qt::endl;
    return 0;
}
