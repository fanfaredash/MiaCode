#include "tools/cover_export/IntroCoverExporter.h"

#include "common/IntroConfig.h"
#include "tools/video_export/VideoExportController.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

namespace miacode::cover_export {
namespace {

// Pump the event loop and the QQuickView so async resources (the trackstart
// atlases, the LV digit atlas and the bundled FontLoaders) finish loading
// before the offscreen grab. The card's jacket Images use asynchronous:false,
// but the prefab/atlas/font loads still settle on posted events.
void settleRendering(QQuickView& view, int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        view.update();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
}

QVariantMap loadBannerTemplate(bool transparentBackground)
{
    QVariantMap templateMap;
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    // The bundled template ships transparentBackground:true (it composites over
    // IntroOverlay's own backdrop). Override it for the standalone cover so an
    // opaque export gets the card's self-drawn blurred-jacket backdrop.
    templateMap.insert(QStringLiteral("transparentBackground"), transparentBackground);
    return templateMap;
}

QVariantMap bannerTrackOverrides(const IntroBannerSpec& banner)
{
    QVariantMap track;
    track.insert(QStringLiteral("title"), banner.title);
    track.insert(QStringLiteral("artist"), banner.artist);
    track.insert(QStringLiteral("designer"), banner.designer);
    track.insert(QStringLiteral("level"), banner.level);
    track.insert(QStringLiteral("difficulty"), banner.difficulty);
    track.insert(QStringLiteral("bpm"), banner.bpm);
    track.insert(QStringLiteral("mode"), banner.mode);
    return track;
}

// card.<ext>, then card(1).<ext>, card(2).<ext> … on collision.
QString uniqueCoverPath(const QDir& dir, const QString& extension)
{
    const QString stem = QStringLiteral("card");
    QString candidate = dir.filePath(stem + QLatin1Char('.') + extension);
    int copyIndex = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QStringLiteral("%1(%2).%3").arg(stem).arg(copyIndex).arg(extension));
        ++copyIndex;
    }
    return QFileInfo(candidate).absoluteFilePath();
}

}  // namespace

CoverExportResult exportIntroCover(
    const IntroBannerSpec& banner,
    const QSize& size,
    bool transparentBackground,
    const QString& outputDirectory)
{
    CoverExportResult result;

    const int width = qMax(1, size.width());
    const int height = qMax(1, size.height());

    QDir outDir(outputDirectory);
    if (outputDirectory.trimmed().isEmpty() || (!outDir.exists() && !outDir.mkpath(QStringLiteral(".")))) {
        result.errorMessage = QStringLiteral("invalid output directory: %1").arg(outputDirectory);
        return result;
    }

    const QVariantMap templateMap = loadBannerTemplate(transparentBackground);
    if (templateMap.isEmpty()) {
        result.errorMessage = QStringLiteral("banner template could not be loaded from qrc");
        return result;
    }

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setColor(transparentBackground ? QColor(Qt::transparent) : QColor(Qt::black));
    view.setFlags(view.flags() | Qt::FramelessWindowHint | Qt::Tool);
    view.resize(width, height);
    view.setSource(QUrl(QStringLiteral("qrc:/intro/qml/MaimaiBannerCard.qml")));

    if (view.status() == QQuickView::Error || view.rootObject() == nullptr) {
        QString details;
        const QList<QQmlError> errors = view.errors();
        for (const QQmlError& error : errors) {
            details += error.toString() + QLatin1Char('\n');
        }
        result.errorMessage = QStringLiteral("failed to load banner card QML: %1").arg(details.trimmed());
        return result;
    }

    QQuickItem* root = view.rootObject();
    root->setWidth(width);
    root->setHeight(height);
    // Inject the parsed template directly (externalTemplate) so the card skips
    // its XMLHttpRequest loader, which never completes under an offscreen grab.
    root->setProperty("externalTemplate", templateMap);
    root->setProperty("trackOverrides", bannerTrackOverrides(banner));
    root->setProperty("logoImage", QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)));
    if (!banner.jacketPath.isEmpty()) {
        root->setProperty("jacketImage", QUrl::fromLocalFile(banner.jacketPath));
    }
    // revealStartFrame stays -1 (the default) so the card renders fully assembled
    // — no staggered fade-in or pop. frame stays 0.

    // Render fully transparent so the offscreen grab produces no on-screen flash;
    // grabWindow() reads the scene-graph render directly, so window opacity does
    // not zero the captured pixels.
    view.setOpacity(0.0);
    view.show();
    settleRendering(view, 8);

    QImage image = view.grabWindow();
    view.close();

    if (image.isNull()) {
        result.errorMessage = QStringLiteral("failed to render the banner card (empty grab)");
        return result;
    }
    image.setDevicePixelRatio(1.0);
    if (image.width() != width || image.height() != height) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (!transparentBackground && image.hasAlphaChannel()) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const QString extension = transparentBackground ? QStringLiteral("png") : QStringLiteral("jpg");
    const QString outputPath = uniqueCoverPath(outDir, extension);
    const int quality = transparentBackground ? -1 : 95;
    if (!image.save(outputPath, transparentBackground ? "PNG" : "JPG", quality)) {
        result.errorMessage = QStringLiteral("failed to write image: %1").arg(outputPath);
        return result;
    }

    result.success = true;
    result.outputPath = outputPath;
    return result;
}

}  // namespace miacode::cover_export
