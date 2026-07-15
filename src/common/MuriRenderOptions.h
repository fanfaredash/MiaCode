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
    bool showChartReviewTapJudgeOverlay = false;
    bool showChartReviewBreakJudgeOverlay = false;
    bool showChartReviewTouchJudgeOverlay = false;
    bool wifiNeedC = false;
    bool excludeTouchFromMultiTouch = true;
};
