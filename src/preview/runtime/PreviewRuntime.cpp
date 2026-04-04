#include "preview/runtime/PreviewRuntime.h"

#include "preview/runtime/PreviewQuickRuntimeSurface.h"

#include <QPainter>
#include <QWindow>

PreviewRuntime::PreviewRuntime(QObject* parent)
    : QObject(parent)
{
    renderer_ = new PreviewCanvas();
    frameState_.assets.outlineImage = renderer_->outlineImageForScene();
    frameState_.assets.layoutRingDiameterRatio = renderer_->layoutRingDiameterRatioForScene();
    frameState_.render.noteFlowSpeed = renderer_->noteFlowSpeed();
    frameState_.render.showTimestamp = renderer_->showTimestamp();
    frameState_.render.showObjectStatsHud = renderer_->showObjectStatsHud();
    presentedFrameIntervalsMs_.resize(120);
    presentedFrameIntervalsMs_.fill(0.0);
    surface_ = new PreviewQuickRuntimeSurface(this);
    surface_->setRuntime(this);

    connect(surface_, &PreviewQuickRuntimeSurface::framePresented, this, [this]() {
        updatePresentedFrameStats();
        emit framePresented();
    });
    connect(renderer_, &PreviewCanvas::skinAssetsChanged, this, [this]() {
        refreshAssetStateFromRenderer();
        update();
    });
}

PreviewRuntime::~PreviewRuntime()
{
    delete renderer_;
    renderer_ = nullptr;
}

QWindow* PreviewRuntime::hostWindow() const
{
    return surface_ != nullptr ? surface_->hostWindow() : nullptr;
}

void PreviewRuntime::requestActivate()
{
    if (surface_ != nullptr) {
        surface_->requestActivate();
    }
}

void PreviewRuntime::update()
{
    if (surface_ != nullptr) {
        surface_->requestFrame();
    }
}

void PreviewRuntime::setStageMediaAvailable(bool hasMedia)
{
    if (renderer_ != nullptr) {
        renderer_->setStageMediaAvailable(hasMedia);
    }
    frameState_.media.stageMediaAvailable = hasMedia;
    refreshAssetStateFromRenderer();
    update();
}

void PreviewRuntime::setPlayheadSeconds(double seconds, bool requestUpdate)
{
    if (renderer_ != nullptr) {
        renderer_->setPlayheadSeconds(seconds, false);
    }
    frameState_.playheadSeconds = qMax(0.0, seconds);
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setMediaFrame(const QImage& frame)
{
    if (renderer_ != nullptr) {
        renderer_->setMediaFrame(frame);
    }
    frameState_.media.mediaFrame = frame;
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = QVideoFrame();
#endif
    frameState_.media.retainedVideoFallbackFrame = QImage();
    update();
}

void PreviewRuntime::setVideoFrame(const QVideoFrame& frame)
{
    if (renderer_ != nullptr) {
        renderer_->setVideoFrame(frame);
    }
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = frame;
#endif
}

void PreviewRuntime::setRetainedVideoFallbackFrame(const QImage& frame)
{
    if (renderer_ != nullptr) {
        renderer_->setRetainedVideoFallbackFrame(frame);
    }
    frameState_.media.retainedVideoFallbackFrame = frame;
}

void PreviewRuntime::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    if (renderer_ != nullptr) {
        renderer_->setNoteMarkers(notes);
    }
    frameState_.noteMarkers = notes;
    update();
}

void PreviewRuntime::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    if (renderer_ != nullptr) {
        renderer_->setMuriAnalysisReport(report);
    }
    frameState_.muriAnalysisReport = report;
    update();
}

void PreviewRuntime::setMuriRenderOptions(const MuriRenderOptions& options)
{
    if (renderer_ != nullptr) {
        renderer_->setMuriRenderOptions(options);
    }
    frameState_.muriRenderOptions = options;
    update();
}

void PreviewRuntime::setSkinDirectory(const QString& skinDir)
{
    if (renderer_ != nullptr) {
        renderer_->setSkinDirectory(skinDir);
    }
}

void PreviewRuntime::setBackgroundBrightness(double brightness)
{
    if (renderer_ != nullptr) {
        renderer_->setBackgroundBrightness(brightness);
    }
    frameState_.render.backgroundBrightnessOuter = brightness;
    frameState_.render.backgroundBrightnessInner = brightness;
    update();
}

void PreviewRuntime::setBackgroundBrightnessOuter(double brightness)
{
    if (renderer_ != nullptr) {
        renderer_->setBackgroundBrightnessOuter(brightness);
    }
    frameState_.render.backgroundBrightnessOuter = brightness;
    update();
}

void PreviewRuntime::setBackgroundBrightnessInner(double brightness)
{
    if (renderer_ != nullptr) {
        renderer_->setBackgroundBrightnessInner(brightness);
    }
    frameState_.render.backgroundBrightnessInner = brightness;
    update();
}

void PreviewRuntime::setLayoutSquareScale(double scale)
{
    if (renderer_ != nullptr) {
        renderer_->setLayoutSquareScale(scale);
    }
    frameState_.render.layoutSquareScale = scale;
    update();
}

void PreviewRuntime::setSmoothBrightness(bool smooth)
{
    if (renderer_ != nullptr) {
        renderer_->setSmoothBrightness(smooth);
    }
    frameState_.render.smoothBrightness = smooth;
    update();
}

void PreviewRuntime::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    if (renderer_ != nullptr) {
        renderer_->setBackgroundScaleMode(mode);
    }
    frameState_.render.backgroundScaleMode = mode;
    update();
}

void PreviewRuntime::setNoteFlowSpeed(double flowSpeed)
{
    if (renderer_ != nullptr) {
        renderer_->setNoteFlowSpeed(flowSpeed);
    }
    frameState_.render.noteFlowSpeed = flowSpeed;
    update();
}

void PreviewRuntime::setShowDebugInfo(bool show)
{
    if (renderer_ != nullptr) {
        renderer_->setShowDebugInfo(show);
    }
    frameState_.render.showDebugInfo = show;
    update();
}

void PreviewRuntime::setShowTimestamp(bool show)
{
    if (renderer_ != nullptr) {
        renderer_->setShowTimestamp(show);
    }
    frameState_.render.showTimestamp = show;
    update();
}

void PreviewRuntime::setShowObjectStatsHud(bool show)
{
    if (renderer_ != nullptr) {
        renderer_->setShowObjectStatsHud(show);
    }
    frameState_.render.showObjectStatsHud = show;
    update();
}

bool PreviewRuntime::showTimestamp() const
{
    return renderer_ != nullptr ? renderer_->showTimestamp() : true;
}

bool PreviewRuntime::showObjectStatsHud() const
{
    return renderer_ != nullptr ? renderer_->showObjectStatsHud() : false;
}

void PreviewRuntime::reset()
{
    if (renderer_ != nullptr) {
        renderer_->reset();
    }
    frameState_ = miacode::preview::scene::PreviewFrameState();
    refreshAssetStateFromRenderer();
    update();
}

void PreviewRuntime::noteTickForProfiling()
{
    if (renderer_ != nullptr) {
        renderer_->noteTickForProfiling();
    }
}

void PreviewRuntime::resetProfilingSession()
{
    if (renderer_ != nullptr) {
        renderer_->resetProfilingSession();
    }
}

QString PreviewRuntime::writeProfilingSummaryToFile()
{
    return renderer_ != nullptr ? renderer_->writeProfilingSummaryToFile() : QString();
}

bool PreviewRuntime::hasCoreSkinAssetsLoadedForDebug() const
{
    return renderer_ != nullptr && renderer_->hasCoreSkinAssetsLoadedForDebug();
}

void PreviewRuntime::setFrameSize(const QSize& size)
{
    const QSize safeSize(qMax(1, size.width()), qMax(1, size.height()));
    if (frameSize_ == safeSize) {
        return;
    }
    frameSize_ = safeSize;
    update();
}

void PreviewRuntime::paintFrame(QPainter& painter, const QSize& outputSize, qreal devicePixelRatio)
{
    paintLegacyFrame(
        painter,
        outputSize,
        devicePixelRatio,
        miacode::preview::scene::kPreviewAllRenderLayers
    );
}

void PreviewRuntime::paintLegacyFrame(
    QPainter& painter,
    const QSize& outputSize,
    qreal devicePixelRatio,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags,
    bool allowGpuDrawing,
    bool collectFrameStats
)
{
    if (renderer_ == nullptr) {
        return;
    }
    renderer_->paintPresentFrame(
        painter,
        outputSize,
        devicePixelRatio,
        layerFlags,
        allowGpuDrawing,
        collectFrameStats
    );
}

QImage PreviewRuntime::renderLayerImage(
    const QSize& outputSize,
    qreal devicePixelRatio,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags
)
{
    const QSize safeSize(qMax(1, outputSize.width()), qMax(1, outputSize.height()));
    const QSize pixelSize(
        qMax(1, qRound(static_cast<qreal>(safeSize.width()) * qMax<qreal>(1.0, devicePixelRatio))),
        qMax(1, qRound(static_cast<qreal>(safeSize.height()) * qMax<qreal>(1.0, devicePixelRatio)))
    );
    QImage image(pixelSize, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(qMax<qreal>(1.0, devicePixelRatio));
    {
        QPainter painter(&image);
        renderer_->paintPresentFrame(painter, safeSize, devicePixelRatio, layerFlags, false, false);
    }
    return image;
}

void PreviewRuntime::refreshAssetStateFromRenderer()
{
    if (renderer_ == nullptr) {
        return;
    }
    frameState_.assets.outlineImage = renderer_->outlineImageForScene();
    frameState_.assets.layoutRingDiameterRatio = renderer_->layoutRingDiameterRatioForScene();
}

void PreviewRuntime::updatePresentedFrameStats()
{
    if (!presentTimer_.isValid()) {
        presentTimer_.start();
        lastPresentedNs_ = 0;
        frameState_.fpsDisplay = 0.0;
        frameState_.usedGpuRendererThisFrame = true;
        frameState_.cpuFallbackCount = 0;
        return;
    }

    const qint64 nowNs = presentTimer_.nsecsElapsed();
    if (lastPresentedNs_ >= 0) {
        const qint64 intervalNs = nowNs - lastPresentedNs_;
        const double intervalMs = static_cast<double>(intervalNs) / 1000000.0;
        if (!presentedFrameIntervalsMs_.isEmpty()) {
            presentedFrameIntervalsMs_[presentedFrameIntervalWriteIndex_] = intervalMs;
            presentedFrameIntervalWriteIndex_ =
                (presentedFrameIntervalWriteIndex_ + 1) % presentedFrameIntervalsMs_.size();
            presentedFrameIntervalCount_ = qMin(presentedFrameIntervalCount_ + 1, presentedFrameIntervalsMs_.size());
            double sumMs = 0.0;
            for (int index = 0; index < presentedFrameIntervalCount_; ++index) {
                sumMs += presentedFrameIntervalsMs_[index];
            }
            frameState_.fpsDisplay = sumMs > 1e-6
                ? 1000.0 / (sumMs / static_cast<double>(presentedFrameIntervalCount_))
                : 0.0;
        }
    }
    lastPresentedNs_ = nowNs;
    frameState_.usedGpuRendererThisFrame = true;
    frameState_.cpuFallbackCount = 0;
}
