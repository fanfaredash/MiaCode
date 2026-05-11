#pragma once

#include <QFont>
#include <QHash>
#include <QSize>

#include <memory>

#include "common/WaveformCache.h"
#include "timeline/TimelineRenderData.h"
#include "timeline/TimelineSceneState.h"

namespace miacode::timeline {

struct TimelineSceneBuildRequest {
    TimelineRenderSnapshot snapshot;
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData;
    QHash<quint64, QVector<TimelineMuriMarkerPlacement>> muriMarkersByLocation;
    QHash<quint64, QString> muriMarkerTooltips;
    QSize viewportSize;
    QFont headerLineNumberFont;
    int horizontalScrollValue = 0;
    int headerLeftLimit = 0;
    int headerRightLimit = 0;
    // Phase 7 — scroll-bucket viewport culling. When > 0, the builder
    // emits waveform / grid / note primitives only for the visible
    // second range expanded by this many pixels on each side. The
    // caller (TimelineQuickItem) bumps cached revisions by the
    // current scroll bucket so QSG layers rebuild children whenever
    // the user scrolls into a new bucket. When 0, the builder falls
    // back to legacy full-chart emission (correct for paths that
    // don't manage bucket-based revision bumps). Set to viewportSize
    // .width() in the bucket-aware path so emission spans 3 viewports
    // total (one buffer on each side) — small enough to keep
    // primitive count bounded, large enough that intra-bucket scroll
    // moves don't reveal empty space.
    int horizontalCullPaddingPx = 0;
    double zoomScale = 0.5;
    double contentScale = 1.0;
    double playbackEntrySeconds = 0.0;
    double playheadSeconds = 0.0;
    double cursorSeconds = 0.0;
    double playheadUpperLimitSeconds = -1.0;
    bool showSlideTracks = true;
    bool playheadIndicatorSuppressed = false;
    bool dragActive = false;
    // Phase 9d-native — header control state (mirrors the QML
    // ToolButton + CheckBox in TimelineTabSurface.qml). The DComp
    // path renders these natively in the popup composition plane
    // because DWM stacks the popup HWND above the QQuickWindow's
    // surface and QML siblings can't paint above it.
    bool followPreviewEnabled = false;
    bool followProgressEnabled = true;
    bool isChineseUi = false;
    quint64 appearanceRevision = 0;
    quint64 gridRevision = 0;
    quint64 waveformRevision = 0;
    quint64 headerRevision = 0;
    quint64 notesRevision = 0;
    quint64 overlayRevision = 0;
    quint64 overlayDynamicRevision = 0;
};

class TimelineSceneStateBuilder
{
public:
    static TimelineSceneLayoutMetrics layoutMetrics(const TimelineSceneBuildRequest& request);
    static int maxHorizontalScrollValue(const TimelineSceneLayoutMetrics& metrics);
    static int secondToSceneX(const TimelineSceneLayoutMetrics& metrics, double second);
    static TimelineSceneState build(const TimelineSceneBuildRequest& request);
    static int secondToSceneX(const TimelineSceneState& state, double second);
    static double sceneXToSecond(const TimelineSceneState& state, qreal x);
};

}  // namespace miacode::timeline
