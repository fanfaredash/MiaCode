#include "tools/cover_export/SceneFrameRenderer.h"

#include "tools/video_export/VideoExportController.h"

#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/scene/PreviewLayerOrder.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"

#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <Qt>

#include <memory>

namespace miacode::cover_export {
namespace {

// Pump the event loop so the skin/judge sprite textures upload and the first
// scene-graph build settle before the offscreen grab. cold = a fresh window
// (textures still uploading) — settle generously; warm = a later grab where
// grabWindow()'s own synchronous polish+render already suffices. Mirrors
// CoverComposerView::settleEvents.
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

}  // namespace

SceneFrameRenderer::SceneFrameRenderer() = default;

SceneFrameRenderer::~SceneFrameRenderer()
{
    // window_ owns its content item, which owns sceneRoot_ (parented in its ctor),
    // so deleting the window tears the whole scene down.
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

    // A new chart's state invalidates the warm scene's prepared cache; the next
    // renderAt() rebuilds it. If a window already exists, re-point it at the new
    // state and force a cold settle.
    if (sceneRoot_ != nullptr) {
        sceneRoot_->setFrameState(&frameState_);
        warmedUp_ = false;
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

    // Bare QQuickWindow parked off-screen at opacity 0 — the only legal in-process
    // capture (see the header). No QQuickView, no forced OpenGL: in-process D3D11.
    window_ = new QQuickWindow();
    window_->setFlags(window_->flags() | Qt::FramelessWindowHint | Qt::Tool
                      | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    window_->setColor(QColor(Qt::transparent));
    window_->setPosition(-32000, -32000);
    window_->resize(256, 256);

    // PreviewQuickSceneRoot is a plain C++ QQuickItem; parenting it into the
    // content item gives it both its visual parent and QObject ownership.
    sceneRoot_ = new PreviewQuickSceneRoot(window_->contentItem());
    sceneRoot_->setZ(0.0);
    // Force the QSG chart-render path even when DComp is enabled globally
    // (--quick-shell-beta) — otherwise updatePaintNode short-circuits to nullptr
    // and the grab is blank. Same override the export session uses.
    sceneRoot_->setDCompFallbackActive(true);
    sceneRoot_->setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    sceneRoot_->setFrameState(&frameState_);

    window_->setOpacity(0.0);
    window_->show();
    warmedUp_ = false;
    return true;
}

QImage SceneFrameRenderer::renderAt(double seconds, int sidePx, QString* errorMessage)
{
    if (!ready_) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("scene frame renderer not bootstrapped");
        }
        return QImage();
    }
    const int side = qBound(16, sidePx, 4096);
    if (!ensureWindow(errorMessage)) {
        return QImage();
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

    settleEvents(!warmedUp_);

    QImage image = window_->grabWindow();
    if (image.isNull()) {
        settleEvents(true);
        image = window_->grabWindow();
    }
    if (image.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("grabWindow returned an empty image");
        }
        return QImage();   // leave warmedUp_ false so the next attempt cold-settles
    }
    warmedUp_ = true;      // latch warm only once a grab actually succeeded

    image.setDevicePixelRatio(1.0);
    if (image.width() != side || image.height() != side) {
        image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image.convertToFormat(QImage::Format_ARGB32);
}

}  // namespace miacode::cover_export
