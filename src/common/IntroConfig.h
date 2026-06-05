#pragma once

namespace miacode::intro {

// IntroOverlay.qml authors its whole timeline in 60-fps frames. The overlay
// window == durationFrames == cycle2End == 349 (the cycle-2 wipe starts at 195
// = 3.25 s and spans 154 frames = 2.567 s, retracting fully at 349 = 5.817 s,
// which is also where the chart hands off / audio starts). The chart-background
// fade-from-black (5.6 s → 6.6 s) is a downstream ffmpeg filter that deliberately
// extends PAST this hand-off. KEEP IN SYNC with src/intro/qml/IntroOverlay.qml —
// if the QML timeline length changes, update kDurationFrames here too.
inline constexpr int kAuthoringFps = 60;
inline constexpr int kDurationFrames = 349;
inline constexpr double kDurationSeconds =
    static_cast<double>(kDurationFrames) / static_cast<double>(kAuthoringFps);  // ~5.82 s

// Chart-background ("曲绘") fade-from-black. Applied to the ffmpeg BACKGROUND base
// (so the playfield outline + HUD, which live in the QSG overlay, are unaffected)
// — NOT in the QML overlay. The bg is held black through the covered intro and the
// early part of the full-range lead-in preview, then fades from pure black to full
// brightness, COMPLETING exactly at chart 0 (so the 曲绘 is full when the song +
// clock_count start). Only the DURATION is fixed here; the start is computed in the
// controller as (chart-0 output second − duration), i.e. anchored to the lead-in,
// not a fixed frame. See the VideoExportController bg-fade filter.
inline constexpr int kBgFadeDurationFrames = 60;  // 1.00 s fade, ending AT chart 0
inline constexpr double kBgFadeDurationSeconds =
    static_cast<double>(kBgFadeDurationFrames) / static_cast<double>(kAuthoringFps); // ~1.00 s

// The maimai overlay covers the chart until its cycle-2 wipe fully retracts; the
// HUD / timestamp are suppressed until this authoring frame, then shown over the
// black/fading background (so they read as "unaffected" by the bg fade).
inline constexpr int kHudRevealFrame = 349;       // == cycle2End (wipe fully retracted)

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
