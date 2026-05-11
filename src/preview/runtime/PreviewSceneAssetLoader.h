#pragma once

#include <QString>

#include "core/scene/PreviewFrameState.h"

namespace miacode::preview::runtime {

struct PreviewSceneAssetLoadResult {
    quint64 generation = 0;
    QString skinDirectory;
    scene::PreviewAssetState assetState;
    scene::PreviewSkinAssets skinAssets;
    scene::PreviewJudgeOverlayAssets judgeOverlayAssets;
    scene::PreviewJudgeEffectAssets judgeEffectAssets;
};

class PreviewSceneAssetLoader
{
public:
    static PreviewSceneAssetLoadResult load(
        const QString& skinDirectory,
        PreviewOutlineVariant outlineVariant,
        quint64 generation = 0,
        const QString& outlineImagePath = QString()
    );

    static scene::PreviewAssetState loadAssetState(
        PreviewOutlineVariant outlineVariant,
        const QString& outlineImagePath = QString());
};

}  // namespace miacode::preview::runtime
