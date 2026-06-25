#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QFont>
#include <QSize>
#include <QVector>

#include <memory>

#include "common/TimelineThemeConfig.h"
#include "common/MuriTypes.h"
#include "common/WaveformCache.h"
#include "timeline/TimelineRenderData.h"
#include "timeline/TimelineSceneState.h"

class TimelineView;

class TimelineQuickStateBridge : public QObject
{
    Q_OBJECT
    // QML-bindable mirrors of the follow flags. Used by
    // BottomTabsQuickHost.qml's tab-strip checkboxes (the controls
    // that replaced the cramped in-timeline visuals); the legacy QSG
    // header rendering of the same toggles has been removed so we
    // no longer have two sources of truth for the on-screen state.
    Q_PROPERTY(bool followPreviewEnabled READ followPreviewEnabled
               WRITE setFollowPreviewEnabled NOTIFY followPreviewEnabledChanged)
    Q_PROPERTY(bool viewportLockEnabled READ viewportLockEnabled
               WRITE setViewportLockEnabled NOTIFY viewportLockEnabledChanged)
    Q_PROPERTY(bool followProgressEnabled READ followProgressEnabled
               WRITE setFollowProgressEnabled NOTIFY followProgressEnabledChanged)
    Q_PROPERTY(bool timelineSyncEnabled READ timelineSyncEnabled
               WRITE setTimelineSyncEnabled NOTIFY timelineSyncEnabledChanged)
    Q_PROPERTY(double waveformBrightness READ waveformBrightness
               WRITE setWaveformBrightness NOTIFY waveformBrightnessChanged)

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
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> muriMarkersByLocation() const;
    QHash<quint64, QString> muriMarkerTooltips() const;
    void setHeaderLineNumberFont(const QFont& font);
    QFont headerLineNumberFont() const;
    int horizontalScrollValue() const;
    void setHorizontalScrollValue(int value);
    double zoomScale() const;
    void setZoomScale(double scale);
    double contentScale() const;
    void setContentScale(double scale);
    double waveformBrightness() const;
    void setWaveformBrightness(double brightness);
    double waveformPhaseCompensationSeconds() const;
    void setWaveformPhaseCompensationSeconds(double seconds);
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
    bool viewportLockEnabled() const;
    void setViewportLockEnabled(bool enabled);
    bool followProgressEnabled() const;
    void setFollowProgressEnabled(bool enabled);
    bool timelineSyncEnabled() const;
    void setTimelineSyncEnabled(bool enabled);
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
    void zoomScaleChanged(double scale);
    // Dedicated NOTIFY signals for the Q_PROPERTYs above — keeping
    // these separate from renderStateChanged means the QML
    // checkbox bindings only re-evaluate when the toggle they
    // actually care about flips, not on every per-frame render
    // state push.
    void followPreviewEnabledChanged(bool enabled);
    void viewportLockEnabledChanged(bool enabled);
    void followProgressEnabledChanged(bool enabled);
    void timelineSyncEnabledChanged(bool enabled);
    void waveformBrightnessChanged(double brightness);

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
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> muriMarkersByLocation_;
    QHash<quint64, QString> muriMarkerTooltips_;
    QFont headerLineNumberFont_;
    QSize quickViewportSize_;
    QVector<double> zoomPresets_;
    int zoomPresetIndex_ = 1;
    int horizontalScrollValue_ = 0;
    double contentScale_ = 1.0;
    double waveformBrightness_ = miacode::timeline::kTimelineWaveformBrightnessDefault;
    double waveformPhaseCompensationSeconds_ = 0.0;
    double playbackEntrySeconds_ = 0.0;
    double playheadUpperLimitSeconds_ = -1.0;
    double playheadSeconds_ = 0.0;
    double cursorSeconds_ = 0.0;
    bool showSlideTracks_ = true;
    bool followPreviewEnabled_ = false;
    bool viewportLockEnabled_ = false;
    bool followProgressEnabled_ = true;
    bool timelineSyncEnabled_ = false;
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
