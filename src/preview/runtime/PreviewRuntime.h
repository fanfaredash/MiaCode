#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QSize>
#include <QVector>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"

class PreviewQuickRuntimeSurface;
class QWindow;
struct PreviewTextureStats;

struct PreviewRuntimeLayerProfileAggregate {
    QString name;
    qint64 spriteCountSum = 0;
    qint64 spriteBatchCountSum = 0;
    double buildMsSum = 0.0;
    qint64 spriteCountMax = 0;
    qint64 spriteBatchCountMax = 0;
    double buildMsMax = 0.0;
    qint64 spriteActiveFrameCount = 0;
};

class PreviewRuntime : public QObject
{
    Q_OBJECT

public:
    explicit PreviewRuntime(QObject* parent = nullptr);
    ~PreviewRuntime() override;

    QWindow* hostWindow() const;
    void requestActivate();
    void update();

    void setStageMediaAvailable(bool hasMedia);
    void setPlayheadSeconds(double seconds, bool requestUpdate = true);
    void setMediaFrame(const QImage& frame);
    void setVideoFrame(const QVideoFrame& frame);
    void setRetainedVideoFallbackFrame(const QImage& frame);
    void setNoteMarkers(const QVector<TimelineNoteMarker>& notes);
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    void setMuriRenderOptions(const MuriRenderOptions& options);
    void setSkinDirectory(const QString& skinDir);
    void setBackgroundBrightness(double brightness);
    void setBackgroundBrightnessOuter(double brightness);
    void setBackgroundBrightnessInner(double brightness);
    void setLayoutSquareScale(double scale);
    void setSmoothBrightness(bool smooth);
    void setBackgroundScaleMode(PreviewBackgroundScaleMode mode);
    void setNoteFlowSpeed(double flowSpeed);
    void setShowDebugInfo(bool show);
    void setShowTimestamp(bool show);
    void setShowObjectStatsHud(bool show);
    bool showTimestamp() const;
    bool showObjectStatsHud() const;

    void reset();
    void noteTickForProfiling();
    void resetProfilingSession();
    QString writeProfilingSummaryToFile();
    bool hasCoreSkinAssetsLoadedForDebug() const;

    void setFrameSize(const QSize& size);
    const miacode::preview::scene::PreviewFrameState& frameState() const { return frameState_; }

signals:
    void framePresented();

private:
    void refreshAssetStateFromRepository();
    void updatePresentedFrameStats();
    void updateTextureProfilingStats(const PreviewTextureStats& frameStats);

    miacode::preview::runtime::PreviewSceneAssetRepository* assets_ = nullptr;
    PreviewQuickRuntimeSurface* surface_ = nullptr;
    QSize frameSize_;
    miacode::preview::scene::PreviewFrameState frameState_;
    QElapsedTimer presentTimer_;
    qint64 lastPresentedNs_ = -1;
    QVector<double> presentedFrameIntervalsMs_;
    int presentedFrameIntervalWriteIndex_ = 0;
    int presentedFrameIntervalCount_ = 0;
    bool pendingPresentedStatsRefresh_ = true;
    bool profilingSummaryDirty_ = false;
    qint64 profiledTextureFrameCount_ = 0;
    qint64 profiledActiveSpriteFrameCount_ = 0;
    qint64 cachedTextureHitTotal_ = 0;
    qint64 cachedTextureCreateTotal_ = 0;
    qint64 transientTextureHitTotal_ = 0;
    qint64 transientTextureCreateTotal_ = 0;
    qint64 spriteCountTotal_ = 0;
    qint64 spriteBatchCountTotal_ = 0;
    qint64 spriteCountMax_ = 0;
    qint64 spriteBatchCountMax_ = 0;
    double layerBuildMsTotal_ = 0.0;
    double layerBuildMsMax_ = 0.0;
    qint64 peakFrameSpriteCount_ = 0;
    qint64 peakFrameSpriteBatchCount_ = 0;
    double peakFrameLayerBuildMs_ = 0.0;
    QVector<PreviewRuntimeLayerProfileAggregate> layerProfileAggregates_;
};
