#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QFont>
#include <QSize>
#include <QSet>
#include <QVector>

#include <memory>

#include "common/MuriTypes.h"
#include "common/WaveformCache.h"
#include "timeline/TimelineRenderData.h"
#include "timeline/TimelineSceneState.h"

class TimelineView;

class TimelineQuickStateBridge : public QObject
{
    Q_OBJECT

public:
    explicit TimelineQuickStateBridge(QObject* parent = nullptr);

    // Reference-only mirror attach for the classic QWidget timeline path.
    void attachReferenceView(TimelineView* referenceView);
    void setQuickViewportSize(const QSize& viewportSize);

    void clear();
    void setTimelineData(const TimelineRenderSnapshot& snapshot);
    const TimelineRenderSnapshot& renderSnapshot() const;
    void setWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData() const;
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    QSet<quint64> muriMarkerLocationIds() const;
    QHash<quint64, QString> muriMarkerTooltips() const;
    void setHeaderLineNumberFont(const QFont& font);
    QFont headerLineNumberFont() const;
    int horizontalScrollValue() const;
    void setHorizontalScrollValue(int value);
    double zoomScale() const;
    void cycleZoomPreset(double anchorSecond);
    void stepZoomPreset(int deltaSteps, double anchorSecond);
    void setPlaybackEntrySeconds(double second);
    double playbackEntrySeconds() const;
    void setPlayheadUpperLimitSeconds(double second);
    double playheadUpperLimitSeconds() const;
    double durationSeconds() const;
    double playheadSeconds() const;
    void setPlayheadSeconds(double second, bool centerView);
    double cursorSeconds() const;
    void setCursorSeconds(double second, bool centerView);
    void focusPlayhead(bool centerView);
    void focusCursor(bool centerView);
    bool showSlideTracks() const;
    void setShowSlideTracks(bool show);
    bool followPreviewEnabled() const;
    void setFollowPreviewEnabled(bool enabled);
    bool playheadIndicatorSuppressed() const;
    void suppressPlayheadIndicator();
    void restorePlayheadIndicator(bool immediate = false);

    quint64 gridRevision() const { return gridRevision_; }
    quint64 waveformRevision() const { return waveformRevision_; }
    quint64 headerRevision() const { return headerRevision_; }
    quint64 notesRevision() const { return notesRevision_; }
    quint64 overlayRevision() const { return overlayRevision_; }
    quint64 overlayDynamicRevision() const { return overlayDynamicRevision_; }

signals:
    void renderStateChanged();
    void playheadChanged(double second);

private:
    QSize effectiveViewportSize() const;
    void refreshLayoutMetrics();
    int maxHorizontalScrollValue() const;
    bool centerOnSecond(double second);
    void bumpAllRevisions();
    void bumpHeaderRevision();
    void bumpNotesRevision();
    void bumpOverlayRevision();
    void bumpOverlayDynamicRevision();

    QPointer<TimelineView> referenceView_;
    TimelineRenderSnapshot snapshot_;
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData_;
    QSet<quint64> muriMarkerLocationIds_;
    QHash<quint64, QString> muriMarkerTooltips_;
    QFont headerLineNumberFont_;
    QSize quickViewportSize_;
    QVector<double> zoomPresets_;
    int zoomPresetIndex_ = 1;
    int horizontalScrollValue_ = 0;
    double playbackEntrySeconds_ = 0.0;
    double playheadUpperLimitSeconds_ = -1.0;
    double playheadSeconds_ = 0.0;
    double cursorSeconds_ = 0.0;
    bool showSlideTracks_ = true;
    bool followPreviewEnabled_ = false;
    bool playheadIndicatorSuppressed_ = false;
    miacode::timeline::TimelineSceneLayoutMetrics layoutMetrics_;
    bool layoutMetricsValid_ = false;
    quint64 gridRevision_ = 1;
    quint64 waveformRevision_ = 1;
    quint64 headerRevision_ = 1;
    quint64 notesRevision_ = 1;
    quint64 overlayRevision_ = 1;
    quint64 overlayDynamicRevision_ = 1;
};
