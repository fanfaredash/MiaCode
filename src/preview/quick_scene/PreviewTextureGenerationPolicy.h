#pragma once

#include <QtGlobal>

namespace miacode::preview::quick_scene {

constexpr bool previewTextureGenerationResetRequired(
    bool flushPending,
    qsizetype cachedTextureCount,
    qint64 cachedTextureBytes,
    qsizetype cachedFastKeyCount,
    qsizetype cachedTextureEntryLimit,
    qint64 cachedTextureByteLimit,
    qsizetype cachedFastKeyEntryLimit
)
{
    return flushPending
        || cachedTextureCount > cachedTextureEntryLimit
        || cachedTextureBytes > cachedTextureByteLimit
        || cachedFastKeyCount > cachedFastKeyEntryLimit;
}

}  // namespace miacode::preview::quick_scene
