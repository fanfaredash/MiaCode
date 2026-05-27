#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "timeline/TimelineData.h"
#include "timeline/TimelineRenderData.h"

class MainWindow;
class QTimer;

namespace miacode::latency {

// Drives the latency-detection page's "sandbox audition" — synthesizes
// a long stream of plain tap notes at the user's BPM/Offset/subdivision,
// hot-swaps the timeline + SFX engine to play it, and restores the
// previous chart timeline + audio settings on exit.
//
// Lives as a member of MainWindow; the page UI talks to this controller
// rather than the engine internals directly.
class LatencySandboxController : public QObject
{
    Q_OBJECT

public:
    explicit LatencySandboxController(MainWindow* owner, QObject* parent = nullptr);
    ~LatencySandboxController() override;

    // Whether the user currently has the latency page selected. Set by
    // the page's enter/exit hooks. Used so the global Space shortcut
    // routes to toggleAudition() instead of the main preview transport.
    bool isOnPage() const { return onPage_; }
    void setOnPage(bool onPage);

    bool isAuditionRunning() const { return auditionRunning_; }

    double bpm() const { return bpm_; }
    double offsetSeconds() const { return offsetSeconds_; }
    int subdivision() const { return subdivision_; }
    int sfxVolumePercent() const { return sfxVolumePercent_; }
    double playheadSeconds() const;

    // Parameters that drive synthesis. Updates while audition is running
    // hot-swap the test chart (without restarting playback).
    void setBpm(double bpm);
    void setOffsetSeconds(double seconds);
    void setSubdivision(int subdivision);  // 4 or 8; other values clamped to 4
    void setSfxVolumePercent(int percent);

    // Audition lifecycle. startAudition() returns false if the
    // controller can't begin (no SFX runtime, etc.). stopAudition() and
    // exitIfActive() are idempotent. toggleAudition() flips state.
    bool startAudition();
    void stopAudition();
    void toggleAudition();
    void exitIfActive();

signals:
    void auditionStateChanged(bool running);
    void parametersChanged();
    void playheadAdvanced(double seconds);

private:
    void onTick();
    void regenerateAndPushIfRunning();
    void pushSyntheticTimeline();
    void restoreOriginalTimeline();
    void applyOverrideAudioSettings();
    void restoreOriginalAudioSettings();
    PreviewAudioSettings buildOverrideAudioSettings(const PreviewAudioSettings& base, int sfxPercent) const;
    TimelineRenderSnapshot buildSyntheticSnapshot(double durationSeconds) const;
    double resolveAudioDurationSeconds() const;

    QPointer<MainWindow> owner_;
    QTimer* tickTimer_ = nullptr;

    bool onPage_ = false;
    bool auditionRunning_ = false;

    double bpm_ = 120.0;
    double offsetSeconds_ = 0.0;
    int subdivision_ = 4;
    int sfxVolumePercent_ = 80;

    // Captured-on-startAudition state used to restore after stopAudition.
    bool hasCachedTimeline_ = false;
    QVector<TimelineNoteMarker> cachedNoteMarkers_;
    QByteArray cachedNoteMarkerSignature_;
    TimelineRenderSnapshot cachedSnapshot_;
    bool hasCachedAudioSettings_ = false;
    PreviewAudioSettings cachedAudioSettings_;

    double lastSentPlayheadSeconds_ = -1.0;
};

}  // namespace miacode::latency
