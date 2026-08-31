#include "tools/cover_export/CoverCompositeRenderer.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <Qt>
#include <QtQml>

namespace miacode::cover_export {
namespace {

constexpr char kComposerQmlUrl[] = "qrc:/intro/qml/CoverComposer.qml";
constexpr char kCoverChartImageProviderId[] = "coverchart";

void ensureComposerQmlTypesRegistered()
{
    static const bool registered = [] {
        qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
        return true;
    }();
    Q_UNUSED(registered);
}

QUrl localFileUrlOrEmpty(const QString& path)
{
    const QString trimmed = path.trimmed();
    return trimmed.isEmpty() ? QUrl() : QUrl::fromLocalFile(trimmed);
}

class CoverChartImageProvider final : public QQuickImageProvider
{
public:
    explicit CoverChartImageProvider(CoverLayoutModel* model)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , model_(model)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        Q_UNUSED(requestedSize);
        QString key = id;
        const int queryPos = key.indexOf(QLatin1Char('?'));
        if (queryPos >= 0) {
            key.truncate(queryPos);
        }
        QImage image;
        if (model_ != nullptr) {
            if (CoverLayer* layer = model_->layer(key)) {
                image = layer->frameImage();
            }
        }
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
        }
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

private:
    QPointer<CoverLayoutModel> model_;
};

void settleEvents(bool cold)
{
    if (cold) {
        for (int i = 0; i < 12; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(3);
        }
        return;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
}

void applyComposerInputs(QQuickItem* root,
                         CoverLayoutModel* model,
                         const CoverComposerInputs& inputs,
                         bool editable)
{
    if (root == nullptr) {
        return;
    }
    root->setProperty("coverLayout", QVariant::fromValue<QObject*>(model));
    root->setProperty("coverTemplate", inputs.templateMap);
    root->setProperty("trackOverrides", inputs.trackOverrides);
    root->setProperty("jacketImage", localFileUrlOrEmpty(inputs.jacketPath));
    root->setProperty("backgroundImage", localFileUrlOrEmpty(inputs.backgroundPath));
    root->setProperty("backgroundMode", static_cast<int>(inputs.backgroundMode));
    root->setProperty("blurEnabled", inputs.blurBackground);
    root->setProperty("coverBgBrightness", inputs.coverBgBrightness);
    root->setProperty("cardShadowEnabled", inputs.cardShadow);
    root->setProperty("chartFrameBgEnabled", inputs.chartFrameBackground);
    root->setProperty("chartFrameBgTransparency", inputs.chartFrameBgTransparency);
    root->setProperty("chartFrameBgBrightness", inputs.chartFrameBgBrightness);
    root->setProperty("chartFrameDiskDiameter", inputs.chartFrameDiskDiameter);
    root->setProperty("activeChartFrameKey", QString());
    root->setProperty("editable", editable);
}

QString uniqueCoverPath(const QDir& dir, const QString& extension)
{
    QString candidate = dir.filePath(QStringLiteral("card.") + extension);
    int copyIndex = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QStringLiteral("card(%1).%2").arg(copyIndex).arg(extension));
        ++copyIndex;
    }
    return QFileInfo(candidate).absoluteFilePath();
}

}  // namespace

void registerCoverChartImageProvider(QQmlEngine* engine, CoverLayoutModel* model)
{
    if (engine == nullptr) {
        return;
    }
    engine->addImageProvider(QString::fromLatin1(kCoverChartImageProviderId),
                             new CoverChartImageProvider(model));
}

QImage renderCoverComposite(CoverLayoutModel* model,
                            const CoverComposerInputs& inputs,
                            const QSize& fullSize,
                            QString* errorMessage)
{
    const int width = qMax(1, fullSize.width());
    const int height = qMax(1, fullSize.height());
    const bool transparent = inputs.backgroundMode == CoverBackgroundMode::Transparent;
    if (inputs.templateMap.isEmpty() || !inputs.templateMap.contains(QStringLiteral("card"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("banner template missing or invalid (no \"card\" entry)");
        }
        return QImage();
    }

    ensureComposerQmlTypesRegistered();
    auto* window = new QQuickWindow();
    window->setFlags(window->flags() | Qt::FramelessWindowHint | Qt::Tool
                     | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    window->setColor(transparent ? QColor(Qt::transparent) : QColor(Qt::black));
    window->resize(width, height);
    window->setPosition(-32000, -32000);

    auto* engine = new QQmlEngine(window);
    registerCoverChartImageProvider(engine, model);
    QQmlComponent component(engine, QUrl(QString::fromLatin1(kComposerQmlUrl)));
    if (component.isError()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to load CoverComposer.qml: %1")
                                .arg(component.errorString().trimmed());
        }
        delete window;
        return QImage();
    }
    QObject* object = component.create(engine->rootContext());
    auto* root = qobject_cast<QQuickItem*>(object);
    if (root == nullptr) {
        delete object;
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CoverComposer.qml root is not a QQuickItem");
        }
        delete window;
        return QImage();
    }
    root->setParentItem(window->contentItem());
    root->setParent(window->contentItem());
    root->setSize(QSizeF(width, height));
    applyComposerInputs(root, model, inputs, false);

    window->setOpacity(0.0);
    window->show();
    settleEvents(true);
    QImage image = window->grabWindow();
    if (image.isNull()) {
        settleEvents(true);
        image = window->grabWindow();
    }
    delete window;
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("grabWindow returned an empty image");
        }
        return QImage();
    }
    image.setDevicePixelRatio(1.0);
    if (image.size() != QSize(width, height)) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image.convertToFormat(transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32);
}

CoverExportResult exportCoverComposite(CoverLayoutModel* model,
                                       const CoverComposerInputs& inputs,
                                       const QSize& fullSize,
                                       const QString& outputDirectory)
{
    CoverExportResult result;
    QDir outputDir(outputDirectory);
    if (outputDirectory.trimmed().isEmpty()
        || (!outputDir.exists() && !outputDir.mkpath(QStringLiteral(".")))) {
        result.errorMessage = QStringLiteral("invalid output directory: %1").arg(outputDirectory);
        return result;
    }
    QImage image = renderCoverComposite(model, inputs, fullSize, &result.errorMessage);
    if (image.isNull()) {
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("failed to render the cover");
        }
        return result;
    }
    const bool transparent = inputs.backgroundMode == CoverBackgroundMode::Transparent;
    const QString outputPath = uniqueCoverPath(outputDir, transparent ? QStringLiteral("png")
                                                                    : QStringLiteral("jpg"));
    if (!image.save(outputPath, transparent ? "PNG" : "JPG", transparent ? -1 : 95)) {
        result.errorMessage = QStringLiteral("failed to write image: %1").arg(outputPath);
        return result;
    }
    result.success = true;
    result.outputPath = outputPath;
    return result;
}

}  // namespace miacode::cover_export
