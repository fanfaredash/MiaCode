#pragma once

#include "common/PreviewSfxSemantics.h"

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

struct PreviewAudioSettings {
    double masterVolume = 1.0;
    double masterRestoreVolume = 1.0;
    double bgmVolume = 0.40;
    double bgmRestoreVolume = 0.40;
    double answerVolume = 0.30;
    double answerRestoreVolume = 0.30;
    double judgeVolume = 0.20;
    double judgeRestoreVolume = 0.20;
    double slideVolume = 0.10;
    double slideRestoreVolume = 0.10;
    double breakVolume = 0.10;
    double breakRestoreVolume = 0.10;
    double breakSlideVolume = 0.10;
    double breakSlideRestoreVolume = 0.10;
    double exVolume = 0.20;
    double exRestoreVolume = 0.20;
    double touchVolume = 0.20;
    double touchRestoreVolume = 0.20;
    double touchholdVolume = 0.20;
    double touchholdRestoreVolume = 0.20;
    double fireworkVolume = 0.20;
    double fireworkRestoreVolume = 0.20;

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

inline double previewSfxVolumeForKind(const PreviewAudioSettings& settings, const QString& kind)
{
    const double masterVolume = PreviewAudioSettings::clampMaster(settings.masterVolume);
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered == "answer") {
        return settings.answerVolume * masterVolume;
    }
    if (lowered == "judge") {
        return settings.judgeVolume * 0.5 * masterVolume;
    }
    if (lowered == "judge_break" || lowered == "break_touch") {
        return settings.breakVolume * 1.5 * masterVolume;
    }
    if (lowered == "slide") {
        return settings.slideVolume * 0.5 * masterVolume;
    }
    if (lowered == "break") {
        return settings.breakVolume * 1.5 * masterVolume;
    }
    if (lowered == "break_slide"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "judge_break_slide") {
        return settings.breakSlideVolume * 0.5 * masterVolume;
    }
    if (lowered == "ex") {
        return settings.exVolume * 0.5 * masterVolume;
    }
    if (lowered == "touch") {
        return settings.touchVolume * 0.5 * masterVolume;
    }
    if (lowered == "touchhold") {
        return settings.touchholdVolume * 0.5 * masterVolume;
    }
    if (lowered == "firework") {
        return settings.fireworkVolume * masterVolume;
    }
    return 0.0;
}

inline double previewBgmVolume(const PreviewAudioSettings& settings)
{
    return PreviewAudioSettings::clamp(settings.bgmVolume) * PreviewAudioSettings::clampMaster(settings.masterVolume);
}
