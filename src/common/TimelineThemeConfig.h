#pragma once

#include <array>

#include <QColor>
#include <QtGlobal>

namespace miacode::timeline {

constexpr double kTimelineWaveformBrightnessMin = 0.2;
constexpr double kTimelineWaveformBrightnessMax = 2.0;
// Default waveform brightness (== translucent-fill opacity multiplier). 0.5
// gives a subtle silhouette that reads clearly without dominating the grid —
// adopted as the shipped default. The slider still spans
// kTimelineWaveformBrightnessMin..Max so users can dial it up/down.
constexpr double kTimelineWaveformBrightnessDefault = 0.5;
constexpr double kTimelineMeasureLineBrightnessMin = 0.2;
constexpr double kTimelineMeasureLineBrightnessMax = 2.0;
constexpr double kTimelineMeasureLineBrightnessDefault = 1.0;

inline double normalizedTimelineWaveformBrightness(double brightness)
{
    return qBound(kTimelineWaveformBrightnessMin, brightness, kTimelineWaveformBrightnessMax);
}

inline double normalizedTimelineMeasureLineBrightness(double brightness)
{
    return qBound(kTimelineMeasureLineBrightnessMin, brightness, kTimelineMeasureLineBrightnessMax);
}

// Tiered grid-line heights feature. The timeline draws three tiers of vertical
// grid lines; this toggle controls whether they get distinct vertical extents
// so the hierarchy reads by HEIGHT as well as colour/width:
//   小节线   (bar / measure line)        -> kTimelineGridHeightFractionMeasure
//   四分音符线 (quarter/eighth subdivision) -> kTimelineGridHeightFractionSubdivision
//   逗号线   (per-comma note tick)        -> kTimelineGridHeightFractionComma
// Lines are anchored at the top of the timeline content area and extend down
// by `fraction * timelineHeight`, so a shorter line hangs from the ruler like
// a tick mark. When the feature is OFF (0) every tier spans the full height
// (the original behaviour). Flip kTimelineTieredGridLineHeightsEnabled to 0 to
// restore the legacy look.
constexpr int kTimelineTieredGridLineHeightsEnabled = 1;
constexpr qreal kTimelineGridHeightFractionMeasure = 9.0 / 9.0;
constexpr qreal kTimelineGridHeightFractionSubdivision = 8.0 / 9.0;
constexpr qreal kTimelineGridHeightFractionComma = 7.0 / 9.0;

// Resolve a tier's height fraction honouring the feature toggle: returns the
// tiered fraction when enabled, otherwise 1.0 (full height / legacy).
inline qreal timelineGridLineHeightFraction(qreal tieredFraction)
{
    return kTimelineTieredGridLineHeightsEnabled != 0 ? tieredFraction : 1.0;
}

// The waveform is a translucent fill stacked ON TOP of the grid lines, so the
// "brightness" slider is best expressed as opacity/prominence: scale ALPHA and
// keep RGB (hue + saturation) fixed. Rationale — the old per-channel RGB
// multiply had three problems: (1) at the high end the channels clamp to 255 at
// different points, so the teal drifted to pale cyan (a hue shift, not
// "brighter"); (2) lifting RGB moved the colour toward white, i.e. LESS
// contrast as you raised "brightness" (inverted); (3) once a channel clamped,
// more slider did nothing. Driving alpha instead is monotonic and intuitive,
// never shifts hue, and uses the whole slider range. The curve is anchored so
// 1.0x reproduces the palette's base alpha exactly; below 1.0x it fades toward
// the backdrop, above 1.0x it approaches fully opaque (no dead zone at the top).
inline QColor adjustedTimelineWaveformColor(QColor color, double brightness)
{
    const double clamped = normalizedTimelineWaveformBrightness(brightness);
    const double baseAlpha = static_cast<double>(color.alpha());
    double alpha = baseAlpha;
    if (clamped < 1.0) {
        // 0.2x .. 1.0x : fade toward the backdrop (down to 20% of base alpha).
        alpha = baseAlpha * clamped;
    } else if (clamped > 1.0) {
        // 1.0x .. 2.0x : approach fully opaque without a dead zone at the top.
        const double t = (clamped - 1.0) / (kTimelineWaveformBrightnessMax - 1.0);
        alpha = baseAlpha + (255.0 - baseAlpha) * t;
    }
    color.setAlpha(qBound(0, qRound(alpha), 255));
    return color;
}

inline QColor adjustedTimelineMeasureLineColor(QColor color, double brightness)
{
    const double clamped = normalizedTimelineMeasureLineBrightness(brightness);
    const int baseAlpha = color.alpha();
    if (clamped < 1.0) {
        color.setAlpha(qBound(0, qRound(static_cast<double>(baseAlpha) * clamped), 255));
        return color;
    }
    if (clamped > 1.0) {
        // Timeline chrome is a single dark scheme; brighten toward white with
        // the legacy dark-theme factor.
        const double t = (clamped - 1.0) / (kTimelineMeasureLineBrightnessMax - 1.0);
        const int factor = qRound(100.0 + 80.0 * t);
        color = color.lighter(factor);
        color.setAlpha(baseAlpha);
    }
    return color;
}

// Timeline "shell" chrome colours (window/base/border/grids/lanes/labels and
// the translucent waveform tint). These live in Theme.qml as the single source
// of truth; the QML TimelineThemeBridge writes them here before any scene
// state is built, and scene builders read them through timelineThemeColors().
struct TimelineChromeColors {
    QColor window;
    QColor header;
    QColor sidebar;
    QColor base;
    QColor border;
    QColor axis;
    QColor gridMajor;
    QColor gridSubdivision;
    QColor gridMinor;
    QColor laneEven;
    QColor laneOdd;
    QColor label;
    QColor textSecondary;
    QColor waveStroke;
};

inline TimelineChromeColors& timelineChromeColorsStorage()
{
    static TimelineChromeColors colors;
    return colors;
}

inline const TimelineChromeColors& timelineChromeColors()
{
    return timelineChromeColorsStorage();
}

inline void setTimelineChromeColors(const TimelineChromeColors& colors)
{
    timelineChromeColorsStorage() = colors;
}

struct TimelineThemeColors {
    QColor window;
    QColor header;
    QColor sidebar;
    QColor base;
    QColor border;
    QColor axis;
    QColor gridMajor;
    QColor gridSubdivision;
    QColor gridMinor;
    QColor laneEven;
    QColor laneOdd;
    QColor label;
    QColor textSecondary;
    QColor waveform;
    QColor playhead;
    QColor cursor;
    QColor entryMarker;
    QColor cursorMarker;
    QColor muriMarker;
    QColor controlFill;
    QColor controlBorder;
    QColor controlHover;
    QColor controlPressed;
    QColor controlAccent;
    QColor noteNormal;
    QColor noteEach;
    QColor noteBreak;
    QColor noteExStroke;
    QColor touch;
    QColor touchHold;
    QColor slideTrack;
    QColor holdBody;
    std::array<QColor, 5> fireworkBands;
};

inline TimelineThemeColors timelineThemeColors()
{
    const TimelineChromeColors& c = timelineChromeColors();
    return TimelineThemeColors{
        c.window,
        c.header,
        c.sidebar,
        c.base,
        c.border,
        c.axis,
        // Tiered grid lines map to dedicated chrome entries:
        //   bar lines        -> c.gridMajor
        //   beat lines       -> c.gridSubdivision (between bar / note)
        //   note lines       -> c.gridMinor
        c.gridMajor,
        c.gridSubdivision,
        c.gridMinor,
        c.laneEven,
        c.laneOdd,
        c.label,
        c.textSecondary,
        c.waveStroke,
        // Below: functional marker colours kept local to the timeline (single
        // dark scheme; they do not vary with the shell chrome).
        QColor("#FFC90E"),                 // playhead
        QColor("#F29A83"),                 // cursor
        QColor("#FFC90E"),                 // entry marker (same as playhead)
        QColor(242, 154, 131, 230),        // cursor marker (cursor at ~90%)
        QColor(255, 146, 43, 230),         // muri marker
        QColor("#202122"),                 // controlFill
        QColor("#333536"),                 // controlBorder
        QColor("#2A2B2C"),                 // controlHover
        QColor("#333536"),                 // controlPressed
        QColor("#3994BC"),                 // controlAccent
        QColor("#4F8FEC"),                 // noteNormal
        QColor("#FFD640"),                 // noteEach
        QColor("#FF922B"),                 // noteBreak
        QColor("#F7FBFF"),                 // noteExStroke
        QColor(44, 214, 255),              // touch
        QColor(44, 214, 255, 180),         // touchHold
        QColor("#4F8FEC"),                 // slideTrack
        QColor("#4F8FEC"),                 // holdBody (no-texture fallback line)
        {
            QColor(232, 124, 72),
            QColor(208, 106, 182),
            QColor(102, 180, 236),
            QColor(178, 202, 84),
            QColor(226, 206, 104),
        },
    };
}

}  // namespace miacode::timeline
