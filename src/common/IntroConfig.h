#pragma once

namespace miacode::intro {

// IntroOverlay.qml authors its whole timeline in 60-fps frames; its
// durationFrames == cycle2Start(168) + cycleDuration(123) == 291.
// KEEP IN SYNC with src/intro/qml/IntroOverlay.qml — if the QML timeline
// length changes, update kDurationFrames here too.
inline constexpr int kAuthoringFps = 60;
inline constexpr int kDurationFrames = 291;
inline constexpr double kDurationSeconds =
    static_cast<double>(kDurationFrames) / static_cast<double>(kAuthoringFps);  // ~4.85 s

// qrc locations (bundled via resources/intro.qrc + resources/app_icons.qrc).
inline constexpr char kOverlayQmlUrl[] = "qrc:/intro/qml/IntroOverlay.qml";
inline constexpr char kBannerTemplateUrl[] = "qrc:/intro/templates/maimai_banner.json";
inline constexpr char kLogoFallbackUrl[] = "qrc:/icons/app.png";
// Opening SFX, mixed at the FRONT of the export audio (output t=0 == intro
// start) over the silent front-pad. QFile/resource path form (no "qrc" scheme).
inline constexpr char kOpeningSfxResource[] = ":/intro/audio/track_start.wav";

// Map an export-frame index to the IntroOverlay `frame` (authored at 60 fps).
inline int authoringFrameForOutputFrame(int outputFrameIndex, int outputFps)
{
    if (outputFps <= 0) {
        return outputFrameIndex;
    }
    const double seconds = static_cast<double>(outputFrameIndex) / static_cast<double>(outputFps);
    return static_cast<int>(seconds * kAuthoringFps + 0.5);
}

// Number of export frames the intro pre-roll occupies at the given fps.
inline int introFrameCountForFps(int outputFps)
{
    const int fps = outputFps > 0 ? outputFps : kAuthoringFps;
    return static_cast<int>(kDurationSeconds * fps + 0.5);
}

}  // namespace miacode::intro
