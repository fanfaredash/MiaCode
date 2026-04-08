#pragma once

#include <QString>

#include "preview/scene/PreviewFrameState.h"

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
        quint64 generation = 0
    );

    static scene::PreviewAssetState loadAssetState(PreviewOutlineVariant outlineVariant);
};

}  // namespace miacode::preview::runtime
