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
    bool showChartReviewSlideJudgeOverlay = true;
    bool showChartReviewSimpleJudgeOverlay = false;
    bool wifiNeedC = false;
    bool excludeTouchFromMultiTouch = true;
};
