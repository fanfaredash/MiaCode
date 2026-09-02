#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QFont>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include <memory>

#include "common/TimelineThemeConfig.h"
#include "common/MuriTypes.h"
#include "common/WaveformCache.h"
#include "timeline/TimelineRenderData.h"
#include "timeline/TimelineSceneState.h"

class TimelineQuickStateBridge : public QObject
{
    Q_OBJECT
    // QML-bindable mirrors of the follow flags. BottomTabBar.qml exposes
    // Follow Code through AppCheckBox; fixed follow states remain
    // available to the timeline pipeline through this bridge.
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
    Q_PROPERTY(double measureLineBrightness READ measureLineBrightness
               WRITE setMeasureLineBrightness NOTIFY measureLineBrightnessChanged)
    Q_PROPERTY(double zoomScale READ zoomScale NOTIFY zoomScaleChanged)
    Q_PROPERTY(QVariantList zoomPresetValues READ zoomPresetValues CONSTANT)

public:
    explicit TimelineQuickStateBridge(QObject* parent = nullptr, const QFont& font = QFont());

    void setQuickViewportSize(const QSize& viewportSize);
    int timelineTop() const;

    void clear();
    void setTimelineData(const TimelineRenderSnapshot& snapshot);
    const TimelineRenderSnapshot& renderSnapshot() const;
    void setWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData() const;
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> muriMarkersByLocation() const;
    QHash<quint64, QString> muriMarkerTooltips() const;
    QFont headerLineNumberFont() const;
    QString skinDirectory() const;
    void setSkinDirectory(const QString& skinDirectory);
    // Sub-pixel. `maxHorizontalScrollValue()` stays an int because it is a content extent,
    // not a position.
    double horizontalScrollValue() const;
    void setHorizontalScrollValue(double value);
    double zoomScale() const;
    void setZoomScale(double scale);
    QVector<double> zoomPresets() const;
    QVariantList zoomPresetValues() const;
    Q_INVOKABLE void applyZoomPreset(double scale);
    QStringList zoomInWheelShortcuts() const;
    QStringList zoomOutWheelShortcuts() const;
    void setZoomWheelShortcuts(const QStringList& zoomInShortcuts, const QStringList& zoomOutShortcuts);
    double viewportCenterSecond();
    QSize viewportSize() const;
    void setZoomScaleAnchored(double scale, double anchorSecond);
    double contentScale() const;
    void setContentScale(double scale);
    double waveformBrightness() const;
    void setWaveformBrightness(double brightness);
    double measureLineBrightness() const;
    void setMeasureLineBrightness(double brightness);
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
    // Relay for TimelineQuickItem's per-frame afterAnimating hook. The item owns the window
    // binding; the bridge is what MainWindow already holds, so the tick is republished here
    // rather than making MainWindow reach for a QML-instantiated item pointer.
    void notifyRenderCadenceTick();
    // True while preview playback is driving the timeline. The item uses this to keep the
    // render loop spinning (QQuickWindow::update) so afterAnimating keeps arriving; without
    // it the cadence would depend on whether an update() issued during afterAnimating also
    // schedules a follow-up frame, which is a Qt render-loop internal we should not bet on.
    void setPlaybackCadenceActive(bool active);
    bool playbackCadenceActive() const;
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
    quint64 layoutRevision() const { return layoutRevision_; }
    quint64 waveformRevision() const { return waveformRevision_; }
    quint64 headerRevision() const { return headerRevision_; }
    quint64 notesRevision() const { return notesRevision_; }
    quint64 overlayRevision() const { return overlayRevision_; }
    quint64 overlayDynamicRevision() const { return overlayDynamicRevision_; }

signals:
    void renderStateChanged();
    void playheadChanged(double second);
    void zoomScaleChanged(double scale);
    // Emitted once per timeline frame from TimelineQuickItem's afterAnimating hook, on the
    // GUI thread, immediately before the render thread syncs that frame. Playback samples
    // its clock here instead of on a free-running timer so the sampled second and the frame
    // it lands in are phase-locked; see notifyRenderCadenceTick().
    void renderCadenceTick();
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
    void measureLineBrightnessChanged(double brightness);

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

    TimelineRenderSnapshot snapshot_;
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData_;
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> muriMarkersByLocation_;
    QHash<quint64, QString> muriMarkerTooltips_;
    QFont headerLineNumberFont_;
    QString skinDirectory_;
    QSize quickViewportSize_;
    QVector<double> zoomPresets_;
    QStringList zoomInWheelShortcuts_{QStringLiteral("Ctrl+WheelUp")};
    QStringList zoomOutWheelShortcuts_{QStringLiteral("Ctrl+WheelDown")};
    int zoomPresetIndex_ = 1;
    double horizontalScrollValue_ = 0.0;
    bool playbackCadenceActive_ = false;
    double contentScale_ = 1.0;
    double waveformBrightness_ = miacode::timeline::kTimelineWaveformBrightnessDefault;
    double measureLineBrightness_ = miacode::timeline::kTimelineMeasureLineBrightnessDefault;
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
    quint64 layoutRevision_ = 1;
    quint64 gridRevision_ = 1;
    quint64 waveformRevision_ = 1;
    quint64 headerRevision_ = 1;
    quint64 notesRevision_ = 1;
    quint64 overlayRevision_ = 1;
    quint64 overlayDynamicRevision_ = 1;
};
