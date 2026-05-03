#pragma once

#include <array>

#include <QColor>

#include "app/ui/UiTheme.h"

namespace miacode::timeline {

struct TimelineThemeColors {
    QColor window;
    QColor header;
    QColor sidebar;
    QColor base;
    QColor border;
    QColor axis;
    QColor gridMajor;
    QColor gridMinor;
    QColor laneEven;
    QColor laneOdd;
    QColor label;
    QColor textSecondary;
    QColor waveform;
    QColor playhead;
    QColor cursor;
    QColor entryMarker;
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
    // Beta21-fix6 — note-line clamp bumped 180 → 220 per user request
    // for "slightly more prominent" per-comma ticks. Higher cap pushes
    // the blended on-screen brightness another ~6-8 levels above lane
    // bg without going fully opaque (which would make note-line and
    // bar-line visually indistinguishable).
    QColor minorGrid = c.timelineBorder;
    minorGrid.setAlpha(qMin(minorGrid.alpha(), 220));
    return TimelineThemeColors{
        c.timelineWindow,
        c.timelineHeader,
        c.timelineSidebar,
        c.timelineBase,
        c.timelineBorder,
        c.timelineAxis,
        // Beta21-fix7 — bar lines tested against waveform contrast.
        // `c.timelineAxis` matched the boundary line color visually
        // against the empty sidebar (where the boundary lives), but
        // its luminance (~RGB 167 light / ~146 dark) is essentially
        // identical to `c.timelineWaveStroke` (~RGB 167 light / ~114
        // dark). With waveform underneath, the axis-colored bar line
        // disappears into the waveform brightness band even with the
        // force-Blending=true fix in TimelineQuickLayerUtils.cpp
        // ensuring the line draws after the waveform.
        //
        // Switching to `c.timelineLabel` (the lane-number text color):
        //   Light: `#4D5C6D` (RGB 77,92,109, brightness ~93) — clearly
        //          darker than waveform 167, visible separator.
        //   Dark:  `#C8D5E5` (RGB 200,213,229, brightness ~217) —
        //          clearly brighter than waveform 114, visible.
        // This still matches the user's "same color as boundary
        // vertical line" intent in spirit — the lane-number text and
        // the boundary line are part of the same left-hand sidebar
        // visual group, and timelineLabel is what the lane numbers
        // themselves are drawn in.
        c.timelineLabel,
        minorGrid,
        c.timelineLaneEven,
        c.timelineLaneOdd,
        c.timelineLabel,
        c.textSecondary,
        c.timelineWaveStroke,
        c.timelinePlayhead,
        c.timelineCursor,
        c.timelinePlayhead,
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
