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
};
