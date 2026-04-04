#pragma once

#include <QtGlobal>

namespace miacode::preview::scene {

enum PreviewRenderLayer : quint32 {
    StageBackgroundLayer = 1u << 0,
    BackdropLayer = 1u << 1,
    MuriPadStateLayer = 1u << 2,
    MuriActionLayer = 1u << 3,
    JudgeFireworkLayer = 1u << 4,
    GuideLayer = 1u << 5,
    TrackLayer = 1u << 6,
    SlideMotionLayer = 1u << 7,
    JudgeLayer = 1u << 8,
    JudgeTouchLayer = 1u << 9,
    HeadLayer = 1u << 10,
    TouchLayer = 1u << 11,
    TouchHoldLayer = 1u << 12,
    ChartReviewLayer = 1u << 13,
    MaimuriDxJudgeLayer = 1u << 14,
    HudLayer = 1u << 15,
};

using PreviewRenderLayerFlags = quint32;

inline constexpr PreviewRenderLayerFlags kPreviewAllRenderLayers =
    StageBackgroundLayer
    | BackdropLayer
    | MuriPadStateLayer
    | MuriActionLayer
    | JudgeFireworkLayer
    | GuideLayer
    | TrackLayer
    | SlideMotionLayer
    | JudgeLayer
    | JudgeTouchLayer
    | HeadLayer
    | TouchLayer
    | TouchHoldLayer
    | ChartReviewLayer
    | MaimuriDxJudgeLayer
    | HudLayer;

inline constexpr PreviewRenderLayerFlags kPreviewSceneGraphFirstBatchLayers =
    StageBackgroundLayer
    | BackdropLayer
    | HudLayer;

inline constexpr PreviewRenderLayerFlags kPreviewSceneGraphSecondBatchLayers =
    kPreviewSceneGraphFirstBatchLayers
    | MuriPadStateLayer
    | MuriActionLayer
    | JudgeFireworkLayer
    | JudgeTouchLayer;

inline constexpr PreviewRenderLayerFlags kPreviewLegacyLowerBridgeLayers =
    TrackLayer
    | SlideMotionLayer
    | JudgeLayer;

inline constexpr PreviewRenderLayerFlags kPreviewLegacyPostTouchBridgeLayers =
    ChartReviewLayer
    | MaimuriDxJudgeLayer;

inline constexpr PreviewRenderLayerFlags kPreviewLegacyBridgeLayers =
    kPreviewAllRenderLayers
    & ~kPreviewSceneGraphSecondBatchLayers
    & ~GuideLayer
    & ~HeadLayer
    & ~TouchLayer
    & ~TouchHoldLayer;

inline bool previewRenderLayerEnabled(PreviewRenderLayerFlags flags, PreviewRenderLayer layer)
{
    return (flags & static_cast<PreviewRenderLayerFlags>(layer)) != 0;
}

}  // namespace miacode::preview::scene
