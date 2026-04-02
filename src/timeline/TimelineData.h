#pragma once

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "common/MuriTypes.h"

struct TimelineBeatMarker {
    double second = 0.0;
    bool major = false;
    int sourceLine = 1;
    int sourceCol = 1;
};

struct TimelineNoteMarker {
    double second = 0.0;
    double endSecond = -1.0;
    double slideTraceSecond = -1.0;
    double availableSecond = -1.0;
    int parseOrder = -1;
    int eachGroupId = -1;
    int sourceLine = 1;
    int sourceCol = 1;
    int lane = 1;
    int endLane = 1;
    QString type;
    QString slideDisplayKey;
    QString slideTrackKey;
    QStringList slideSegmentKeys;
    QVector<double> slideSegmentShootSeconds;
    QVector<double> slideSegmentDurations;
    QVector<QVector<MuriPadTimeEntry>> slideSegmentPadEnterTimes;
    QVector<double> slideSegmentCriticalProportions;
    QVector<QVector<QPointF>> slideSegmentPoints;
    QVector<QVector<double>> slideSegmentAngles;
    QVector<QVector<QPointF>> wifiLanePoints;
    QVector<QVector<double>> wifiLaneAngles;
    QVector<QVector<QVector<QPointF>>> slideTrackAreaPoints;
    QVector<QVector<QVector<double>>> slideTrackAreaRotations;
    QVector<QVector<double>> slideTrackAreaThresholds;
    QVector<QVector<QVector<double>>> slideTrackAreaCheckpoints;
    QVector<QVector<QVector<int>>> slideTrackAreaCutIndices;
    QVector<QVector<QPointF>> wifiTrackAreaPoints;
    QVector<QVector<double>> wifiTrackAreaRotations;
    QVector<QVector<int>> wifiTrackAreaImageIndices;
    QVector<double> wifiTrackAreaThresholds;
    QVector<QVector<double>> wifiTrackAreaCheckpoints;
    QVector<MuriPadTimeEntry> wifiPadEnterTimes;
    double wifiCriticalProportion = 1.0;
    double slideNativeTrackLength = 0.0;
    double slideRuntimeTrackLength = 0.0;
    QPointF touchPoint;
    QString touchPad;
    bool tapUsesStarMaterial = false;
    bool tapStarDouble = false;
    bool isEach = false;
    bool isBreak = false;
    bool isEx = false;
    bool isFirework = false;
    bool onSlide = false;
    bool slideHead = false;
    bool tailOnSlideHead = false;
    bool slideEach = false;
    bool sameHeadSlide = false;
    bool beforeSlide = false;
    bool afterSlide = false;
    bool headEach = false;
    bool headBreak = false;
    bool headEx = false;
    bool slideHeadUsesTapMaterial = false;
    bool trackBreak = false;
    bool hasHeadStar = true;
    bool headlessImmediate = false;
};
