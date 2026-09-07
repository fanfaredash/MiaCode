#pragma once

#include <QString>

enum class RenderMode {
    Native,
    MaimuriDxStyle,
    EraseByArea,
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

// Persisted render-mode token, shared by portable preview state and the video
// export snapshot so the two cannot drift apart. Unknown tokens fall back to
// Native, which keeps older settings files loading.
inline QString muriRenderModeToken(RenderMode mode)
{
    switch (mode) {
        case RenderMode::MaimuriDxStyle:
            return QStringLiteral("maimuri_dx_style");
        case RenderMode::EraseByArea:
            return QStringLiteral("erase_by_area");
        case RenderMode::Native:
            break;
    }
    return QStringLiteral("native");
}

inline RenderMode muriRenderModeFromToken(const QString& token)
{
    const QString normalized = token.trimmed();
    if (normalized.compare(QStringLiteral("maimuri_dx_style"), Qt::CaseInsensitive) == 0) {
        return RenderMode::MaimuriDxStyle;
    }
    if (normalized.compare(QStringLiteral("erase_by_area"), Qt::CaseInsensitive) == 0) {
        return RenderMode::EraseByArea;
    }
    return RenderMode::Native;
}
