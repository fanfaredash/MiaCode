#pragma once

#include <QObject>

#include "preview/runtime/PreviewSceneAssetLoader.h"

namespace miacode::preview::runtime {

class PreviewSceneAssetRepository : public QObject
{
    Q_OBJECT

public:
    explicit PreviewSceneAssetRepository(QObject* parent = nullptr);

    void setStageMediaAvailable(bool hasMedia);
    bool stageMediaAvailable() const { return stageMediaAvailable_; }

    void setSkinDirectory(const QString& skinDirectory);
    bool loadSkinDirectorySync(const QString& skinDirectory);
    QString skinDirectory() const { return skinDirectory_; }

    bool hasCoreSkinAssetsLoaded() const;

    scene::PreviewAssetState assetState() const { return currentAssets_.assetState; }
    scene::PreviewSkinAssets skinAssets() const { return currentAssets_.skinAssets; }
    scene::PreviewJudgeOverlayAssets judgeOverlayAssets() const { return currentAssets_.judgeOverlayAssets; }
    scene::PreviewJudgeEffectAssets judgeEffectAssets() const { return currentAssets_.judgeEffectAssets; }
    PreviewSceneAssetLoadResult snapshot() const { return currentAssets_; }

signals:
    void assetsChanged();

private:
    void applyLoadResult(PreviewSceneAssetLoadResult&& result);

    QString skinDirectory_;
    quint64 loadGeneration_ = 0;
    bool stageMediaAvailable_ = false;
    PreviewSceneAssetLoadResult currentAssets_;
};

}  // namespace miacode::preview::runtime
