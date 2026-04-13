#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

struct PreviewAudioSettings {
    double masterVolume = 1.0;
    double masterRestoreVolume = 1.0;
    double bgmVolume = 0.25;
    double bgmRestoreVolume = 0.25;
    double answerVolume = 0.35;
    double answerRestoreVolume = 0.35;
    double judgeVolume = 0.15;
    double judgeRestoreVolume = 0.15;
    double slideVolume = 0.15;
    double slideRestoreVolume = 0.15;
    double breakVolume = 0.15;
    double breakRestoreVolume = 0.15;
    double breakSlideVolume = 0.15;
    double breakSlideRestoreVolume = 0.15;
    double exVolume = 0.15;
    double exRestoreVolume = 0.15;
    double touchVolume = 0.15;
    double touchRestoreVolume = 0.15;
    double touchholdVolume = 0.15;
    double touchholdRestoreVolume = 0.15;
    double fireworkVolume = 0.15;
    double fireworkRestoreVolume = 0.15;

    static double clamp(double value);
    static double clampMaster(double value);
    void normalize();

    int bgmPercent() const;
    int masterPercent() const;
    int answerPercent() const;
    int judgePercent() const;
    int slidePercent() const;
    int breakPercent() const;
    int breakSlidePercent() const;
    int exPercent() const;
    int touchPercent() const;
    int touchholdPercent() const;
    int fireworkPercent() const;
    void setBgmPercent(int value);
    void setMasterPercent(int value);
    void setAnswerPercent(int value);
    void setJudgePercent(int value);
    void setSlidePercent(int value);
    void setBreakPercent(int value);
    void setBreakSlidePercent(int value);
    void setExPercent(int value);
    void setTouchPercent(int value);
    void setTouchholdPercent(int value);
    void setFireworkPercent(int value);

    bool masterMuted() const;
    bool bgmMuted() const;
    bool answerMuted() const;
    bool judgeMuted() const;
    bool slideMuted() const;
    bool breakMuted() const;
    bool breakSlideMuted() const;
    bool exMuted() const;
    bool touchMuted() const;
    bool touchholdMuted() const;
    bool fireworkMuted() const;
    bool allNonBgmMuted() const;

    void toggleMasterMuted();
    void toggleBgmMuted();
    void toggleAnswerMuted();
    void toggleJudgeMuted();
    void toggleSlideMuted();
    void toggleBreakMuted();
    void toggleBreakSlideMuted();
    void toggleExMuted();
    void toggleTouchMuted();
    void toggleTouchholdMuted();
    void toggleFireworkMuted();
    void toggleAllNonBgmMuted();

    QJsonObject toJson() const;
    static PreviewAudioSettings fromJson(const QJsonObject& object);
};

constexpr double kPreviewSfxSameKindBoostGain = 1.5;
constexpr double kPreviewTouchholdMaxPlaybackCopies = 1.5;

inline QString previewSfxNormalizedKind(const QString& kind)
{
    return kind.trimmed().toLower();
}

inline bool previewSfxShouldAggregateKind(const QString& kind)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    return lowered == "answer"
        || lowered == "judge"
        || lowered == "judge_break"
        || lowered == "break"
        || lowered == "slide"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "judge_break_slide"
        || lowered == "ex"
        || lowered == "touch"
        || lowered == "firework";
}

inline bool previewSfxShouldBoostAggregatedKind(const QString& kind)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    return lowered == "judge"
        || lowered == "slide"
        || lowered == "ex";
}

inline bool previewSfxShouldInterruptPreviousKind(const QString& kind)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    return lowered == "judge_break"
        || lowered == "break"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "judge_break_slide"
        || lowered == "firework";
}

inline int previewSfxAggregatePlaybackCopies(const QString& kind, int count)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered == "touch") {
        // Touch clusters are capped so dense chords do not keep scaling indefinitely.
        return qBound(1, count, 2);
    }
    return 1;
}

inline double previewTouchholdAggregatePlaybackCopies(int count)
{
    return qBound(0.0, qMin(kPreviewTouchholdMaxPlaybackCopies, static_cast<double>(count)), kPreviewTouchholdMaxPlaybackCopies);
}

inline double previewSfxPlaybackGainForAggregate(const QString& kind, int count, double maxGain)
{
    double gain = qMax(0.0, maxGain);
    if (gain <= 0.0) {
        return 0.0;
    }

    const QString lowered = previewSfxNormalizedKind(kind);
    gain *= static_cast<double>(previewSfxAggregatePlaybackCopies(lowered, count));
    if (count >= 2 && previewSfxShouldBoostAggregatedKind(lowered)) {
        gain = qMin(kPreviewSfxSameKindBoostGain, gain * kPreviewSfxSameKindBoostGain);
    }
    return gain;
}

inline double previewSfxVolumeForKind(const PreviewAudioSettings& settings, const QString& kind)
{
    const double masterVolume = PreviewAudioSettings::clampMaster(settings.masterVolume);
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered == "answer") {
        return settings.answerVolume * masterVolume;
    }
    if (lowered == "judge") {
        return settings.judgeVolume * 0.25 * masterVolume;
    }
    if (lowered == "judge_break" || lowered == "break_touch") {
        return settings.breakVolume * 0.5 * masterVolume;
    }
    if (lowered == "slide") {
        return settings.slideVolume * 0.25 * masterVolume;
    }
    if (lowered == "break") {
        return settings.breakVolume * 0.5 * masterVolume;
    }
    if (lowered == "break_slide"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "judge_break_slide") {
        return settings.breakSlideVolume * 0.5 * masterVolume;
    }
    if (lowered == "ex") {
        return settings.exVolume * 0.25 * masterVolume;
    }
    if (lowered == "touch") {
        return settings.touchVolume * 0.25 * masterVolume;
    }
    if (lowered == "touchhold") {
        return settings.touchholdVolume * 0.25 * masterVolume;
    }
    if (lowered == "firework") {
        return settings.fireworkVolume * 0.5 * masterVolume;
    }
    return 0.0;
}

inline double previewBgmVolume(const PreviewAudioSettings& settings)
{
    return PreviewAudioSettings::clamp(settings.bgmVolume) * PreviewAudioSettings::clampMaster(settings.masterVolume);
}
