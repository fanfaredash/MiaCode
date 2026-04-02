#pragma once

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

struct TimelineNoteMarker;

struct MuriPadTimeEntry {
    QString pad;
    double proportion = 0.0;
};

enum class MuriKind {
    SlideTooFast,
    SlideHeadTap,
    TapOnSlide,
    Overlap,
    MultiTouch,
};

enum class MuriAlertLevel {
    Muri,
    Warning,
};

enum class AreaJudgeCause {
    Self,
    Other,
    ExistingHold,
    Unknown,
};

enum class MuriJudgeSpriteKind {
    Simple,
    SlideStraight,
    SlideCircleCcw,
    SlideCircleCw,
    WifiUp,
    WifiDown,
};

enum class MuriSimpleJudgeEffect {
    Good,
    Perfect,
};

struct MuriPadWindow {
    QString pad;
    double startSecond = 0.0;
    double endSecond = 0.0;
    QString sourceMarkerKey;
    QString sourceType;
};

struct MuriActionTrail {
    QString sourceMarkerKey;
    QString sourceType;
    double startSecond = 0.0;
    double endSecond = 0.0;
    double radius = 0.0;
    QVector<QPointF> points;
};

struct MuriCheckpointState {
    double proportion = 0.0;
    double second = -1.0;
    QStringList pads;
    QString causeMarkerKey;
    QString causeType;
    AreaJudgeCause cause = AreaJudgeCause::Unknown;
    bool skipped = false;
};

struct MuriSegmentState {
    QVector<QVector<MuriCheckpointState>> areaCheckpoints;
    double completedSecond = -1.0;
    double expectedCompletedSecond = -1.0;
    double criticalSecond = -1.0;
};

struct MarkerMuriState {
    QString markerKey;
    QString markerType;
    int line = 1;
    int col = 1;
    double second = 0.0;
    double endSecond = 0.0;
    bool earlyCleared = false;
    double flashSecond = -1.0;
    QVector<MuriSegmentState> slideSegments;
    QVector<QVector<QVector<MuriCheckpointState>>> wifiLaneAreas;
    QVector<QVector<double>> wifiLaneProgressSeconds;
    double wifiCompletedSecond = -1.0;
    double wifiExpectedCompletedSecond = -1.0;
    double wifiPadCSecond = -1.0;
    double wifiCriticalSecond = -1.0;
};

struct MuriDiagnostic {
    MuriKind kind = MuriKind::SlideTooFast;
    MuriAlertLevel alertLevel = MuriAlertLevel::Muri;
    double second = 0.0;
    double anchorSecond = -1.0;
    int line = 1;
    int col = 1;
    QString markerKey;
    QString title;
    QString detail;
};

struct MuriJudgeSpriteEvent {
    MuriJudgeSpriteKind kind = MuriJudgeSpriteKind::Simple;
    MuriSimpleJudgeEffect simpleEffect = MuriSimpleJudgeEffect::Good;
    double second = 0.0;
    double spawnSecond = 0.0;
    QString markerKey;
    QString pad;
    int lane = 1;
};

struct MuriStaticReferenceNote {
    QString markerKey;
    QString markerType;
    QString pad;
    QString slideTrackKey;
    int line = 1;
    int col = 1;
    int lane = 1;
    int endLane = 1;
    double second = 0.0;
    double endSecond = -1.0;
    double slideTraceSecond = -1.0;
    bool headStarTapLike = false;
};

struct MuriStaticReference {
    MuriKind kind = MuriKind::Overlap;
    MuriAlertLevel alertLevel = MuriAlertLevel::Muri;
    MuriStaticReferenceNote affected;
    MuriStaticReferenceNote cause;
    double deltaSecond = 0.0;
    bool hasDelta = false;
    bool slideHeadHasTapOnSlide = false;
};

struct MuriAnalysisReport {
    QHash<QString, MarkerMuriState> markerStates;
    QVector<MuriDiagnostic> diagnostics;
    QVector<MuriJudgeSpriteEvent> judgeSpriteEvents;
    QVector<MuriPadWindow> padWindows;
    QVector<MuriActionTrail> actionTrails;
    QString sourceSignature;

    bool isEmpty() const
    {
        return markerStates.isEmpty()
            && diagnostics.isEmpty()
            && judgeSpriteEvents.isEmpty()
            && padWindows.isEmpty()
            && actionTrails.isEmpty();
    }
};

QString makeMarkerAnalysisKey(const TimelineNoteMarker& marker);
QString muriKindDisplayName(MuriKind kind, bool chineseUi);
QString muriAlertLevelDisplayName(MuriAlertLevel level, bool chineseUi);
