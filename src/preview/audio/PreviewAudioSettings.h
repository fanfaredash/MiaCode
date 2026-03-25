#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

struct PreviewAudioSettings {
    double bgmVolume = 0.25;
    double answerVolume = 0.35;
    double judgeVolume = 0.15;
    double slideVolume = 0.15;
    double breakVolume = 0.15;
    double breakSlideVolume = 0.15;
    double exVolume = 0.15;
    double touchVolume = 0.15;
    double touchholdVolume = 0.15;
    double fireworkVolume = 0.15;

    static double clamp(double value);
    void normalize();

    int bgmPercent() const;
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
    void setAnswerPercent(int value);
    void setJudgePercent(int value);
    void setSlidePercent(int value);
    void setBreakPercent(int value);
    void setBreakSlidePercent(int value);
    void setExPercent(int value);
    void setTouchPercent(int value);
    void setTouchholdPercent(int value);
    void setFireworkPercent(int value);

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
    const QString lowered = previewSfxNormalizedKind(kind);
    if (lowered == "answer") {
        return settings.answerVolume;
    }
    if (lowered == "judge") {
        return settings.judgeVolume * 0.25;
    }
    if (lowered == "judge_break" || lowered == "break_touch") {
        return settings.breakVolume * 0.5;
    }
    if (lowered == "slide") {
        return settings.slideVolume * 0.25;
    }
    if (lowered == "break") {
        return settings.breakVolume * 0.5;
    }
    if (lowered == "break_slide"
        || lowered == "break_slide_start"
        || lowered == "break_slide_finish"
        || lowered == "judge_break_slide") {
        return settings.breakSlideVolume * 0.5;
    }
    if (lowered == "ex") {
        return settings.exVolume * 0.25;
    }
    if (lowered == "touch" || lowered == "touchhold") {
        return settings.touchVolume * 0.25;
    }
    if (lowered == "firework") {
        return settings.fireworkVolume * 0.5;
    }
    return 0.0;
}
