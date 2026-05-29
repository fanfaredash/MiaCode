#pragma once

#include <limits>

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

struct TimelineNoteMarker;

// Shared internal data model for the Muri analyzer pipeline (2026-05-29 god-file
// decomposition). These structs are the common vocabulary passed between the
// analyzer's stages (runtime-model build, pad-window/overlay build, slide/wifi
// judging, simple-note + multi-touch judging, diagnostic collection). They are
// implementation detail of the Muri analyzer — namespace miacode::muri::detail —
// not part of the public MuriAnalyzer / MuriTypes surface.
//
// Stage-local structs (e.g. the slide/wifi runtime-judge working state) stay
// with the stage that owns them rather than living here.
namespace miacode::muri::detail {

struct PadWindowIndex {
    int index = -1;
    double activeSecond = -1.0;
};

struct MarkerSourceRef {
    int order = -1;
    double second = 0.0;
    int line = 1;
    int col = 1;
    QString type;
};

struct PadWindowInterval {
    QString pad;
    int startTick = 0;
    int endTick = -1;
    QString sourceMarkerKey;
    QString sourceType;
    int sourceOrder = -1;
    int line = 1;
    int col = 1;
};

struct RuntimePadEvent {
    QString pad;
    int tick = 0;
    double second = -1.0;
    QString sourceMarkerKey;
    QString sourceType;
    int sourceOrder = -1;
    int line = 1;
    int col = 1;
    bool extraPadDown = false;
};

struct RuntimeJudgeHit {
    bool judged = false;
    bool skipped = false;
    double second = -1.0;
    RuntimePadEvent cause;
};

struct RuntimeSlideJudgeResult {
    bool valid = false;
    bool isWifi = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    double criticalSecond = -1.0;
    QVector<double> segmentCompletedSeconds;
    QVector<RuntimeJudgeHit> areaHits;
    QVector<QStringList> judgeSequence;
    QVector<int> segmentEndAreaIndices;
    QVector<QVector<RuntimeJudgeHit>> wifiLaneAreaHits;
    QVector<QVector<QStringList>> wifiLaneJudgeSequence;
    QVector<QVector<double>> wifiLaneProgressSeconds;
    double wifiPadCSecond = -1.0;
};

struct JudgeableSimpleNote {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    QString type;
    QString pad;
    int order = -1;
    int parseOrder = -1;
    int eachGroupId = -1;
    int line = 1;
    int col = 1;
    double momentSecond = 0.0;
    double pressEndSecond = 0.0;
    double criticalSeconds = 0.0;
    double availableSeconds = 0.0;
    int expiryTick = 0;
    bool slideHead = false;
    bool onSlide = false;
    bool tailOnSlideHead = false;
    bool headStarTapLike = false;
    bool hasProtection = false;
    bool judged = false;
    bool judgeBad = false;
    bool lateBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent cause;
};

struct RuntimeTouchGroup {
    int eachGroupId = -1;
    int firstParseOrder = -1;
    QString sourceMarkerKey;
    int line = 1;
    int col = 1;
    double momentSecond = 0.0;
    bool allOnSlide = true;
    QVector<int> childNoteIndices;
    double threshold = 0.0;
};

struct RuntimeTopLevelNote {
    int sequenceOrder = -1;
    int simpleNoteIndex = -1;
    int touchGroupIndex = -1;
};

enum class RuntimeHandActionKind {
    Press,
    Slide,
};

struct RuntimeHandAction {
    RuntimeHandActionKind kind = RuntimeHandActionKind::Press;
    QString markerKey;
    QString sourceType;
    QString mergeKey;
    int order = -1;
    int line = 1;
    int col = 1;
    double startSecond = 0.0;
    double endSecond = 0.0;
    double motionDurationSecond = 0.0;
    double radius = 0.0;
    bool requireTwoHands = false;
    QVector<QPointF> points;
};

struct RuntimeTouchPoint {
    int actionIndex = -1;
    QPointF center;
    QPointF tangent;
    double radius = 0.0;
    bool hasTangent = false;
};

struct DiagnosticAnchor {
    bool valid = false;
    double second = 0.0;
    int order = std::numeric_limits<int>::max();
    int line = 1;
    int col = 1;
};

}  // namespace miacode::muri::detail
