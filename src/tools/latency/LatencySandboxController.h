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

// Drives the latency-detection page's "sandbox audition".
//
// Design (per the page's intended behavior): the latency page plays exactly
// like a difficulty's chart page — the ONLY differences are that the chart is a
// synthesized test chart (`(BPM){n}1,1,1,...`), it is not editable, and it is
// not shown in an editor. To get that parity, this controller does NOT run its
// own playback loop. Instead it:
//   * installs the test chart as the preview source (note markers + bottom
//     timeline + SFX timeline + "snapshot ready" flags) — the same state a
//     slow-refresh produces for a real difficulty;
//   * marks `latencySandboxAuditionActive_` so the playback gates treat it as a
//     previewable chart (see TimelineSection::hasPreviewableChart);
//   * routes Play/Pause to the real transport (MainWindow::onTogglePreviewPause
//     / startQtPreviewPlayback / pauseQtPreviewPlaybackExact), which then drives
//     the preview render, bottom timeline, slider, SFX, and song audio
//     identically to a difficulty;
//   * restores the previous chart's preview state on exit.
//
// A lightweight poll timer mirrors the real playback state (playing? / current
// second) onto the latency page's own widgets (audition button + position
// label) via the signals below.
//
// Lives as a member of MainWindow (a friend), so it can reuse MainWindow's
// preview/timeline/transport helpers directly.
class LatencySandboxController : public QObject
{
    Q_OBJECT

public:
    explicit LatencySandboxController(MainWindow* owner, QObject* parent = nullptr);
    ~LatencySandboxController() override;

    // Whether the user currently has the latency page selected. Set by the
    // page's enter/exit hooks; installs/restores the test-chart preview source.
    bool isOnPage() const { return onPage_; }
    void setOnPage(bool onPage);

    // Mirrors the real transport's playing state (updated by the poll timer).
    bool isAuditionRunning() const { return auditionRunning_; }

    double bpm() const { return bpm_; }
    double offsetSeconds() const { return offsetSeconds_; }
    int subdivision() const { return subdivision_; }
    int sfxVolumePercent() const { return sfxVolumePercent_; }
    double playheadSeconds() const;

    // Parameters that drive synthesis. While on the page (and not actively
    // playing) these rebuild the installed test chart.
    void setBpm(double bpm);
    void setOffsetSeconds(double seconds);
    void setSubdivision(int subdivision);  // 4 or 8; other values clamped to 4
    void setSfxVolumePercent(int percent);

    // Play/Pause toggle for the page button — delegates to the real transport.
    void toggleAudition();
    // Stop any in-progress audition playback (used on file-path change). Only
    // acts while on the latency page; never touches normal-difficulty playback.
    void exitIfActive();

signals:
    void auditionStateChanged(bool running);
    void parametersChanged();
    void playheadAdvanced(double seconds);

private:
    void onTick();                 // UI poll: mirror real playback state → page
    void installSandboxScene();    // cache real chart, install the test chart
    void teardownSandboxScene();   // stop playback + restore the real chart
    void regenerateAndPushIfActive();
    void applyPlayheadToScene(double seconds);
    void setupSandboxPreviewState();  // build test chart → preview/timeline/SFX
    void restoreOriginalTimeline();
    double resolveAudioDurationSeconds() const;

    QPointer<MainWindow> owner_;
    QTimer* tickTimer_ = nullptr;

    bool onPage_ = false;
    bool auditionRunning_ = false;   // cached mirror of state_.qtPreviewPlaying_
    double lastPolledSecond_ = -1.0;

    double bpm_ = 120.0;
    double offsetSeconds_ = 0.0;
    int subdivision_ = 4;
    int sfxVolumePercent_ = 80;

    // Captured-on-enter state, restored when leaving the page.
    bool hasCachedTimeline_ = false;
    QVector<TimelineNoteMarker> cachedNoteMarkers_;
    QByteArray cachedNoteMarkerSignature_;
    TimelineRenderSnapshot cachedSnapshot_;
};

}  // namespace miacode::latency
