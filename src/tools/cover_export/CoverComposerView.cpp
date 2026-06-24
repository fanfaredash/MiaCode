#include "tools/cover_export/CoverComposerView.h"

#include "tools/cover_export/CoverLayoutModel.h"

#include "core/scene/PreviewLayerOrder.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <QWidget>
#include <Qt>
#include <QtQml>

namespace miacode::cover_export {
namespace {

constexpr char kComposerQmlUrl[] = "qrc:/intro/qml/CoverComposer.qml";
constexpr char kCoverChartImageProviderId[] = "coverchart";

// CoverComposer.qml `import MiaCode.Preview` to host the live chart-frame edit
// scene (A2). That module is normally registered only by the quick-shell
// bootstrap, which the widget-shell cover dialog never runs — so register the one
// type the composer needs here. qmlRegisterType is process-global and idempotent;
// calling it once before EITHER engine (live preview + offscreen export) loads
// the QML is enough for the import to resolve in both. (Registering OUR type into
// MiaCode.Preview does NOT re-register the protected QtQuick modules, so it is
// safe in the packaged layout where a QQuickView would crash — see the header.)
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

// Serves a layer's rendered chart still to QML's `Image { source:
// "image://coverchart/<layerKey>?r=<rev>" }`. The "?r=<rev>" query is ignored
// here — it only exists to bust QML's URL-keyed image cache when the still is
// re-grabbed (CoverLayoutModel::setLayerImage bumps imageRevision). The image
// lives on the shared CoverLayoutModel, so the same provider class backs both the
// live preview engine and the offscreen export engine.
class CoverChartImageProvider : public QQuickImageProvider
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

// Register the cover-chart image provider on `engine` (idempotent per engine —
// addImageProvider replaces any existing provider of the same id and takes
// ownership of the new one).
void registerCoverChartImageProvider(QQmlEngine* engine, CoverLayoutModel* model)
{
    if (engine == nullptr) {
        return;
    }
    engine->addImageProvider(QString::fromLatin1(kCoverChartImageProviderId),
                             new CoverChartImageProvider(model));
}

// Pump the event loop so async resources (card fonts, the trackstart frame/tab/
// pill atlases, the LV digit atlas, the backdrop image + blur) reach Ready
// before the offscreen grab. cold = a fresh window (everything still loading) —
// settle generously; warm = a later grab where grabWindow()'s own synchronous
// polish+render suffices.
void settleEvents(bool cold)
{
    if (cold) {
        for (int i = 0; i < 12; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(3);
        }
    } else {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
}

// Apply the presentation inputs + shared layout model to a loaded CoverComposer
// root. Used by BOTH the live preview and the offscreen export render, so the
// two are pixel-identical bar resolution.
void applyComposerInputs(QQuickItem* root,
                         CoverLayoutModel* model,
                         const CoverComposerInputs& inputs,
                         const QString& activeChartFrameKey,
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
    root->setProperty("cardShadowEnabled", inputs.cardShadow);
    root->setProperty("chartFrameBgEnabled", inputs.chartFrameBackground);
    root->setProperty("chartFrameBgBrightness", inputs.chartFrameBgBrightness);
    root->setProperty("chartFrameDiskDiameter", inputs.chartFrameDiskDiameter);
    root->setProperty("activeChartFrameKey", activeChartFrameKey);
    root->setProperty("editable", editable);
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

// ===================== CoverComposerView (live preview) =====================

CoverComposerView::CoverComposerView(CoverLayoutModel* model, QObject* parent)
    : QObject(parent)
    , model_(model)
{
}

CoverComposerView::~CoverComposerView()
{
    // When embedded, QWidget::createWindowContainer takes ownership of window_
    // (and engine_ is a QObject child of window_, so it dies with it). Only when
    // the window was never embedded do we still own it here.
    root_ = nullptr;
    if (container_ == nullptr) {
        delete window_;
    }
    window_ = nullptr;
    engine_ = nullptr;
}

bool CoverComposerView::ensureLoaded()
{
    if (root_ != nullptr) {
        return true;
    }

    ensureComposerQmlTypesRegistered();

    window_ = new QQuickWindow();
    // Neutral editor backdrop — shows through in Transparent mode (the QML's own
    // black fill is hidden there), and is fully covered by the QML in the opaque
    // modes.
    window_->setColor(QColor(0x14, 0x14, 0x1C));
    window_->resize(360, 360);

    // Engine parented to the window so it outlives the scene items but is torn
    // down together with the window (whoever owns it).
    engine_ = new QQmlEngine(window_);
    registerCoverChartImageProvider(engine_, model_);
    QQmlComponent component(engine_, QUrl(QString::fromLatin1(kComposerQmlUrl)));
    if (component.isError()) {
        lastError_ = QStringLiteral("failed to load CoverComposer.qml: %1")
                         .arg(component.errorString().trimmed());
        delete window_;
        window_ = nullptr;
        engine_ = nullptr;
        return false;
    }
    QObject* object = component.create(engine_->rootContext());
    root_ = qobject_cast<QQuickItem*>(object);
    if (root_ == nullptr) {
        delete object;
        lastError_ = QStringLiteral("CoverComposer.qml root is not a QQuickItem");
        delete window_;
        window_ = nullptr;
        engine_ = nullptr;
        return false;
    }
    root_->setParentItem(window_->contentItem());
    root_->setParent(window_->contentItem());

    syncRootSize();
    applyInputs();

    connect(window_, &QQuickWindow::widthChanged, this, [this] { syncRootSize(); });
    connect(window_, &QQuickWindow::heightChanged, this, [this] { syncRootSize(); });
    return true;
}

QWidget* CoverComposerView::createContainer(QWidget* parent)
{
    if (!ensureLoaded()) {
        return nullptr;
    }
    if (container_ == nullptr) {
        container_ = QWidget::createWindowContainer(window_, parent);
        container_->setFocusPolicy(Qt::StrongFocus);
    }
    return container_;
}

QObject* CoverComposerView::previewWindowObject() const
{
    return window_;
}

void CoverComposerView::setInputs(const CoverComposerInputs& inputs)
{
    inputs_ = inputs;
    if (root_ != nullptr) {
        applyInputs();
        if (window_ != nullptr) {
            window_->update();
        }
    }
}

void CoverComposerView::setActiveChartFrameKey(const QString& key)
{
    if (activeChartFrameKey_ == key) {
        return;
    }
    activeChartFrameKey_ = key;
    if (root_ != nullptr) {
        root_->setProperty("activeChartFrameKey", activeChartFrameKey_);
        if (window_ != nullptr) {
            window_->update();
        }
    }
}

void CoverComposerView::applyInputs()
{
    applyComposerInputs(root_, model_, inputs_, activeChartFrameKey_, /*editable=*/true);
    // Live path only — the export render (renderCoverComposite) never sets this,
    // so its editable=false Loader stays inactive and the static grab Image shows.
    if (root_ != nullptr) {
        root_->setProperty("chartSceneBinder", QVariant::fromValue<QObject*>(this));
    }
}

void CoverComposerView::setChartFrameState(const miacode::preview::scene::PreviewFrameState* frameState)
{
    chartFrameState_ = frameState;
    if (liveChartScene_) {
        liveChartScene_->setFrameState(chartFrameState_);
    }
}

void CoverComposerView::refreshLiveChartScene()
{
    if (liveChartScene_) {
        // updatePaintNode reads frameState_->playheadSeconds fresh each frame, so a
        // plain update() repaints at the moved playhead with zero readback.
        liveChartScene_->update();
    }
}

void CoverComposerView::detachLiveChartScene()
{
    chartFrameState_ = nullptr;
    if (liveChartScene_) {
        liveChartScene_->setFrameState(nullptr);
    }
}

void CoverComposerView::bindLiveChartScene(QObject* sceneRoot)
{
    auto* root = qobject_cast<PreviewQuickSceneRoot*>(sceneRoot);
    if (root == nullptr) {
        return;
    }
    liveChartScene_ = root;
    // Same configuration the offscreen SceneFrameRenderer uses, so the live edit
    // scene is pixel-equivalent to the exported grab: overlay layers only (no song
    // background) over transparent, and force the QSG path even when DComp is on
    // globally (--quick-shell-beta) — otherwise updatePaintNode short-circuits to
    // nullptr and the layer renders blank.
    root->setDCompFallbackActive(true);
    root->setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    root->setFrameState(chartFrameState_);
}

void CoverComposerView::unbindLiveChartScene()
{
    liveChartScene_.clear();
}

void CoverComposerView::syncRootSize()
{
    if (root_ == nullptr || window_ == nullptr) {
        return;
    }
    root_->setWidth(window_->width());
    root_->setHeight(window_->height());
}

// ===================== Offscreen export render =====================

QImage renderCoverComposite(CoverLayoutModel* model,
                            const CoverComposerInputs& inputs,
                            const QSize& fullSize,
                            QString* errorMessage)
{
    const int width = qMax(1, fullSize.width());
    const int height = qMax(1, fullSize.height());
    const bool transparent = (inputs.backgroundMode == CoverBackgroundMode::Transparent);

    // Guard: an empty/invalid template would render a card-less image and still
    // report success. Fail loudly instead. Only reachable if the qrc-baked JSON is
    // broken — but then say so rather than silently writing a blank cover.
    if (inputs.templateMap.isEmpty() || !inputs.templateMap.contains(QStringLiteral("card"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("banner template missing or invalid (no \"card\" entry)");
        }
        return QImage();
    }

    ensureComposerQmlTypesRegistered();

    // Bare QQuickWindow parked off-screen at opacity 0 (the only legal in-process
    // capture). No QQuickView, no forced OpenGL — in-process D3D11.
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
    root->setWidth(width);
    root->setHeight(height);
    applyComposerInputs(root, model, inputs, QString(), /*editable=*/false);

    window->setOpacity(0.0);
    window->show();

    settleEvents(/*cold=*/true);
    QImage image = window->grabWindow();
    if (image.isNull()) {
        settleEvents(/*cold=*/true);
        image = window->grabWindow();
    }
    delete window;  // deletes engine (child) + the QML scene

    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("grabWindow returned an empty image");
        }
        return QImage();
    }

    image.setDevicePixelRatio(1.0);
    if (image.width() != width || image.height() != height) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    return image;
}

CoverExportResult exportCoverComposite(CoverLayoutModel* model,
                                       const CoverComposerInputs& inputs,
                                       const QSize& fullSize,
                                       const QString& outputDirectory)
{
    CoverExportResult result;

    QDir outDir(outputDirectory);
    if (outputDirectory.trimmed().isEmpty() || (!outDir.exists() && !outDir.mkpath(QStringLiteral(".")))) {
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

    const bool transparent = (inputs.backgroundMode == CoverBackgroundMode::Transparent);
    const QString extension = transparent ? QStringLiteral("png") : QStringLiteral("jpg");
    const QString outputPath = uniqueCoverPath(outDir, extension);
    const int quality = transparent ? -1 : 95;
    if (!image.save(outputPath, transparent ? "PNG" : "JPG", quality)) {
        result.errorMessage = QStringLiteral("failed to write image: %1").arg(outputPath);
        return result;
    }

    result.success = true;
    result.outputPath = outputPath;
    return result;
}

}  // namespace miacode::cover_export
