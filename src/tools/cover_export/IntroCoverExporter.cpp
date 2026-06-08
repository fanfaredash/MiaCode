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
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <QVariantMap>
#include <Qt>

namespace miacode::cover_export {
namespace {

// Pump the event loop so async resources (the trackstart atlases, the LV digit
// atlas and the bundled FontLoaders) reach Ready before the offscreen render.
void settleEvents(int cycles)
{
    for (int i = 0; i < cycles; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
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
    track.insert(QStringLiteral("lvRenderMode"), banner.lvRenderMode);
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

// Offscreen renderer for the banner card, using PUBLIC Qt Quick API only.
//
// Two constraints learned the hard way:
//   1. A fresh QQuickView re-registers the QtQuick / QtQuick.Window modules in
//      the packaged (windeployqt) layout → "Cannot install element … into
//      protected module 'QtQuick'". A bare QQmlEngine + QQmlComponent does NOT
//      (it's how the intro export loads the same QML), so the card is loaded
//      that way and parented into a plain QQuickWindow — QQuickView is avoided
//      entirely.
//   2. This runs IN-PROCESS in the GUI app, whose scene graph already runs on
//      the platform RHI (Direct3D11 on Windows). Forcing an OpenGL context here
//      (as the export *worker* process can, since it forces OpenGL globally)
//      hard-crashes. A plain QQuickWindow + grabWindow() uses the process RHI
//      with no graphics-device override and no private QRhi headers.
class OffscreenCardRenderer
{
public:
    ~OffscreenCardRenderer() { teardown(); }

    QImage render(
        const QVariantMap& templateMap,
        const QVariantMap& trackOverrides,
        const QUrl& jacketUrl,
        bool transparentBackground,
        const QSize& size,
        QString* errorMessage)
    {
        const int width = qMax(1, size.width());
        const int height = qMax(1, size.height());

        window_ = new QQuickWindow();
        window_->setColor(transparentBackground ? QColor(Qt::transparent) : QColor(Qt::black));
        window_->setFlags(window_->flags() | Qt::FramelessWindowHint | Qt::Tool);
        window_->resize(width, height);

        engine_ = new QQmlEngine();
        QQmlComponent component(engine_, QUrl(QStringLiteral("qrc:/intro/qml/MaimaiBannerCard.qml")));
        if (component.isError()) {
            setError(errorMessage,
                QStringLiteral("failed to load banner card QML: %1").arg(component.errorString().trimmed()));
            return QImage();
        }
        QObject* object = component.create(engine_->rootContext());
        cardItem_ = qobject_cast<QQuickItem*>(object);
        if (cardItem_ == nullptr) {
            delete object;
            setError(errorMessage, QStringLiteral("banner card QML root is not a QQuickItem"));
            return QImage();
        }
        cardItem_->setParentItem(window_->contentItem());
        cardItem_->setParent(window_->contentItem());
        cardItem_->setSize(QSizeF(width, height));
        cardItem_->setProperty("width", width);
        cardItem_->setProperty("height", height);
        cardItem_->setProperty("externalTemplate", templateMap);
        cardItem_->setProperty("trackOverrides", trackOverrides);
        cardItem_->setProperty("logoImage", QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)));
        if (!jacketUrl.isEmpty()) {
            cardItem_->setProperty("jacketImage", jacketUrl);
        }
        // revealStartFrame stays -1 (default) so the card renders fully assembled
        // — no staggered fade-in or pop; frame stays 0.

        // Render fully transparent so the offscreen grab produces no on-screen
        // flash; grabWindow() re-renders the scene for readback, so the window's
        // composited opacity does not affect the captured pixels.
        window_->setOpacity(0.0);
        window_->show();
        settleEvents(8);

        QImage image = window_->grabWindow();
        if (image.isNull()) {
            // One more chance after additional async settling.
            settleEvents(4);
            image = window_->grabWindow();
        }
        window_->hide();
        if (image.isNull()) {
            setError(errorMessage, QStringLiteral("grabWindow returned an empty image"));
        }
        return image;
    }

private:
    static void setError(QString* errorMessage, const QString& text)
    {
        if (errorMessage != nullptr) {
            *errorMessage = text;
        }
    }

    void teardown()
    {
        // cardItem_ is parented to the window's content item and dies with it.
        cardItem_ = nullptr;
        delete window_;
        window_ = nullptr;
        delete engine_;
        engine_ = nullptr;
    }

    QQuickWindow* window_ = nullptr;
    QQmlEngine* engine_ = nullptr;
    QQuickItem* cardItem_ = nullptr;
};

}  // namespace

CoverExportResult exportIntroCover(
    const IntroBannerSpec& banner,
    const QSize& size,
    bool transparentBackground,
    const QString& outputDirectory,
    const QString& textOverflowMode)
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

    const QUrl jacketUrl = banner.jacketPath.isEmpty()
        ? QUrl()
        : QUrl::fromLocalFile(banner.jacketPath);

    QVariantMap track = bannerTrackOverrides(banner);
    track.insert(QStringLiteral("stillTextMode"),
        textOverflowMode.trimmed().isEmpty() ? QStringLiteral("shrink") : textOverflowMode.trimmed());

    QImage image;
    {
        OffscreenCardRenderer renderer;
        image = renderer.render(
            templateMap,
            track,
            jacketUrl,
            transparentBackground,
            QSize(width, height),
            &result.errorMessage);
    }
    if (image.isNull()) {
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("failed to render the banner card");
        }
        return result;
    }

    image.setDevicePixelRatio(1.0);
    if (image.width() != width || image.height() != height) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (transparentBackground) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    } else {
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
