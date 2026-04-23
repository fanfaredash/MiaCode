#pragma once

#include <QString>
#include <QtGlobal>

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

inline bool previewSfxShouldInterruptPreviousKind(const QString& kind)
{
    const QString lowered = previewSfxNormalizedKind(kind);
    return previewSfxShouldAggregateKind(lowered)
        || lowered == "break_touch"
        || lowered == "break_slide";
}

inline double previewSfxPlaybackGainForAggregate(const QString& kind, int count, double maxGain)
{
    Q_UNUSED(kind);
    Q_UNUSED(count);
    return qMax(0.0, maxGain);
}
