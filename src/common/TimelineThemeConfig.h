#pragma once

#include <array>

#include <QColor>
#include <QtGlobal>

#include "app/ui/UiTheme.h"

namespace miacode::timeline {

constexpr double kTimelineWaveformBrightnessMin = 0.2;
constexpr double kTimelineWaveformBrightnessMax = 2.0;
// Default waveform brightness (== translucent-fill opacity multiplier). 0.5
// gives a subtle silhouette that reads clearly in both themes without
// dominating the grid — adopted as the shipped default. The slider still
// spans kTimelineWaveformBrightnessMin..Max so users can dial it up/down.
constexpr double kTimelineWaveformBrightnessDefault = 0.5;

inline double normalizedTimelineWaveformBrightness(double brightness)
{
    return qBound(kTimelineWaveformBrightnessMin, brightness, kTimelineWaveformBrightnessMax);
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
// "brighter"); (2) on a light theme, lifting RGB moved the colour toward white,
// i.e. LESS contrast as you raised "brightness" (inverted); (3) once a channel
// clamped, more slider did nothing. Driving alpha instead is monotonic and
// intuitive in BOTH themes (higher = more prominent / more grid covered), never
// shifts hue, and uses the whole slider range. The curve is anchored so 1.0x
// reproduces the palette's base alpha exactly; below 1.0x it fades toward the
// backdrop, above 1.0x it approaches fully opaque (no dead zone at the top).
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
    const UiTheme::Colors& c = UiTheme::colors();
    return TimelineThemeColors{
        c.timelineWindow,
        c.timelineHeader,
        c.timelineSidebar,
        c.timelineBase,
        c.timelineBorder,
        c.timelineAxis,
        // Beta21-fix11 — wired through dedicated palette entries:
        //   bar lines        -> c.timelineGridMajor
        //   timeline lines   -> c.timelineGridSubdivision (between bar / note)
        //   note lines       -> c.timelineGridMinor
        // Tune these palette entries directly in UiTheme.cpp without
        // affecting any other timeline element (axis boundary, lane
        // labels, etc.).
        c.timelineGridMajor,
        c.timelineGridSubdivision,
        c.timelineGridMinor,
        c.timelineLaneEven,
        c.timelineLaneOdd,
        c.timelineLabel,
        c.textSecondary,
        c.timelineWaveStroke,
        c.timelinePlayhead,
        c.timelineCursor,
        c.timelinePlayhead,
        QColor(239, 68, 68, 230),
        QColor(255, 146, 43, 230),
        c.accent,
        QColor(255, 214, 64),
        QColor(255, 146, 43),
        c.accentText,
        QColor(44, 214, 255),
        QColor(44, 214, 255, 180),
        c.accent,
        c.accent,
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
