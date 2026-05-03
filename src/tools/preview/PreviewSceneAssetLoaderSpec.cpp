#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>

#include "common/AssetPaths.h"
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

bool verifyDxRootLayout(QTextStream& err)
{
    const QString dxDir = miacode::assets::assetPath(QStringLiteral("skinDX"));
    if (!require(!dxDir.isEmpty(), QStringLiteral("skinDX asset directory was not resolved"), err)) {
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
            miacode::assets::assetPath(QStringLiteral("skin/just_str_l_fast_gd.png")),
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

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyDxRootLayout(err)) {
        return 1;
    }
    if (!verifyLegacyFallback(err)) {
        return 1;
    }

    out << "preview_asset_loader_spec ok" << Qt::endl;
    return 0;
}
