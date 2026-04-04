#include "preview/runtime/PreviewRuntime.h"

#include "common/DebugOptions.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "preview/runtime/PreviewQuickRuntimeSurface.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QWindow>

namespace {

QString previewRuntimeProfilingPath()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(baseDir);
    return QDir(baseDir).filePath(QStringLiteral("preview_runtime_profile.txt"));
}

}  // namespace

PreviewRuntime::PreviewRuntime(QObject* parent)
    : QObject(parent)
{
    assets_ = new miacode::preview::runtime::PreviewSceneAssetRepository(this);
    refreshAssetStateFromRepository();
    presentedFrameIntervalsMs_.resize(120);
    presentedFrameIntervalsMs_.fill(0.0);
    surface_ = new PreviewQuickRuntimeSurface(this);
    surface_->setRuntime(this);

    connect(surface_, &PreviewQuickRuntimeSurface::framePresented, this, [this]() {
        updatePresentedFrameStats();
        pendingPresentedStatsRefresh_ = false;
        emit framePresented();
    });
    connect(assets_, &miacode::preview::runtime::PreviewSceneAssetRepository::assetsChanged, this, [this]() {
        refreshAssetStateFromRepository();
        update();
    });
}

PreviewRuntime::~PreviewRuntime() = default;

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
    pendingPresentedStatsRefresh_ = true;
    if (surface_ != nullptr) {
        surface_->requestFrame();
    }
}

void PreviewRuntime::setStageMediaAvailable(bool hasMedia)
{
    if (assets_ != nullptr) {
        assets_->setStageMediaAvailable(hasMedia);
    }
    frameState_.media.stageMediaAvailable = hasMedia;
    update();
}

void PreviewRuntime::setPlayheadSeconds(double seconds, bool requestUpdate)
{
    frameState_.playheadSeconds = qMax(0.0, seconds);
    if (requestUpdate) {
        update();
    }
}

void PreviewRuntime::setMediaFrame(const QImage& frame)
{
    frameState_.media.mediaFrame = frame;
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = QVideoFrame();
#endif
    frameState_.media.retainedVideoFallbackFrame = QImage();
    setStageMediaAvailable(!frame.isNull());
}

void PreviewRuntime::setVideoFrame(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    frameState_.media.videoFrame = frame;
    if (!frame.isValid()) {
        frameState_.media.retainedVideoFallbackFrame = QImage();
    }
    setStageMediaAvailable(frame.isValid());
#else
    Q_UNUSED(frame);
#endif
}

void PreviewRuntime::setRetainedVideoFallbackFrame(const QImage& frame)
{
    frameState_.media.retainedVideoFallbackFrame = frame;
    update();
}

void PreviewRuntime::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    frameState_.noteMarkers = notes;
    update();
}

void PreviewRuntime::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    frameState_.muriAnalysisReport = report;
    update();
}

void PreviewRuntime::setMuriRenderOptions(const MuriRenderOptions& options)
{
    frameState_.muriRenderOptions = options;
    update();
}

void PreviewRuntime::setSkinDirectory(const QString& skinDir)
{
    if (assets_ != nullptr) {
        assets_->setSkinDirectory(skinDir);
    }
}

void PreviewRuntime::setBackgroundBrightness(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setBackgroundBrightnessOuter(double brightness)
{
    frameState_.render.backgroundBrightnessOuter = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setBackgroundBrightnessInner(double brightness)
{
    frameState_.render.backgroundBrightnessInner = qBound(0.0, brightness, 1.0);
    update();
}

void PreviewRuntime::setLayoutSquareScale(double scale)
{
    frameState_.render.layoutSquareScale = miacode::preview_video::normalizedLayoutSquareScale(scale);
    update();
}

void PreviewRuntime::setSmoothBrightness(bool smooth)
{
    frameState_.render.smoothBrightness = smooth;
    update();
}

void PreviewRuntime::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    frameState_.render.backgroundScaleMode = mode;
    update();
}

void PreviewRuntime::setNoteFlowSpeed(double flowSpeed)
{
    frameState_.render.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    update();
}

void PreviewRuntime::setShowDebugInfo(bool show)
{
    frameState_.render.showDebugInfo = show;
    update();
}

void PreviewRuntime::setShowTimestamp(bool show)
{
    frameState_.render.showTimestamp = show;
    update();
}

void PreviewRuntime::setShowObjectStatsHud(bool show)
{
    frameState_.render.showObjectStatsHud = show;
    update();
}

bool PreviewRuntime::showTimestamp() const
{
    return frameState_.render.showTimestamp;
}

bool PreviewRuntime::showObjectStatsHud() const
{
    return frameState_.render.showObjectStatsHud;
}

void PreviewRuntime::reset()
{
    frameState_.noteMarkers.clear();
    frameState_.muriAnalysisReport = MuriAnalysisReport();
    frameState_.playheadSeconds = 0.0;
    frameState_.media = miacode::preview::scene::PreviewMediaFrameState();
    frameState_.fpsDisplay = 0.0;
    frameState_.cpuFallbackCount = 0;
    frameState_.usedGpuRendererThisFrame = false;
    if (assets_ != nullptr) {
        assets_->setStageMediaAvailable(false);
    }
    refreshAssetStateFromRepository();
    pendingPresentedStatsRefresh_ = true;
    update();
}

void PreviewRuntime::noteTickForProfiling()
{
}

void PreviewRuntime::resetProfilingSession()
{
    presentTimer_.invalidate();
    lastPresentedNs_ = -1;
    presentedFrameIntervalsMs_.fill(0.0);
    presentedFrameIntervalWriteIndex_ = 0;
    presentedFrameIntervalCount_ = 0;
    frameState_.fpsDisplay = 0.0;
}

QString PreviewRuntime::writeProfilingSummaryToFile()
{
    if (!miacode::debug_options::previewProfileOutputEnabled() || presentedFrameIntervalCount_ <= 0) {
        return QString();
    }

    QFile file(previewRuntimeProfilingPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString();
    }

    double sumMs = 0.0;
    double maxMs = 0.0;
    for (int index = 0; index < presentedFrameIntervalCount_; ++index) {
        const double sample = presentedFrameIntervalsMs_[index];
        sumMs += sample;
        maxMs = qMax(maxMs, sample);
    }
    const double avgMs = sumMs / static_cast<double>(presentedFrameIntervalCount_);

    QTextStream stream(&file);
    stream << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "frame_samples=" << presentedFrameIntervalCount_ << '\n';
    stream << "present_avg_ms=" << QString::number(avgMs, 'f', 4) << '\n';
    stream << "present_max_ms=" << QString::number(maxMs, 'f', 4) << '\n';
    stream << "fps=" << QString::number(frameState_.fpsDisplay, 'f', 4) << '\n';
    file.close();
    return file.fileName();
}

bool PreviewRuntime::hasCoreSkinAssetsLoadedForDebug() const
{
    return assets_ != nullptr && assets_->hasCoreSkinAssetsLoaded();
}

void PreviewRuntime::setFrameSize(const QSize& size)
{
    const QSize safeSize(qMax(1, size.width()), qMax(1, size.height()));
    if (frameSize_ == safeSize) {
        return;
    }
    frameSize_ = safeSize;
    pendingPresentedStatsRefresh_ = true;
    update();
}

void PreviewRuntime::refreshAssetStateFromRepository()
{
    if (assets_ == nullptr) {
        return;
    }
    frameState_.assets = assets_->assetState();
    frameState_.skin = assets_->skinAssets();
    frameState_.judgeOverlay = assets_->judgeOverlayAssets();
    frameState_.judgeEffect = assets_->judgeEffectAssets();
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
