#pragma once

#include <QJsonObject>
#include <QtGlobal>

enum class PreviewTimingOffsetUnit {
    Seconds,
    Frames60,
};

inline double previewTimingSecondsFromUnit(double value, PreviewTimingOffsetUnit unit)
{
    if (!qIsFinite(value)) {
        return 0.0;
    }
    return unit == PreviewTimingOffsetUnit::Frames60
        ? (value / 60.0)
        : value;
}

struct PreviewTimingSettings {
    double audioOffsetSeconds = 0.0;
    double displayOffsetSeconds = 0.0;
    double judgeOffsetSeconds = 0.0;
    double answerOffsetSeconds = 0.0;

    static double normalizeSeconds(double value)
    {
        return qIsFinite(value) ? value : 0.0;
    }

    void normalize()
    {
        audioOffsetSeconds = normalizeSeconds(audioOffsetSeconds);
        displayOffsetSeconds = normalizeSeconds(displayOffsetSeconds);
        judgeOffsetSeconds = normalizeSeconds(judgeOffsetSeconds);
        answerOffsetSeconds = normalizeSeconds(answerOffsetSeconds);
    }

    QJsonObject toJson() const
    {
        QJsonObject object;
        object.insert(QStringLiteral("audio_offset_seconds"), normalizeSeconds(audioOffsetSeconds));
        object.insert(QStringLiteral("display_offset_seconds"), normalizeSeconds(displayOffsetSeconds));
        object.insert(QStringLiteral("judge_offset_seconds"), normalizeSeconds(judgeOffsetSeconds));
        object.insert(QStringLiteral("answer_offset_seconds"), normalizeSeconds(answerOffsetSeconds));
        return object;
    }

    static PreviewTimingSettings fromJson(const QJsonObject& object)
    {
        PreviewTimingSettings settings;
        settings.audioOffsetSeconds =
            object.value(QStringLiteral("audio_offset_seconds")).toDouble(settings.audioOffsetSeconds);
        settings.displayOffsetSeconds =
            object.value(QStringLiteral("display_offset_seconds")).toDouble(settings.displayOffsetSeconds);
        settings.judgeOffsetSeconds =
            object.value(QStringLiteral("judge_offset_seconds")).toDouble(settings.judgeOffsetSeconds);
        settings.answerOffsetSeconds =
            object.value(QStringLiteral("answer_offset_seconds")).toDouble(settings.answerOffsetSeconds);
        settings.normalize();
        return settings;
    }
};

inline bool previewTimingSettingsEqual(
    const PreviewTimingSettings& a,
    const PreviewTimingSettings& b,
    double epsilon = 1e-9)
{
    return qAbs(a.audioOffsetSeconds - b.audioOffsetSeconds) <= epsilon
        && qAbs(a.displayOffsetSeconds - b.displayOffsetSeconds) <= epsilon
        && qAbs(a.judgeOffsetSeconds - b.judgeOffsetSeconds) <= epsilon
        && qAbs(a.answerOffsetSeconds - b.answerOffsetSeconds) <= epsilon;
}
