#pragma once

#include "common/PreviewSfxSemantics.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

struct PreviewAudioSettings {
    double globalVolume = 0.30;
    double globalRestoreVolume = 0.30;
    double trackVolume = 1.0;
    double trackRestoreVolume = 1.0;
    double answerVolume = 0.80;
    double answerRestoreVolume = 0.80;
    double tapVolume = 0.30;
    double tapRestoreVolume = 0.30;
    double exVolume = 0.30;
    double exRestoreVolume = 0.30;
    double breakVolume = 0.30;
    double breakRestoreVolume = 0.30;
    double breakSlideVolume = 0.30;
    double breakSlideRestoreVolume = 0.30;
    double slideVolume = 0.30;
    double slideRestoreVolume = 0.30;
    double touchVolume = 0.30;
    double touchRestoreVolume = 0.30;
    double fireworkVolume = 0.30;
    double fireworkRestoreVolume = 0.30;
    bool breakSlideTailCheerMuted = false;

    static double clamp(double value);
    static double clampGlobal(double value);
    void normalize();

    int globalPercent() const;
    int trackPercent() const;
    int answerPercent() const;
    int tapPercent() const;
    int exPercent() const;
    int breakPercent() const;
    int breakSlidePercent() const;
    int slidePercent() const;
    int touchPercent() const;
    int fireworkPercent() const;
    void setGlobalPercent(int value);
    void setTrackPercent(int value);
    void setAnswerPercent(int value);
    void setTapPercent(int value);
    void setExPercent(int value);
    void setBreakPercent(int value);
    void setBreakSlidePercent(int value);
    void setSlidePercent(int value);
    void setTouchPercent(int value);
    void setFireworkPercent(int value);

    bool globalMuted() const;
    bool trackMuted() const;
    bool answerMuted() const;
    bool tapMuted() const;
    bool exMuted() const;
    bool breakMuted() const;
    bool breakSlideMuted() const;
    bool slideMuted() const;
    bool touchMuted() const;
    bool fireworkMuted() const;
    bool allNonTrackMuted() const;

    void toggleGlobalMuted();
    void toggleTrackMuted();
    void toggleAnswerMuted();
    void toggleTapMuted();
    void toggleExMuted();
    void toggleBreakMuted();
    void toggleBreakSlideMuted();
    void toggleSlideMuted();
    void toggleTouchMuted();
    void toggleFireworkMuted();
    void toggleAllNonTrackMuted();

    QJsonObject toJson() const;
    static PreviewAudioSettings fromJson(const QJsonObject& object);
};

inline double previewSfxVolumeForKind(const PreviewAudioSettings& settings, const QString& kind)
{
    const double globalVolume = PreviewAudioSettings::clampGlobal(settings.globalVolume);
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered == "answer") {
        return settings.answerVolume * globalVolume;
    }
    if (lowered == "judge") {
        return settings.tapVolume * globalVolume;
    }
    if (lowered == "judge_break"
        || lowered == "break_touch"
        || lowered == "break") {
        return settings.breakVolume * globalVolume;
    }
    if (lowered == "break_slide"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "break_slide_tail_break"
        || lowered == "judge_break_slide") {
        return settings.breakSlideVolume * globalVolume;
    }
    if (lowered == "slide") {
        return settings.slideVolume * globalVolume;
    }
    if (lowered == "ex") {
        return settings.exVolume * globalVolume;
    }
    if (lowered == "touch" || lowered == "touchhold") {
        return settings.touchVolume * globalVolume;
    }
    if (lowered == "firework") {
        return settings.fireworkVolume * globalVolume;
    }
    if (lowered == "clock") {
        return settings.answerVolume * globalVolume;
    }
    if (lowered == "track_start") {
        // 片头 opening jingle (preview-only affordance). Shares the answer bucket
        // like clock so it tracks a real, audible level the user can tune.
        return settings.answerVolume * globalVolume;
    }
    return 0.0;
}

inline double previewTrackVolume(const PreviewAudioSettings& settings)
{
    return PreviewAudioSettings::clamp(settings.trackVolume)
        * PreviewAudioSettings::clampGlobal(settings.globalVolume);
}

// Derive the latency-page audition levels from the user's normal preview mix.
// Pure function: every SFX kind plays at `sfxPercent` (independent of the user's
// per-kind SFX volumes), while the song keeps its normal *effective* volume
// (global*track folded into trackVolume, globalVolume pinned to 1.0). This is the
// LatencyAudition half of the single level-dispatch entry
// (MainWindow::applyPreviewAudioSettingsToRuntime); there is no snapshot/restore.
PreviewAudioSettings makePreviewLatencyAuditionLevels(const PreviewAudioSettings& mix, int sfxPercent);
