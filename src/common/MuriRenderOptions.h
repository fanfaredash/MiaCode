#pragma once

enum class RenderMode {
    Native,
    MaimuriDxStyle,
};

struct MuriRenderOptions {
    RenderMode renderMode = RenderMode::Native;
    bool showSlideTracks = true;
    bool showJudgeMarkers = false;
    bool showTouchTrail = false;
};
