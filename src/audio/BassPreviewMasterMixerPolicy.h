#pragma once

#include <QString>
#include <QtGlobal>

namespace miacode::preview_audio::bass {

inline constexpr double kDefaultMasterMixerBufferMs = 30.0;
inline constexpr int kDefaultMasterMixerThreadCount = 1;
inline constexpr double kMaxMasterMixerBufferMs = 500.0;
inline constexpr int kMaxMasterMixerThreadCount = 16;

struct MasterMixerPolicy {
    double bufferMs = kDefaultMasterMixerBufferMs;
    int threadCount = kDefaultMasterMixerThreadCount;
    bool bufferOverrideSet = false;
    bool bufferOverrideValid = true;
    bool threadOverrideSet = false;
    bool threadOverrideValid = true;
};

inline MasterMixerPolicy masterMixerPolicyFromOverrides(
    const QString& rawBufferMs,
    const QString& rawThreadCount)
{
    MasterMixerPolicy policy;
    const QString bufferText = rawBufferMs.trimmed();
    if (!bufferText.isEmpty()) {
        policy.bufferOverrideSet = true;
        bool ok = false;
        const double bufferMs = bufferText.toDouble(&ok);
        policy.bufferOverrideValid = ok && qIsFinite(bufferMs)
            && bufferMs >= 0.0 && bufferMs <= kMaxMasterMixerBufferMs;
        if (policy.bufferOverrideValid) {
            policy.bufferMs = bufferMs;
        }
    }

    const QString threadText = rawThreadCount.trimmed();
    if (!threadText.isEmpty()) {
        policy.threadOverrideSet = true;
        bool ok = false;
        const int threadCount = threadText.toInt(&ok);
        policy.threadOverrideValid = ok
            && threadCount >= 1 && threadCount <= kMaxMasterMixerThreadCount;
        if (policy.threadOverrideValid) {
            policy.threadCount = threadCount;
        }
    }
    return policy;
}

inline double validOutputBufferSeconds(double seconds)
{
    return qIsFinite(seconds) && seconds >= 0.0 ? seconds : 0.0;
}

}  // namespace miacode::preview_audio::bass
