#pragma once

#include <QtGlobal>

#include "PreviewTimingSettings.h"

namespace miacode::preview_sfx_timing {

constexpr double kFixedRealPreTriggerSeconds = 1.0 / 60.0;

inline double normalizedPlaybackRate(double playbackRate)
{
    if (!qIsFinite(playbackRate) || playbackRate <= 0.0) {
        return 1.0;
    }
    return playbackRate;
}

inline double chartDeltaForFixedRealSeconds(double realSeconds, double playbackRate)
{
    Q_UNUSED(playbackRate);
    return realSeconds;
}

inline double chartDomainSecond(
    double rawSecond,
    const PreviewTimingSettings& settings)
{
    return rawSecond + settings.audioOffsetSeconds;
}

inline double displayShiftedSecond(
    double rawSecond,
    const PreviewTimingSettings& settings,
    double playbackRate)
{
    return chartDomainSecond(rawSecond, settings)
        - chartDeltaForFixedRealSeconds(settings.displayOffsetSeconds, playbackRate);
}

inline double answerPreTriggerChartSeconds(double playbackRate)
{
    return chartDeltaForFixedRealSeconds(kFixedRealPreTriggerSeconds, playbackRate);
}

inline double answerTriggerSecond(
    double rawSecond,
    const PreviewTimingSettings& settings,
    double playbackRate)
{
    return chartDomainSecond(rawSecond, settings)
        + chartDeltaForFixedRealSeconds(settings.answerOffsetSeconds, playbackRate)
        - answerPreTriggerChartSeconds(playbackRate);
}

inline double judgeTriggerSecond(
    double rawSecond,
    const PreviewTimingSettings& settings,
    double playbackRate)
{
    return chartDomainSecond(rawSecond, settings)
        + chartDeltaForFixedRealSeconds(settings.judgeOffsetSeconds, playbackRate)
        - answerPreTriggerChartSeconds(playbackRate);
}

inline double slideTriggerSecond(
    double rawSecond,
    const PreviewTimingSettings& settings,
    double playbackRate)
{
    return displayShiftedSecond(rawSecond, settings, playbackRate);
}

}  // namespace miacode::preview_sfx_timing
