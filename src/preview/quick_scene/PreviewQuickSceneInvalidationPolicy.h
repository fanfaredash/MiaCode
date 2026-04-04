#pragma once

#include <QtGlobal>

namespace miacode::preview::quick_scene {

enum PreviewQuickSceneDirtyFlag : quint32 {
    StageBackgroundDirty = 1u << 0,
    BackdropDirty = 1u << 1,
    HudDirty = 1u << 2,
    LegacyBridgeDirty = 1u << 3,
    GeometryDirty = 1u << 4,
};

using PreviewQuickSceneDirtyFlags = quint32;

inline constexpr PreviewQuickSceneDirtyFlags kPreviewQuickSceneAllDirty =
    StageBackgroundDirty
    | BackdropDirty
    | HudDirty
    | LegacyBridgeDirty
    | GeometryDirty;

}  // namespace miacode::preview::quick_scene
