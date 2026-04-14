#pragma once

#include <QHash>
#include <QVector>
#include <QtGlobal>

namespace miacode::preview::quick_scene {

using SpriteFamilyKey = quint64;

struct PreviewQuickOrderedRunRange {
    SpriteFamilyKey familyKey = 0;
    int startSpriteIndex = 0;
    int spriteCount = 0;
};

inline constexpr int kPreviewQuickSpriteVertexCapacityStep = 96;
inline constexpr int kPreviewQuickSpriteShrinkFrameThreshold = 60;

inline int previewQuickSpriteAlignedVertexCapacity(int requiredVertexCount)
{
    if (requiredVertexCount <= 0) {
        return 0;
    }
    const int steps =
        (requiredVertexCount + kPreviewQuickSpriteVertexCapacityStep - 1) / kPreviewQuickSpriteVertexCapacityStep;
    return qMax(kPreviewQuickSpriteVertexCapacityStep, steps * kPreviewQuickSpriteVertexCapacityStep);
}

inline int previewQuickSpriteInitialReserveHint(
    const QHash<SpriteFamilyKey, int>& reserveHints,
    SpriteFamilyKey familyKey
)
{
    return qMax(kPreviewQuickSpriteVertexCapacityStep, reserveHints.value(familyKey, 0));
}

inline int previewQuickSpriteNextUnderuseFrameCount(
    int requiredVertexCount,
    int allocatedVertexCapacity,
    int currentUnderuseFrameCount
)
{
    if (requiredVertexCount <= 0 || allocatedVertexCapacity <= 0) {
        return 0;
    }
    return requiredVertexCount * 2 <= allocatedVertexCapacity
        ? currentUnderuseFrameCount + 1
        : 0;
}

inline bool previewQuickSpriteShouldShrink(
    int requiredVertexCount,
    int allocatedVertexCapacity,
    int underuseFrameCount
)
{
    if (requiredVertexCount <= 0 || allocatedVertexCapacity <= 0) {
        return false;
    }
    if (underuseFrameCount < kPreviewQuickSpriteShrinkFrameThreshold) {
        return false;
    }
    const int targetCapacity = previewQuickSpriteAlignedVertexCapacity(requiredVertexCount);
    return requiredVertexCount * 2 <= allocatedVertexCapacity
        && targetCapacity > 0
        && targetCapacity < allocatedVertexCapacity;
}

inline QVector<PreviewQuickOrderedRunRange> buildPreviewQuickOrderedRunRanges(
    const QVector<SpriteFamilyKey>& familyKeys
)
{
    QVector<PreviewQuickOrderedRunRange> runs;
    if (familyKeys.isEmpty()) {
        return runs;
    }

    runs.reserve(familyKeys.size());
    for (int spriteIndex = 0; spriteIndex < familyKeys.size(); ++spriteIndex) {
        const SpriteFamilyKey familyKey = familyKeys.at(spriteIndex);
        if (runs.isEmpty() || runs.last().familyKey != familyKey) {
            runs.append(PreviewQuickOrderedRunRange{familyKey, spriteIndex, 1});
        } else {
            runs.last().spriteCount += 1;
        }
    }
    return runs;
}

template <typename VertexT>
inline void fillPreviewQuickDegenerateVertexTail(
    VertexT* vertices,
    int requiredVertexCount,
    int totalVertexCount,
    const VertexT& filler
)
{
    if (vertices == nullptr || requiredVertexCount >= totalVertexCount) {
        return;
    }
    for (int vertexIndex = requiredVertexCount; vertexIndex < totalVertexCount; ++vertexIndex) {
        vertices[vertexIndex] = filler;
    }
}

}  // namespace miacode::preview::quick_scene
