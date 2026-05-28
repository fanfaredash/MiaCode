#pragma once

#include <array>

#include <QColor>
#include <QtGlobal>

#include "app/ui/UiTheme.h"

namespace miacode::timeline {

constexpr double kTimelineWaveformBrightnessMin = 0.2;
constexpr double kTimelineWaveformBrightnessMax = 2.0;
constexpr double kTimelineWaveformBrightnessDefault = 1.0;

inline double normalizedTimelineWaveformBrightness(double brightness)
{
    return qBound(kTimelineWaveformBrightnessMin, brightness, kTimelineWaveformBrightnessMax);
}

inline QColor adjustedTimelineWaveformColor(QColor color, double brightness)
{
    const double clamped = normalizedTimelineWaveformBrightness(brightness);
    color.setRed(qBound(0, qRound(static_cast<double>(color.red()) * clamped), 255));
    color.setGreen(qBound(0, qRound(static_cast<double>(color.green()) * clamped), 255));
    color.setBlue(qBound(0, qRound(static_cast<double>(color.blue()) * clamped), 255));
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
