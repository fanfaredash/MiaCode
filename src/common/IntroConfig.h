#pragma once

namespace miacode::intro {

// IntroOverlay.qml authors its whole timeline in 60-fps frames; its
// durationFrames == cycle2End(291) + bgHoldFrames(12) + bgRevealFrames(18) == 321
// (the cycle-2 wipe ends at 291, then a black hold + a fade-from-black on the
// chart background). KEEP IN SYNC with src/intro/qml/IntroOverlay.qml — if the
// QML timeline length changes, update kDurationFrames here too.
inline constexpr int kAuthoringFps = 60;
inline constexpr int kDurationFrames = 321;
inline constexpr double kDurationSeconds =
    static_cast<double>(kDurationFrames) / static_cast<double>(kAuthoringFps);  // ~5.35 s

// Chart-background fade-from-black. Applied to the ffmpeg BACKGROUND base (so the
// playfield outline + HUD, which live in the QSG overlay, are unaffected) — NOT
// in the QML overlay. The bg is black until the maimai wipe has retracted + a
// short hold, then fades in. Authoring frame N maps to OUTPUT second N/kAuthoringFps.
inline constexpr int kBgFadeStartFrame = 303;     // cycle2End(291) + black hold(12)
inline constexpr int kBgFadeDurationFrames = 18;  // ~0.30 s fade
inline constexpr double kBgFadeStartSeconds =
    static_cast<double>(kBgFadeStartFrame) / static_cast<double>(kAuthoringFps);     // ~5.05 s
inline constexpr double kBgFadeDurationSeconds =
    static_cast<double>(kBgFadeDurationFrames) / static_cast<double>(kAuthoringFps); // ~0.30 s

// The maimai overlay covers the chart until its cycle-2 wipe fully retracts; the
// HUD / timestamp are suppressed until this authoring frame, then shown over the
// black/fading background (so they read as "unaffected" by the bg fade).
inline constexpr int kHudRevealFrame = 291;       // == cycle2End

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
