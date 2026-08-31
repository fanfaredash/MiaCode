#include "tools/cover_export/SceneFrameRenderer.h"

#include "tools/video_export/VideoExportController.h"

#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/scene/PreviewLayerOrder.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QColor>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <Qt>

#include <memory>

namespace miacode::cover_export {

SceneFrameRenderer::SceneFrameRenderer() = default;

SceneFrameRenderer::~SceneFrameRenderer()
{
    // window_ owns its content item, which owns sceneRoot_ (parented in its ctor),
    // so deleting the window tears the whole scene down.
    QObject::disconnect(sceneGraphInitializedConnection_);
    QObject::disconnect(sceneGraphInvalidatedConnection_);
    QObject::disconnect(sceneGraphErrorConnection_);
    sceneRoot_ = nullptr;
    delete window_;
    window_ = nullptr;
}

bool SceneFrameRenderer::bootstrap(const VideoExportTask& task, QString* errorMessage)
{
    assets_.setOutlineSelection(task.outlineVariant, task.outlineImagePath);
    assets_.setStageMediaAvailable(false);   // the cover frame never shows the song bg
    if (!assets_.loadSkinDirectorySync(task.skinDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to load chart skin assets");
        }
        ready_ = false;
        return false;
    }

    // Build the base frame state exactly the way the video export does
    // (VideoExportQuickRenderBackend::bootstrap), minus the HUD/center-display
    // chrome a cover doesn't want.
    frameState_ = miacode::preview::scene::PreviewFrameState();
    frameState_.noteMarkers = task.noteMarkers;
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(task.noteMarkers);
    frameState_.progressStatsCache = cache;
    frameState_.muriAnalysisReport = task.muriAnalysisReport;
    frameState_.muriRenderOptions = task.muriRenderOptions;
    frameState_.sceneContentRevision = ++sceneContentRevision_;
    frameState_.media.stageMediaAvailable = false;
    frameState_.render.backgroundBrightnessOuter = task.backgroundBrightnessOuter;
    frameState_.render.backgroundBrightnessInner = task.backgroundBrightnessInner;
    frameState_.render.layoutSquareScale =
        miacode::preview_video::normalizedLayoutSquareScale(task.layoutSquareScale);
    frameState_.render.smoothBrightness = task.smoothBrightness;
    frameState_.render.backgroundScaleMode = task.backgroundScaleMode;
    frameState_.render.tapFlowSpeed =
        miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.tapFlowSpeed);
    frameState_.render.touchFlowSpeed =
        miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.touchFlowSpeed);
    frameState_.render.slideEarlierSecondAndTextOnTop = task.slideEarlierSecondAndTextOnTop;
    frameState_.render.tapJudgeTextDistance = task.tapJudgeTextDistance;
    frameState_.render.judgeEffectStyle = task.judgeEffectStyle;
    frameState_.render.showDebugInfo = false;
    frameState_.render.showTimestamp = false;
    frameState_.render.showObjectStatsHud = false;
    frameState_.render.showChartInfoHud = false;
    frameState_.render.centerDisplayMode = miacode::preview_gameplay::CenterDisplayMode::Off;
    frameState_.assets = assets_.assetState();
    frameState_.skin = assets_.skinAssets();
    frameState_.judgeOverlay = assets_.judgeOverlayAssets();
    frameState_.judgeEffect = assets_.judgeEffectAssets();
    frameState_.playheadSeconds = 0.0;
    miacode::preview::scene::refreshPreviewFrameStateHudStatsSnapshot(frameState_);

    contentDurationSeconds_ = qMax(0.0, task.contentDurationSeconds);

    // A new chart's state invalidates the scene's prepared cache; the next
    // capture rebuilds it. If a window already exists, re-point it at the new
    // state and let the normal readiness gate observe the rebuilt scene graph.
    if (sceneRoot_ != nullptr) {
        sceneRoot_->setFrameState(&frameState_);
    }

    ready_ = true;
    return true;
}

bool SceneFrameRenderer::ensureWindow(QString* errorMessage)
{
    Q_UNUSED(errorMessage);
    if (window_ != nullptr) {
        return true;
    }

    // Bare QQuickWindow at zero opacity, kept on the primary screen so macOS can
    // expose and initialize its native surface. It is transparent for input and
    // never becomes part of the visible cover UI. No QQuickView, no forced
    // OpenGL: capture stays on the application's in-process RHI.
    window_ = new QQuickWindow();
    window_->setFlags(window_->flags() | Qt::FramelessWindowHint | Qt::Tool
                      | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    window_->setColor(QColor(Qt::transparent));
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        window_->setPosition(screen->availableGeometry().topLeft());
    }
    window_->resize(256, 256);

    // PreviewQuickSceneRoot is a plain C++ QQuickItem; parenting it into the
    // content item gives it both its visual parent and QObject ownership.
    sceneRoot_ = new PreviewQuickSceneRoot(window_->contentItem());
    sceneRoot_->setZ(0.0);
    sceneRoot_->setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    sceneRoot_->setFrameState(&frameState_);

    sceneGraphReady_ = false;
    sceneGraphError_.clear();
    sceneGraphInitializedConnection_ = QObject::connect(
        window_, &QQuickWindow::sceneGraphInitialized, window_, [this]() {
            sceneGraphReady_ = true;
            sceneGraphError_.clear();
        });
    sceneGraphInvalidatedConnection_ = QObject::connect(
        window_, &QQuickWindow::sceneGraphInvalidated, window_, [this]() {
            sceneGraphReady_ = false;
        });
    sceneGraphErrorConnection_ = QObject::connect(
        window_, &QQuickWindow::sceneGraphError, window_,
        [this](QQuickWindow::SceneGraphError, const QString& message) {
            sceneGraphReady_ = false;
            sceneGraphError_ = message;
        });

    window_->setOpacity(0.0);
    window_->show();
    return true;
}

bool SceneFrameRenderer::prepareCaptureWindow(int sidePx, double seconds,
                                               QString* errorMessage)
{
    if (!ready_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("scene frame renderer not bootstrapped");
        }
        return false;
    }
    const int side = qBound(16, sidePx, 4096);
    if (!ensureWindow(errorMessage)) {
        return false;
    }

    if (window_->width() != side || window_->height() != side) {
        window_->resize(side, side);
    }
    window_->contentItem()->setSize(QSizeF(side, side));
    sceneRoot_->setSize(QSizeF(side, side));

    frameState_.playheadSeconds = seconds;
    miacode::preview::scene::refreshPreviewFrameStateHudStatsSnapshot(frameState_);
    // Re-point the (unchanged) state pointer so the scene root marks itself dirty
    // and re-runs updatePaintNode against the new playhead on the next render.
    sceneRoot_->setFrameState(&frameState_);
    window_->update();
    return true;
}

bool SceneFrameRenderer::captureReady() const
{
    return window_ != nullptr && window_->isVisible() && window_->isExposed()
        && sceneGraphReady_ && window_->isSceneGraphInitialized() && window_->rhi() != nullptr
        && sceneGraphError_.isEmpty();
}

QString SceneFrameRenderer::captureReadinessError() const
{
    if (!sceneGraphError_.isEmpty()) {
        return sceneGraphError_;
    }
    if (window_ == nullptr) {
        return QStringLiteral("capture window has not been created");
    }
    if (!window_->isVisible()) {
        return QStringLiteral("capture window is not visible");
    }
    if (!window_->isExposed()) {
        return QStringLiteral("capture window is not exposed");
    }
    if (!sceneGraphReady_ || !window_->isSceneGraphInitialized()) {
        return QStringLiteral("capture scene graph is not initialized");
    }
    if (window_->rhi() == nullptr) {
        return QStringLiteral("capture window has no active QRhi");
    }
    return {};
}

QImage SceneFrameRenderer::renderAt(double seconds, int sidePx, QString* errorMessage)
{
    const int side = qBound(16, sidePx, 4096);
    if (!prepareCaptureWindow(sidePx, seconds, errorMessage)) {
        return QImage();
    }
    if (!captureReady()) {
        if (errorMessage != nullptr) {
            *errorMessage = captureReadinessError();
        }
        return QImage();
    }

    // grabWindow() is GUI-thread-only and must not be preceded by nested event
    // processing: re-entrant events can reset the session or destroy the window
    // while Qt is preparing the scene graph. The session retries this operation
    // on a later event-loop turn until the lifecycle checks above pass.
    QImage image = window_->grabWindow();
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("grabWindow returned an empty image");
        }
        return QImage();
    }
    image.setDevicePixelRatio(1.0);
    if (image.width() != side || image.height() != side) {
        image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_ARGB32);
}

}  // namespace miacode::cover_export
