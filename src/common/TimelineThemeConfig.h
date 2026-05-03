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
    return TimelineThemeColors{
        c.timelineWindow,
        c.timelineHeader,
        c.timelineSidebar,
        c.timelineBase,
        c.timelineBorder,
        c.timelineAxis,
        // Beta21-fix11 — wired through dedicated palette entries:
        //   bar lines  -> c.timelineGridMajor
        //   note lines -> c.timelineGridMinor
        // Tune those two values directly in UiTheme.cpp without
        // affecting any other timeline element (axis boundary, lane
        // labels, etc.).
        c.timelineGridMajor,
        c.timelineGridMinor,
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
