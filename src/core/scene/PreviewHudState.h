#pragma once

#include <QFont>
#include <QString>
#include <QStringList>
#include <QVector>

#include "timeline/TimelineData.h"

namespace miacode::preview::scene {

struct PreviewHudStats {
    int tapPlayed = 0;
    int holdPlayed = 0;
    int slidePlayed = 0;
    int touchPlayed = 0;
    int breakPlayed = 0;
    int tapTotal = 0;
    int holdTotal = 0;
    int slideTotal = 0;
    int touchTotal = 0;
    int breakTotal = 0;
    int combo = 0;
    int totalNotes = 0;
    int dxScore = 0;
    int dxScoreMax = 0;
    double finaleRate = 0.0;
    double deluxeRate = 0.0;
    int deluxeBreakCurrent = 0;
    int deluxeBreakTotal = 0;
};

enum class PreviewHudFontArea {
    ChartInfo,
    Timestamp,
    CenterDisplay,
    ObjectStats,
    DebugInfo,
};

struct PreviewHudFontAreaChoice {
    PreviewHudFontArea area;
    const char* labelKey;
    const char* sample;
};

PreviewHudStats computePreviewHudStats(const QVector<TimelineNoteMarker>& noteMarkers, double second);
QString formatPreviewHudTimeLabel(double seconds);
QFont previewHudTimestampFont(int pointSize, QFont::Weight weight = QFont::Medium);
QFont previewHudMonoFont(int pointSize, QFont::Weight weight = QFont::Medium);
QString previewHudFontDisplayName();
void setPreviewHudCustomFontPath(const QString& fontPath);
QString previewHudCustomFontPath(PreviewHudFontArea area);
QFont previewHudTimestampFontForArea(
    PreviewHudFontArea area,
    int pointSize,
    QFont::Weight weight = QFont::Medium);
QFont previewHudMonoFontForArea(
    PreviewHudFontArea area,
    int pointSize,
    QFont::Weight weight = QFont::Medium);
QString previewHudFontDisplayName(PreviewHudFontArea area);
void setPreviewHudCustomFontPath(PreviewHudFontArea area, const QString& fontPath);
QVector<PreviewHudFontAreaChoice> previewHudFontAreaChoices();
int previewHudFontAreaId(PreviewHudFontArea area);
PreviewHudFontArea previewHudFontAreaFromId(int areaId);
int previewHudFontAreaIndex(PreviewHudFontArea area);

}  // namespace miacode::preview::scene
