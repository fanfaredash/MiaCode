#include "timeline/quick/TimelineQuickItem.h"

#include "UiText.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGRendererInterface>
#include <QStringList>
#include <QToolTip>
#include <QMetaObject>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

#include "common/DebugLog.h"
#include "common/ProcessDiagnostics.h"
#include "common/DebugOptions.h"
#include "common/InputShortcutGesture.h"
#include "common/PreviewInteractionConfig.h"
#include "common/TimelineThemeConfig.h"
#include "common/WaveformCache.h"
#include "timeline/TimelineSceneStateBuilder.h"
#include "timeline/quick/TimelineQuickGridLayer.h"
#include "timeline/quick/TimelineQuickGridLinesLayer.h"
#include "timeline/quick/TimelineQuickHeaderLayer.h"
#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickNotesLayer.h"
#include "timeline/quick/TimelineQuickOverlayLayer.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "timeline/quick/TimelineQuickTextureCache.h"
#include "timeline/quick/TimelineQuickWaveformLayer.h"
#ifdef Q_OS_WIN
#include "render/backend_d3d11/TimelineRenderView.h"
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <dxgi1_4.h>
#endif

namespace {

constexpr double kTimelineKeyHoldAccelerationPerSecond = 1.0;
constexpr int kTimelineKeyHoldTickIntervalMs = 16;
constexpr int kTimelineLayerSlotCount = 6;  // grid + header + gridLines + wave(translucent,on-top) + notes + overlay

// beta7 leak gauge (probes 1.2 + ②③) — live QSG node count AND geometry vertex/index bytes
// under a timeline scene subtree. Walks firstChild()/nextSibling() once per pause (only when the
// render gauge is armed), never per frame. Flat counts while GPU/private memory climbs localises
// the leak BELOW our nodes (Qt-internal RHI deferred release); climbing counts convict orphaned
// render-thread nodes / growing geometry buffers. Called per-slot to break the total down by layer.
struct SceneGraphStats {
    int nodes = 0;
    qint64 geomBytes = 0;
};

void accumulateSceneGraphStats(const QSGNode* node, SceneGraphStats* out)
{
    if (node == nullptr || out == nullptr) {
        return;
    }
    out->nodes += 1;
    if (node->type() == QSGNode::GeometryNodeType) {
        const auto* geometryNode = static_cast<const QSGGeometryNode*>(node);
        if (const QSGGeometry* geometry = geometryNode->geometry()) {
            out->geomBytes += static_cast<qint64>(geometry->vertexCount()) * geometry->sizeOfVertex();
            out->geomBytes += static_cast<qint64>(geometry->indexCount()) * geometry->sizeOfIndex();
        }
    }
    for (const QSGNode* child = node->firstChild(); child != nullptr; child = child->nextSibling()) {
        accumulateSceneGraphStats(child, out);
    }
}

// beta7 leak gauge (probe ① — the decisive CPU-vs-GPU discriminator). private_mb on an integrated
// GPU conflates CPU heap + GPU/D3D11 resources (shared system RAM). DXGI's per-process
// QueryVideoMemoryInfo isolates the GPU portion: if gpu_kb climbs monotonically while our
// node/geometry/texture counts stay flat, the leak is Qt-internal RHI deferred release. Reads the
// RHI's ID3D11Device via QSGRendererInterface on the render thread (where it is valid). KB, or -1.
qint64 timelineGpuProcessMemoryKb(QQuickWindow* window)
{
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return -1;
    }
    QSGRendererInterface* rendererInterface = window->rendererInterface();
    if (rendererInterface == nullptr
        || rendererInterface->graphicsApi() != QSGRendererInterface::Direct3D11) {
        return -1;
    }
    void* devicePtr =
        rendererInterface->getResource(window, QSGRendererInterface::DeviceResource);
    if (devicePtr == nullptr) {
        return -1;
    }
    auto* device = reinterpret_cast<ID3D11Device*>(devicePtr);
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))
        || dxgiDevice == nullptr) {
        return -1;
    }
    qint64 usageKb = -1;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(
                __uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3)))
            && adapter3 != nullptr) {
            quint64 totalBytes = 0;
            DXGI_QUERY_VIDEO_MEMORY_INFO info;
            ZeroMemory(&info, sizeof(info));
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                totalBytes += info.CurrentUsage;
            }
            ZeroMemory(&info, sizeof(info));
            if (SUCCEEDED(
                    adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &info))) {
                totalBytes += info.CurrentUsage;
            }
            usageKb = static_cast<qint64>(totalBytes / 1024ull);
            adapter3->Release();
        }
        adapter->Release();
    }
    dxgiDevice->Release();
    return usageKb;
#else
    Q_UNUSED(window);
    return -1;
#endif
}

double timelineHeldKeyPlaybackRate(double heldSeconds, double maxPlaybackRate)
{
    if (heldSeconds <= 0.0) {
        return 1.0;
    }
    const double accelerated = 1.0 + heldSeconds * kTimelineKeyHoldAccelerationPerSecond;
    return accelerated > maxPlaybackRate ? maxPlaybackRate : accelerated;
}

QSGNode* ensureSlotRoot(QSGNode* oldNode)
{
    auto childCountFor = [](QSGNode* node) {
        int count = 0;
        for (QSGNode* child = node != nullptr ? node->firstChild() : nullptr; child != nullptr; child = child->nextSibling()) {
            ++count;
        }
        return count;
    };
    if (oldNode != nullptr && childCountFor(oldNode) == kTimelineLayerSlotCount) {
        return oldNode;
    }
    delete oldNode;
    auto* root = new QSGNode();
    for (int index = 0; index < kTimelineLayerSlotCount; ++index) {
        root->appendChildNode(new QSGNode());
    }
    return root;
}

QSGNode* layerSlotAt(QSGNode* root, int index)
{
    if (root == nullptr || index < 0) {
        return nullptr;
    }
    QSGNode* slot = root->firstChild();
    for (int currentIndex = 0; slot != nullptr && currentIndex < index; ++currentIndex) {
        slot = slot->nextSibling();
    }
    return slot;
}

template <typename UpdateFn>
void updateLayerSlot(QSGNode* slot, UpdateFn&& updateFn)
{
    if (slot == nullptr) {
        return;
    }
    QSGNode* oldChild = slot->firstChild();
    QSGNode* newChild = updateFn(oldChild);
    QSGNode* child = slot->firstChild();
    while (child != nullptr) {
        QSGNode* next = child->nextSibling();
        if (child != newChild) {
            slot->removeChildNode(child);
            delete child;
        }
        child = next;
    }
    if (newChild != nullptr && newChild->parent() != slot) {
        slot->appendChildNode(newChild);
    }
}

// Phase-4e-old-opt — replaces a string-concat-per-paint hot path with a
// numeric hash accumulator. The previous version built a QString of the
// form `font.toString() + color.name() + ...` per label/marker, which
// at ~100 labels typical for a long chart cost ~10 KB of heap allocs +
// memcpy per paint. The hash here is mixed with the FNV-style 0x9E3779B9
// constant + bit-rotation so that small changes (one label colour
// flipping) flip many bits, keeping comparison robustness.
quint64 timelineThemeSignatureHash(const miacode::timeline::TimelineSceneState& state)
{
    quint64 h = 0;
    const auto mix = [&](quint64 v) {
        // Boost-style hash combine. The magic constant is ~ golden
        // ratio in 64 bits; XOR + shift produces good diffusion.
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    };
    for (const auto& label : state.laneLabels) {
        mix(qHash(label.font));
        mix(static_cast<quint64>(label.color.rgba()));
    }
    for (const auto& label : state.headerLabels) {
        mix(qHash(label.font));
        mix(static_cast<quint64>(label.color.rgba()));
    }
    for (const auto& marker : state.headerMarkers) {
        mix(static_cast<quint64>(marker.color.rgba()));
    }
    if (state.hasEntryMarker) {
        mix(static_cast<quint64>(state.entryMarker.color.rgba()));
    }
    if (state.hasCursorMarker) {
        mix(static_cast<quint64>(state.cursorMarker.color.rgba()));
    }
    return h;
}

void appendTimelineQuickInteractionLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/interaction"),
        text
    );
}

quint64 overlayDynamicRevisionForState(const TimelineQuickStateBridge* stateBridge, bool dragActive)
{
    const quint64 bridgeRevision = stateBridge != nullptr ? stateBridge->overlayDynamicRevision() : 0;
    return (bridgeRevision << 1U) | (dragActive ? 1ULL : 0ULL);
}

void applyDynamicSceneState(
    miacode::timeline::TimelineSceneState* state,
    const TimelineQuickStateBridge* stateBridge,
    bool dragActive)
{
    if (state == nullptr || stateBridge == nullptr) {
        return;
    }

    state->horizontalScrollValue = stateBridge->horizontalScrollValue();
    state->overlayDynamicRevision = overlayDynamicRevisionForState(stateBridge, dragActive);
    state->visibleStartSecond =
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(*state, state->timelineLeft);
    state->visibleEndSecond =
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(*state, state->viewportSize.width());

    // Phase 7 — bucket-bump revisions. The QSG layers
    // (TimelineQuickWaveformLayer / Header / Notes) only
    // rebuild their child node tree when the corresponding revision
    // counter changes. With Phase 7 build-time culling, the cached
    // primitive set is bucket-specific, but the bridge's revisions
    // only bump on data changes (chart edits, waveform load) — not
    // on scroll. Adding the current scroll bucket to each revision
    // here gives the layer a bucket-specific view of the revision
    // that flips when the user scrolls into a new bucket. Combined
    // with the matching cache invalidation in
    // TimelineQuickItem::currentSceneState, the layer rebuilds its
    // children to match the freshly emitted primitives. Header line
    // numbers and line-start triangles use the same low-frequency
    // bucket boundary so paging within a bucket stays transform-only.
    // Within a bucket the bumped revision is constant, so layers
    // happily skip rebuilds and the per-frame cost stays at "set
    // transform".
    if (state->viewportSize.width() > 0) {
        const int bucketSize = state->viewportSize.width();
        const quint64 bucket =
            static_cast<quint64>(state->horizontalScrollValue / bucketSize);
        state->waveformRevision += bucket;
        state->gridRevision += bucket;
        state->headerRevision += bucket;
        state->notesRevision += bucket;
    }

    state->hasCursorLine = false;
    state->hasCursorMarker = false;
    state->hasPlayheadLine = false;
    state->hasDragCenterLine = false;

    const miacode::timeline::TimelineThemeColors theme = miacode::timeline::timelineThemeColors();
    const int cursorX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneX(*state, stateBridge->cursorSeconds());
    if (cursorX > state->timelineLeft) {
        state->hasCursorMarker = true;
        const qreal headerScale = 0.5 + (qBound(0.5, state->contentScale, 1.0) * 0.5);
        const qreal tipY = static_cast<qreal>(state->timelineTop) - (1.0 * headerScale);
        const qreal markerHeight = 8.0 * headerScale;
        const qreal markerHalfWidth = 6.0 * headerScale;
        const qreal baseY = qMax<qreal>(0.0, tipY - markerHeight);
        state->cursorMarker = miacode::timeline::TimelineSceneTriangle{
            QPointF(cursorX, tipY),
            QPointF(cursorX - markerHalfWidth, baseY),
            QPointF(cursorX + markerHalfWidth, baseY),
            theme.cursorMarker,
        };
        state->hasCursorLine = true;
        state->cursorLine = miacode::timeline::TimelineSceneLine{
            QPointF(cursorX, state->timelineTop),
            QPointF(cursorX, state->timelineTop + state->timelineHeight),
            theme.cursor,
            2.0,
        };
    }

    const int playheadX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneX(*state, stateBridge->playheadSeconds());
    if (!stateBridge->playheadIndicatorSuppressed() && playheadX > state->timelineLeft) {
        state->hasPlayheadLine = true;
        state->playheadLine = miacode::timeline::TimelineSceneLine{
            QPointF(playheadX, state->timelineTop),
            QPointF(playheadX, state->timelineTop + state->timelineHeight),
            theme.playhead,
            2.0,
        };
    }

    if (dragActive) {
        const int dragCenterX = state->viewportSize.width() / 2;
        if (dragCenterX > state->timelineLeft) {
            state->hasDragCenterLine = true;
            state->dragCenterLine = miacode::timeline::TimelineSceneLine{
                QPointF(dragCenterX, state->timelineTop),
                QPointF(dragCenterX, state->timelineTop + state->timelineHeight),
                theme.playhead,
                2.0,
            };
        }
    }
}

qreal renderMapSecondToSceneXExact(
    const miacode::timeline::TimelineSceneState& state,
    double second)
{
    return static_cast<qreal>(state.timelineLeft)
        + static_cast<qreal>((second - state.displayStartSeconds) * state.pixelsPerSecond)
        + static_cast<qreal>(state.leadingCenteringPadding);
}

double renderMapWorldXToSecond(
    const miacode::timeline::TimelineSceneState& state,
    qreal worldX)
{
    return miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(
        state,
        worldX - static_cast<qreal>(state.horizontalScrollValue));
}

QString renderMapPointPayload(
    const QString& name,
    const miacode::timeline::TimelineSceneState& state,
    double second)
{
    const int worldX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneX(state, second);
    const qreal worldXExact = renderMapSecondToSceneXExact(state, second);
    const qreal viewX = static_cast<qreal>(worldX - state.horizontalScrollValue);
    const qreal viewXExact = worldXExact - static_cast<qreal>(state.horizontalScrollValue);
    const double roundtrip =
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, viewX);
    const double roundtripExact =
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, viewXExact);
    const bool inView = viewXExact >= static_cast<qreal>(state.timelineLeft)
        && viewXExact <= static_cast<qreal>(state.viewportSize.width());
    return QStringLiteral(
               "action=sample name=%1 sec=%2 world_x=%3 world_x_exact=%4 "
               "view_x=%5 view_x_exact=%6 roundtrip_delta_ms=%7 "
               "roundtrip_exact_delta_ms=%8 in_view=%9")
        .arg(name)
        .arg(second, 0, 'f', 6)
        .arg(worldX)
        .arg(worldXExact, 0, 'f', 3)
        .arg(viewX, 0, 'f', 3)
        .arg(viewXExact, 0, 'f', 3)
        .arg((roundtrip - second) * 1000.0, 0, 'f', 3)
        .arg((roundtripExact - second) * 1000.0, 0, 'f', 3)
        .arg(inView ? 1 : 0);
}

QString renderMapRectPayload(
    const QString& prefix,
    const miacode::timeline::TimelineSceneState& state,
    const QRectF& rect)
{
    const qreal worldX = rect.left();
    const qreal viewX = worldX - static_cast<qreal>(state.horizontalScrollValue);
    return QStringLiteral(
               "%1_sec=%2 %1_world_x=%3 %1_view_x=%4 %1_w=%5")
        .arg(prefix)
        .arg(renderMapWorldXToSecond(state, worldX), 0, 'f', 6)
        .arg(worldX, 0, 'f', 3)
        .arg(viewX, 0, 'f', 3)
        .arg(rect.width(), 0, 'f', 3);
}

QString renderMapLinePayload(
    const QString& prefix,
    const miacode::timeline::TimelineSceneState& state,
    qreal worldX)
{
    const qreal viewX = worldX - static_cast<qreal>(state.horizontalScrollValue);
    return QStringLiteral("%1_sec=%2 %1_world_x=%3 %1_view_x=%4")
        .arg(prefix)
        .arg(renderMapWorldXToSecond(state, worldX), 0, 'f', 6)
        .arg(worldX, 0, 'f', 3)
        .arg(viewX, 0, 'f', 3);
}

double renderMapWaveformColumnEnergy(const miacode::waveform::WaveformColumn& column)
{
    return qMax(qAbs(static_cast<double>(column.min)), qAbs(static_cast<double>(column.max)));
}

QString renderMapWaveformPayload(
    const miacode::timeline::TimelineSceneState& state,
    const TimelineQuickStateBridge* stateBridge)
{
    if (stateBridge == nullptr) {
        return QStringLiteral("wave=0");
    }
    const std::shared_ptr<const miacode::waveform::WaveformData> waveform =
        stateBridge->waveformData();
    if (!waveform || waveform->durationSeconds <= 0.0) {
        return QStringLiteral("wave=0");
    }

    QString payload = QStringLiteral("wave=1 wave_duration=%1")
        .arg(waveform->durationSeconds, 0, 'f', 6);
    const miacode::waveform::WaveformLevel* level =
        miacode::waveform::selectWaveformLevelForVisibleRange(
            *waveform,
            qMax(0.001, state.visibleEndSecond - state.visibleStartSecond),
            qMax(1, state.viewportSize.width() - state.timelineLeft));
    if (level == nullptr || level->columns.isEmpty()) {
        payload += QStringLiteral(" wave_level=0");
        return payload;
    }

    const double phaseCompensationSeconds = qMax(0.0, state.waveformPhaseCompensationSeconds);
    const QPair<int, int> visibleColumns =
        miacode::waveform::visibleWaveformColumnRange(
            *level,
            qMax(0.0, state.visibleStartSecond + phaseCompensationSeconds),
            qMax(0.0, state.visibleEndSecond + phaseCompensationSeconds));
    const double firstColumnSecond =
        level->secondsPerColumn * static_cast<double>(visibleColumns.first);
    const double firstColumnRenderedSecond = firstColumnSecond - phaseCompensationSeconds;
    const qreal firstColumnWorldX = renderMapSecondToSceneXExact(state, firstColumnRenderedSecond);
    payload += QStringLiteral(
                   " wave_level=1 wave_spc_ms=%1 wave_cols=%2 "
                   "wave_visible_col_begin=%3 wave_visible_col_end=%4 "
                   "wave_phase_comp_ms=%5 wave_visible_first_sec=%6 "
                   "wave_visible_first_render_sec=%7 wave_visible_first_world_x=%8 "
                   "wave_visible_first_view_x=%9")
        .arg(level->secondsPerColumn * 1000.0, 0, 'f', 3)
        .arg(level->columns.size())
        .arg(visibleColumns.first)
        .arg(visibleColumns.second)
        .arg(phaseCompensationSeconds * 1000.0, 0, 'f', 3)
        .arg(firstColumnSecond, 0, 'f', 6)
        .arg(firstColumnRenderedSecond, 0, 'f', 6)
        .arg(firstColumnWorldX, 0, 'f', 3)
        .arg(firstColumnWorldX - static_cast<qreal>(state.horizontalScrollValue), 0, 'f', 3);

    const double playheadSecond = stateBridge->playheadSeconds();
    const bool playheadInDuration =
        playheadSecond >= 0.0 && playheadSecond <= waveform->durationSeconds;
    if (level->secondsPerColumn <= 0.0 || !std::isfinite(playheadSecond)) {
        payload += QStringLiteral(" wave_playhead_col=-1 wave_playhead_in_duration=%1")
            .arg(playheadInDuration ? 1 : 0);
        return payload;
    }

    const int playheadColumn = qBound(
        0,
        static_cast<int>(std::floor(qMax(0.0, playheadSecond) / level->secondsPerColumn)),
        level->columns.size() - 1);
    const double playheadColumnStartSecond =
        level->secondsPerColumn * static_cast<double>(playheadColumn);
    const double playheadColumnEndSecond = playheadColumnStartSecond + level->secondsPerColumn;
    const double playheadColumnCenterSecond =
        playheadColumnStartSecond + (level->secondsPerColumn * 0.5);
    const double playheadColumnStartRenderedSecond =
        playheadColumnStartSecond - phaseCompensationSeconds;
    const double playheadColumnCenterRenderedSecond =
        playheadColumnCenterSecond - phaseCompensationSeconds;
    const double playheadColumnEndRenderedSecond =
        playheadColumnEndSecond - phaseCompensationSeconds;
    const qreal playheadViewX =
        renderMapSecondToSceneXExact(state, playheadSecond)
        - static_cast<qreal>(state.horizontalScrollValue);
    const qreal playheadColumnStartWorldX =
        renderMapSecondToSceneXExact(state, playheadColumnStartRenderedSecond);
    const qreal playheadColumnCenterWorldX =
        renderMapSecondToSceneXExact(state, playheadColumnCenterRenderedSecond);
    const qreal playheadColumnEndWorldX =
        renderMapSecondToSceneXExact(state, playheadColumnEndRenderedSecond);
    const qreal playheadColumnCenterViewX =
        playheadColumnCenterWorldX - static_cast<qreal>(state.horizontalScrollValue);
    const miacode::waveform::WaveformColumn& playheadWaveColumn =
        level->columns.at(playheadColumn);
    const double playheadColumnEnergy = renderMapWaveformColumnEnergy(playheadWaveColumn);

    constexpr double kPeakSearchHalfWindowSeconds = 0.125;
    const int peakBeginColumn = qBound(
        0,
        static_cast<int>(std::floor(qMax(0.0, playheadSecond - kPeakSearchHalfWindowSeconds)
                                    / level->secondsPerColumn)),
        level->columns.size());
    const int peakEndColumn = qBound(
        peakBeginColumn,
        static_cast<int>(std::ceil(qMax(0.0, playheadSecond + kPeakSearchHalfWindowSeconds)
                                   / level->secondsPerColumn))
            + 1,
        level->columns.size());
    int peakColumn = -1;
    double peakEnergy = 0.0;
    for (int index = peakBeginColumn; index < peakEndColumn; ++index) {
        const double energy = renderMapWaveformColumnEnergy(level->columns.at(index));
        if (peakColumn < 0 || energy > peakEnergy) {
            peakColumn = index;
            peakEnergy = energy;
        }
    }

    payload += QStringLiteral(
                   " wave_playhead_sec=%1 wave_playhead_view_x=%2 "
                   "wave_playhead_col=%3 wave_playhead_in_duration=%4 "
                   "wave_playhead_col_start_sec=%5 wave_playhead_col_center_sec=%6 "
                   "wave_playhead_col_end_sec=%7 wave_playhead_col_start_render_sec=%8 "
                   "wave_playhead_col_center_render_sec=%9 wave_playhead_col_end_render_sec=%10 "
                   "wave_playhead_col_start_world_x=%11 wave_playhead_col_center_world_x=%12 "
                   "wave_playhead_col_end_world_x=%13 wave_playhead_col_start_view_x=%14 "
                   "wave_playhead_col_center_view_x=%15 wave_playhead_col_end_view_x=%16 "
                   "wave_playhead_col_center_dx_px=%17 wave_playhead_col_center_dt_ms=%18 "
                   "wave_playhead_col_amp=%19 wave_playhead_col_min=%20 wave_playhead_col_max=%21")
        .arg(playheadSecond, 0, 'f', 6)
        .arg(playheadViewX, 0, 'f', 3)
        .arg(playheadColumn)
        .arg(playheadInDuration ? 1 : 0)
        .arg(playheadColumnStartSecond, 0, 'f', 6)
        .arg(playheadColumnCenterSecond, 0, 'f', 6)
        .arg(playheadColumnEndSecond, 0, 'f', 6)
        .arg(playheadColumnStartRenderedSecond, 0, 'f', 6)
        .arg(playheadColumnCenterRenderedSecond, 0, 'f', 6)
        .arg(playheadColumnEndRenderedSecond, 0, 'f', 6)
        .arg(playheadColumnStartWorldX, 0, 'f', 3)
        .arg(playheadColumnCenterWorldX, 0, 'f', 3)
        .arg(playheadColumnEndWorldX, 0, 'f', 3)
        .arg(playheadColumnStartWorldX - static_cast<qreal>(state.horizontalScrollValue), 0, 'f', 3)
        .arg(playheadColumnCenterViewX, 0, 'f', 3)
        .arg(playheadColumnEndWorldX - static_cast<qreal>(state.horizontalScrollValue), 0, 'f', 3)
        .arg(playheadColumnCenterViewX - playheadViewX, 0, 'f', 3)
        .arg((playheadColumnCenterSecond - playheadSecond) * 1000.0, 0, 'f', 3)
        .arg(playheadColumnEnergy, 0, 'f', 6)
        .arg(playheadWaveColumn.min, 0, 'f', 6)
        .arg(playheadWaveColumn.max, 0, 'f', 6);

    if (peakColumn >= 0) {
        const double peakCenterSecond =
            (static_cast<double>(peakColumn) + 0.5) * level->secondsPerColumn;
        const double peakCenterRenderedSecond = peakCenterSecond - phaseCompensationSeconds;
        const qreal peakCenterWorldX = renderMapSecondToSceneXExact(state, peakCenterRenderedSecond);
        const qreal peakCenterViewX =
            peakCenterWorldX - static_cast<qreal>(state.horizontalScrollValue);
        payload += QStringLiteral(
                       " wave_near_peak_window_ms=%1 wave_near_peak_col=%2 "
                       "wave_near_peak_sec=%3 wave_near_peak_render_sec=%4 "
                       "wave_near_peak_view_x=%5 wave_near_peak_dx_px=%6 "
                       "wave_near_peak_dt_ms=%7 wave_near_peak_amp=%8")
            .arg(kPeakSearchHalfWindowSeconds * 2000.0, 0, 'f', 0)
            .arg(peakColumn)
            .arg(peakCenterSecond, 0, 'f', 6)
            .arg(peakCenterRenderedSecond, 0, 'f', 6)
            .arg(peakCenterViewX, 0, 'f', 3)
            .arg(peakCenterViewX - playheadViewX, 0, 'f', 3)
            .arg((peakCenterSecond - playheadSecond) * 1000.0, 0, 'f', 3)
            .arg(peakEnergy, 0, 'f', 6);
    } else {
        payload += QStringLiteral(
                       " wave_near_peak_window_ms=%1 wave_near_peak_col=-1")
            .arg(kPeakSearchHalfWindowSeconds * 2000.0, 0, 'f', 0);
    }
    return payload;
}

QString renderMapPrimitivePayload(
    const miacode::timeline::TimelineSceneState& state)
{
    QStringList parts;
    const qreal worldLeft =
        static_cast<qreal>(state.horizontalScrollValue + state.timelineLeft);
    const qreal worldRight =
        static_cast<qreal>(state.horizontalScrollValue + state.viewportSize.width());

    if (!state.waveformBars.isEmpty()) {
        parts.append(renderMapRectPayload(
            QStringLiteral("wave_emit_first"),
            state,
            state.waveformBars.constFirst().rect));
        for (const auto& bar : state.waveformBars) {
            if (bar.rect.right() >= worldLeft && bar.rect.left() <= worldRight) {
                parts.append(renderMapRectPayload(QStringLiteral("wave_view_first"), state, bar.rect));
                break;
            }
        }
    }

    for (const auto& line : state.gridLines) {
        const qreal x = line.start.x();
        if (x >= worldLeft && x <= worldRight) {
            parts.append(renderMapLinePayload(QStringLiteral("grid_view_first"), state, x));
            break;
        }
    }

    for (const auto& sprite : state.noteSprites) {
        const qreal x = sprite.center.x();
        if (x >= worldLeft && x <= worldRight) {
            parts.append(renderMapLinePayload(QStringLiteral("note_sprite_first"), state, x));
            parts.append(QStringLiteral("note_sprite_first_type=%1").arg(sprite.spriteType));
            break;
        }
    }

    return parts.join(QLatin1Char(' '));
}

QString renderMapDataAnchorPayload(
    const miacode::timeline::TimelineSceneState& state,
    const TimelineQuickStateBridge* stateBridge)
{
    if (stateBridge == nullptr) {
        return QString();
    }
    const TimelineRenderSnapshot& snapshot = stateBridge->renderSnapshot();
    QStringList parts;

    const auto measureIt = std::lower_bound(
        snapshot.measureLineSeconds.cbegin(),
        snapshot.measureLineSeconds.cend(),
        state.visibleStartSecond - 1e-6);
    if (measureIt != snapshot.measureLineSeconds.cend()
        && *measureIt <= state.visibleEndSecond + 1e-6) {
        const double second = *measureIt;
        const qreal worldX = renderMapSecondToSceneXExact(state, second);
        parts.append(QStringLiteral(
                         "measure_first_sec=%1 measure_first_world_x=%2 "
                         "measure_first_view_x=%3")
            .arg(second, 0, 'f', 6)
            .arg(worldX, 0, 'f', 3)
            .arg(worldX - static_cast<qreal>(state.horizontalScrollValue), 0, 'f', 3));
    }

    double firstNoteSecond = std::numeric_limits<double>::infinity();
    int firstNoteLine = -1;
    int firstNoteCol = -1;
    int firstNoteLane = -1;
    int firstNoteKind = -1;
    for (const TimelineRenderLine& line : snapshot.lines) {
        if (line.startSecond > state.visibleEndSecond + 1e-6) {
            break;
        }
        if (line.endSecond < state.visibleStartSecond - 1e-6) {
            continue;
        }
        for (const TimelineRenderNote& note : line.notes) {
            const double second = timelineRenderAbsoluteSecond(line, note.secondOffset);
            if (second < state.visibleStartSecond - 1e-6
                || second > state.visibleEndSecond + 1e-6
                || second >= firstNoteSecond) {
                continue;
            }
            firstNoteSecond = second;
            firstNoteLine = line.lineNumber;
            firstNoteCol = note.sourceCol;
            firstNoteLane = note.lane;
            firstNoteKind = static_cast<int>(note.kind);
        }
    }
    if (std::isfinite(firstNoteSecond)) {
        const qreal worldX = renderMapSecondToSceneXExact(state, firstNoteSecond);
        parts.append(QStringLiteral(
                         "data_note_first_sec=%1 data_note_first_world_x=%2 "
                         "data_note_first_view_x=%3 data_note_first_line=%4 "
                         "data_note_first_col=%5 data_note_first_lane=%6 "
                         "data_note_first_kind=%7")
            .arg(firstNoteSecond, 0, 'f', 6)
            .arg(worldX, 0, 'f', 3)
            .arg(worldX - static_cast<qreal>(state.horizontalScrollValue), 0, 'f', 3)
            .arg(firstNoteLine)
            .arg(firstNoteCol)
            .arg(firstNoteLane)
            .arg(firstNoteKind));
    }

    return parts.join(QLatin1Char(' '));
}

void appendTimelineRenderMapDiagnostics(
    const miacode::timeline::TimelineSceneState& state,
    const TimelineQuickStateBridge* stateBridge,
    int scrollBucket,
    quint64 rebuildCount)
{
    if (stateBridge == nullptr || !miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }

    const TimelineRenderSnapshot& snapshot = stateBridge->renderSnapshot();
    QString payload = QStringLiteral(
                          "action=state rebuild_count=%1 bucket=%2 viewport=%3x%4 "
                          "scroll=%5 max_scroll=%6 timeline_left=%7 leading_pad=%8 "
                          "content_width=%9 display_start=%10 display_end=%11 "
                          "visible_start=%12 visible_end=%13 pps=%14 "
                          "revs=grid:%15,wave:%16,header:%17,notes:%18,overlay:%19,dyn:%20 "
                          "counts=wave:%21,grid:%22,notes:%23,tracks:%24,holds:%25,touch:%26,muri:%27 "
                          "lines=%28 measure_lines=%29")
        .arg(rebuildCount)
        .arg(scrollBucket)
        .arg(state.viewportSize.width())
        .arg(state.viewportSize.height())
        .arg(state.horizontalScrollValue)
        .arg(qMax(0, state.contentWidth - state.viewportSize.width()))
        .arg(state.timelineLeft)
        .arg(state.leadingCenteringPadding)
        .arg(state.contentWidth)
        .arg(state.displayStartSeconds, 0, 'f', 6)
        .arg(state.displayEndSeconds, 0, 'f', 6)
        .arg(state.visibleStartSecond, 0, 'f', 6)
        .arg(state.visibleEndSecond, 0, 'f', 6)
        .arg(state.pixelsPerSecond, 0, 'f', 3)
        .arg(state.gridRevision)
        .arg(state.waveformRevision)
        .arg(state.headerRevision)
        .arg(state.notesRevision)
        .arg(state.overlayRevision)
        .arg(state.overlayDynamicRevision)
        .arg(state.waveformBars.size())
        .arg(state.gridLines.size())
        .arg(state.noteSprites.size())
        .arg(state.trackSprites.size())
        .arg(state.holdSpans.size())
        .arg(state.touchHoldLines.size())
        .arg(state.muriDots.size())
        .arg(snapshot.lines.size())
        .arg(snapshot.measureLineSeconds.size());

    const QString waveformPayload = renderMapWaveformPayload(state, stateBridge);
    const QString primitivePayload = renderMapPrimitivePayload(state);
    const QString dataPayload = renderMapDataAnchorPayload(state, stateBridge);
    if (!waveformPayload.isEmpty()) {
        payload += QLatin1Char(' ') + waveformPayload;
    }
    if (!primitivePayload.isEmpty()) {
        payload += QLatin1Char(' ') + primitivePayload;
    }
    if (!dataPayload.isEmpty()) {
        payload += QLatin1Char(' ') + dataPayload;
    }

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/render_map"),
        payload);
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/render_map"),
        renderMapPointPayload(QStringLiteral("playhead"), state, stateBridge->playheadSeconds()));
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/render_map"),
        renderMapPointPayload(QStringLiteral("cursor"), state, stateBridge->cursorSeconds()));
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/render_map"),
        renderMapPointPayload(QStringLiteral("entry"), state, stateBridge->playbackEntrySeconds()));
}

}  // namespace

TimelineQuickItem::TimelineQuickItem(QQuickItem* parent)
    : QQuickItem(parent)
    , textures_(std::make_unique<TimelineQuickTextureCache>())
    , gridLayer_(std::make_unique<TimelineQuickGridLayer>())
    , waveformLayer_(std::make_unique<TimelineQuickWaveformLayer>())
    , headerLayer_(std::make_unique<TimelineQuickHeaderLayer>())
    , gridLinesLayer_(std::make_unique<TimelineQuickGridLinesLayer>())
    , notesLayer_(std::make_unique<TimelineQuickNotesLayer>())
    , overlayLayer_(std::make_unique<TimelineQuickOverlayLayer>())
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFocus(true);
    heldHorizontalKeyScrollTimer_.setSingleShot(false);
    heldHorizontalKeyScrollTimer_.setInterval(kTimelineKeyHoldTickIntervalMs);
    connect(&heldHorizontalKeyScrollTimer_, &QTimer::timeout, this, &TimelineQuickItem::applyHeldHorizontalKeyScrollTick);

    // Phase 3c — DComp tracker placeholder. The TimelineRenderView's
    // tryDiscoverTrackedItem looks up by this objectName via
    // QObject::findChild on the QQuickWindow.  Even when the env flag
    // is off (no view created) we still set the name so a later
    // toggle-on Just Works. Same pattern as PreviewQuickSceneRoot's
    // preview_dcomp_track_target.
    setObjectName(QStringLiteral("timeline_dcomp_track_target"));
    // Phase 3e-diag — force-log every TimelineQuickItem construction so
    // we can identify if QML is creating multiple instances (which
    // would explain the two-popup symptom in the user's log).
    {
        static std::atomic<int> sInstanceCounter{0};
        const int instanceId = ++sInstanceCounter;
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_item"),
            QStringLiteral("action=construct instance=%1 ptr=0x%2")
                .arg(instanceId)
                .arg(reinterpret_cast<quintptr>(this), 0, 16),
            /*force=*/true);
    }
    if (miacode::debug_options::previewTimelineUseDCompEnabled()) {
#ifdef Q_OS_WIN
        dcompView_ = std::make_unique<miacode::preview::dcomp::TimelineRenderView>(this);
        // Attach to the host window once we're parented into a scene,
        // and tell the view we ARE its tracked item — bypassing the
        // findChild-by-objectName dance, which fails in the
        // sceneGraphInitialized → onWindowGeometryChanged path because
        // the QQuickItem is constructed lazily by QML and isn't in
        // the window's child tree at the right moment. The popup
        // would otherwise stay sized to the full QQuickWindow client
        // area, drawing timeline rects/lines at the window's top-left
        // instead of inside the timeline pane.
        dcompWindowConnection_ = connect(
            this, &QQuickItem::windowChanged, this,
            [this](QQuickWindow* w) {
                if (dcompView_ != nullptr) {
                    dcompView_->attachToWindow(w);
                    dcompView_->setTrackedQuickItem(this);
                }
            });
        if (window() != nullptr) {
            dcompView_->attachToWindow(window());
            dcompView_->setTrackedQuickItem(this);
        }
#endif
    }
}

TimelineQuickItem::~TimelineQuickItem()
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/quick_item"),
        QStringLiteral("action=destruct ptr=0x%1")
            .arg(reinterpret_cast<quintptr>(this), 0, 16),
        /*force=*/true);
#ifdef Q_OS_WIN
    if (dcompWindowConnection_) {
        QObject::disconnect(dcompWindowConnection_);
        dcompWindowConnection_ = QMetaObject::Connection();
    }
#endif
    // dcompView_'s unique_ptr destructor handles renderer.stop() +
    // core.shutdown() ordering through TimelineRenderView::~TimelineRenderView.
}

TimelineQuickStateBridge* TimelineQuickItem::stateBridge() const
{
    return stateBridge_;
}

QObject* TimelineQuickItem::stateBridgeObject() const
{
    return stateBridge_;
}

void TimelineQuickItem::setStateBridge(TimelineQuickStateBridge* stateBridge)
{
    if (stateBridge_ == stateBridge) {
        return;
    }
    if (bridgeRenderStateConnection_) {
        QObject::disconnect(bridgeRenderStateConnection_);
    }
    if (bridgePlayheadConnection_) {
        QObject::disconnect(bridgePlayheadConnection_);
    }
    stateBridge_ = stateBridge;
    cachedSceneStateValid_ = false;
    lastPaintedHorizontalScrollValue_ = -1;
    if (stateBridge_ != nullptr) {
        bridgeRenderStateConnection_ =
            connect(stateBridge_, &TimelineQuickStateBridge::renderStateChanged, this, &TimelineQuickItem::syncSourceState);
        bridgePlayheadConnection_ =
            connect(stateBridge_, &TimelineQuickStateBridge::playheadChanged, this, &TimelineQuickItem::playheadChanged);
        stateBridge_->setQuickViewportSize(QSize(qMax(1, qRound(width())), qMax(1, qRound(height()))));
    }
    syncSourceState();
    emit stateBridgeChanged();
}

void TimelineQuickItem::setStateBridgeObject(QObject* stateBridgeObject)
{
    setStateBridge(qobject_cast<TimelineQuickStateBridge*>(stateBridgeObject));
}

int TimelineQuickItem::headerLeftLimit() const
{
    return headerLeftLimit_;
}

void TimelineQuickItem::setHeaderLeftLimit(int value)
{
    const int normalized = qMax(0, value);
    if (headerLeftLimit_ == normalized) {
        return;
    }
    headerLeftLimit_ = normalized;
    ++appearanceRevision_;
    update();
    emit headerInsetsChanged();
}

int TimelineQuickItem::headerRightLimit() const
{
    return headerRightLimit_;
}

void TimelineQuickItem::setHeaderRightLimit(int value)
{
    const int normalized = qMax(0, value);
    if (headerRightLimit_ == normalized) {
        return;
    }
    headerRightLimit_ = normalized;
    ++appearanceRevision_;
    update();
    emit headerInsetsChanged();
}

int TimelineQuickItem::headerMarkerLeftLimit() const
{
    return headerMarkerLeftLimit_;
}

void TimelineQuickItem::setHeaderMarkerLeftLimit(int value)
{
    const int normalized = qMax(0, value);
    if (headerMarkerLeftLimit_ == normalized) {
        return;
    }
    headerMarkerLeftLimit_ = normalized;
    ++appearanceRevision_;
    update();
    emit headerInsetsChanged();
}

int TimelineQuickItem::headerMarkerRightLimit() const
{
    return headerMarkerRightLimit_;
}

void TimelineQuickItem::setHeaderMarkerRightLimit(int value)
{
    const int normalized = qMax(0, value);
    if (headerMarkerRightLimit_ == normalized) {
        return;
    }
    headerMarkerRightLimit_ = normalized;
    ++appearanceRevision_;
    update();
    emit headerInsetsChanged();
}

qreal TimelineQuickItem::zoomScale() const
{
    return cachedZoomScale_;
}

bool TimelineQuickItem::followPreviewEnabled() const
{
    return cachedFollowPreviewEnabled_;
}

void TimelineQuickItem::setFollowPreviewEnabled(bool enabled)
{
    const bool changed = cachedFollowPreviewEnabled_ != enabled;
    if (stateBridge_ != nullptr) {
        // Bridge::setFollowPreviewEnabled emits renderStateChanged,
        // which is connected to syncSourceState (DirectConnection,
        // same thread). syncSourceState observes
        // cachedFollowPreviewEnabled_ != bridge's new value, performs
        // the cached-update + emit + appearanceRevision_ bump +
        // update() call. By the time control returns here, the
        // visual rebuild is already scheduled and the
        // followPreviewEnabledChanged() signal has fired.
        stateBridge_->setFollowPreviewEnabled(enabled);
    }
    if (!changed) {
        return;
    }
    if (cachedFollowPreviewEnabled_ != enabled) {
        cachedFollowPreviewEnabled_ = enabled;
        emit followPreviewEnabledChanged();
    }
    emit followPreviewToggled(enabled);
    update();
}

bool TimelineQuickItem::viewportLockEnabled() const
{
    return cachedViewportLockEnabled_;
}

void TimelineQuickItem::setViewportLockEnabled(bool enabled)
{
    const bool changed = cachedViewportLockEnabled_ != enabled;
    if (stateBridge_ != nullptr) {
        stateBridge_->setViewportLockEnabled(enabled);
    }
    if (!changed) {
        return;
    }
    if (cachedViewportLockEnabled_ != enabled) {
        cachedViewportLockEnabled_ = enabled;
        emit viewportLockEnabledChanged();
    }
    emit viewportLockToggled(enabled);
    update();
}

bool TimelineQuickItem::followProgressEnabled() const
{
    return cachedFollowProgressEnabled_;
}

void TimelineQuickItem::setFollowProgressEnabled(bool enabled)
{
    const bool changed = cachedFollowProgressEnabled_ != enabled;
    if (stateBridge_ != nullptr) {
        stateBridge_->setFollowProgressEnabled(enabled);
    }
    if (!changed) {
        return;
    }
    if (cachedFollowProgressEnabled_ != enabled) {
        cachedFollowProgressEnabled_ = enabled;
        emit followProgressEnabledChanged();
    }
    emit followProgressToggled(enabled);
    update();
}

int TimelineQuickItem::timelineTop() const
{
    return cachedTimelineTop_;
}

bool TimelineQuickItem::isReady() const
{
    return ready_;
}

void TimelineQuickItem::cycleZoomPreset()
{
    if (stateBridge_ == nullptr) {
        return;
    }
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    stateBridge_->cycleZoomPreset(
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0));
}

void TimelineQuickItem::stepZoomPreset(int deltaSteps)
{
    if (stateBridge_ == nullptr || deltaSteps == 0) {
        return;
    }
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    stateBridge_->stepZoomPreset(
        deltaSteps,
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0));
}

void TimelineQuickItem::setZoomScale(qreal scale)
{
    if (stateBridge_ == nullptr) {
        return;
    }
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    stateBridge_->setZoomScaleAnchored(
        static_cast<double>(scale),
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0));
}

void TimelineQuickItem::setZoomControlPressedPart(int part)
{
    const int normalized = qBound(-2, part, 2);
    if (zoomControlPressedPart_ == normalized) {
        return;
    }
    zoomControlPressedPart_ = normalized;
    ++appearanceRevision_;
    update();
}

void TimelineQuickItem::setZoomControlHoveredPart(int part)
{
    const int normalized = qBound(-2, part, 2);
    if (zoomControlHoveredPart_ == normalized) {
        return;
    }
    zoomControlHoveredPart_ = normalized;
    ++appearanceRevision_;
    update();
}

void TimelineQuickItem::setSettingsControlHovered(bool hovered)
{
    if (settingsControlHovered_ == hovered) {
        return;
    }
    settingsControlHovered_ = hovered;
    ++appearanceRevision_;
    update();
}

void TimelineQuickItem::setSettingsControlPressed(bool pressed)
{
    if (settingsControlPressed_ == pressed) {
        return;
    }
    settingsControlPressed_ = pressed;
    ++appearanceRevision_;
    update();
}

void TimelineQuickItem::refreshTheme()
{
    cachedThemeSignature_ = 0;
    cachedThemeSignatureValid_ = false;
    cachedSceneStateValid_ = false;
    ++appearanceRevision_;
    pendingThemeInvalidation_ = true;
    update();
}

void TimelineQuickItem::syncSourceState()
{
    const qreal nextZoom = stateBridge_ != nullptr ? stateBridge_->zoomScale() : 0.5;
    const bool nextFollow = stateBridge_ != nullptr && stateBridge_->followPreviewEnabled();
    const bool nextViewportLock = stateBridge_ != nullptr && stateBridge_->viewportLockEnabled();
    const bool nextProgressFollow = stateBridge_ == nullptr || stateBridge_->followProgressEnabled();
    const int nextTimelineTop = static_cast<int>(currentSceneState().timelineTop);
    // Header-control visuals (zoom% text + follow-check tick + colour)
    // are emitted in TimelineQuickHeaderLayer's staticRoot rebuild,
    // which is gated on `appearanceChanged || gridRevision changed`.
    // Toggling followPreview / zoom triggers a scene-state rebuild
    // (cachedSceneBuildFollowPreviewEnabled_ check at the rebuildNeeded
    // gate) but DOESN'T bump appearanceRevision_, so the QSG layer
    // would keep rendering the previous control state until some
    // unrelated theme/DPR/grid event happened to bump it. Bump it
    // explicitly here so the visual reflects the new state on the
    // next paint pass.
    bool appearanceBumpNeeded = false;
    if (!qFuzzyCompare(cachedZoomScale_ + 1.0, nextZoom + 1.0)) {
        cachedZoomScale_ = nextZoom;
        emit zoomScaleChanged();
        appearanceBumpNeeded = true;
    }
    if (cachedFollowPreviewEnabled_ != nextFollow) {
        cachedFollowPreviewEnabled_ = nextFollow;
        emit followPreviewEnabledChanged();
        appearanceBumpNeeded = true;
    }
    if (cachedViewportLockEnabled_ != nextViewportLock) {
        cachedViewportLockEnabled_ = nextViewportLock;
        emit viewportLockEnabledChanged();
    }
    if (appearanceBumpNeeded) {
        ++appearanceRevision_;
    }
    if (cachedFollowProgressEnabled_ != nextProgressFollow) {
        cachedFollowProgressEnabled_ = nextProgressFollow;
        emit followProgressEnabledChanged();
    }
    if (cachedTimelineTop_ != nextTimelineTop) {
        cachedTimelineTop_ = nextTimelineTop;
        emit sceneMetricsChanged();
    }
    if (!canBecomeReady()) {
        updateReadyState(false);
    }
    update();
    // Phase 3c — also push the latest state to the DComp render view.
    // syncSourceState fires on every renderStateChanged from the bridge,
    // so this is the natural hook for keeping the render view in sync.
    // No-op when the env flag is off (dcompView_ stays nullptr).
    pushSceneStateToDComp();
}

void TimelineQuickItem::pushSceneStateToDComp()
{
#ifdef Q_OS_WIN
    if (dcompView_ == nullptr) {
        return;
    }
    dcompView_->setSceneState(currentSceneState());
#endif
}

void TimelineQuickItem::updateReadyState(bool ready)
{
    if (ready_ == ready) {
        return;
    }
    ready_ = ready;
    if (ready_ && miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_item"),
            QStringLiteral("action=ready_emitted width=%1 height=%2 dpr=%3")
                .arg(width(), 0, 'f', 2)
                .arg(height(), 0, 'f', 2)
                .arg(window() != nullptr ? window()->effectiveDevicePixelRatio() : 0.0, 0, 'f', 2)
        );
    }
    emit readyChanged();
    if (ready_) {
        emit timelineSurfaceReady();
    }
}

bool TimelineQuickItem::canBecomeReady() const
{
    return window() != nullptr && stateBridge_ != nullptr && width() > 0.0 && height() > 0.0;
}

miacode::timeline::TimelineSceneState TimelineQuickItem::currentSceneState() const
{
    if (stateBridge_ == nullptr) {
        return miacode::timeline::TimelineSceneState();
    }
    const QSize viewportSize(qMax(1, qRound(width())), qMax(1, qRound(height())));
    // Phase 7 — bucket size = one viewport. Build emits primitives
    // for [bucket-1, bucket+2] (3 viewports total: bucket itself + 1
    // buffer each side). Scrolling within a bucket reuses the
    // cached state; crossing into a new bucket triggers a rebuild
    // which emits primitives for the new window.
    const int bucketSize = viewportSize.width();
    const int currentScroll = stateBridge_->horizontalScrollValue();
    const int currentScrollBucket =
        bucketSize > 0 ? (currentScroll / bucketSize) : 0;
    const bool rebuildNeeded =
        !cachedSceneStateValid_
        || cachedSceneBuildViewportSize_ != viewportSize
        || cachedScrollBucket_ != currentScrollBucket
        || cachedSceneBuildHeaderLeftLimit_ != headerLeftLimit_
        || cachedSceneBuildHeaderRightLimit_ != headerRightLimit_
        || cachedSceneBuildHeaderMarkerLeftLimit_ != headerMarkerLeftLimit_
        || cachedSceneBuildHeaderMarkerRightLimit_ != headerMarkerRightLimit_
        || cachedSceneBuildAppearanceRevision_ != appearanceRevision_
        || cachedSceneBuildGridRevision_ != stateBridge_->gridRevision()
        || cachedSceneBuildWaveformRevision_ != stateBridge_->waveformRevision()
        || cachedSceneBuildHeaderRevision_ != stateBridge_->headerRevision()
        || cachedSceneBuildNotesRevision_ != stateBridge_->notesRevision()
        || cachedSceneBuildOverlayRevision_ != stateBridge_->overlayRevision()
        // Phase 9d-native polish — header-control state. Without these
        // the native zoom-button text + follow-check tick only update
        // when some other revision happens to bump (e.g., a playback
        // tick), making the click feel unresponsive.
        || cachedSceneBuildFollowPreviewEnabled_ != stateBridge_->followPreviewEnabled()
        || cachedSceneBuildFollowProgressEnabled_ != stateBridge_->followProgressEnabled()
        || !qFuzzyCompare(cachedSceneBuildZoomScale_ + 1.0,
                          stateBridge_->zoomScale() + 1.0)
        || !qFuzzyCompare(cachedSceneBuildContentScale_ + 1.0,
                          stateBridge_->contentScale() + 1.0);
    if (rebuildNeeded && miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_scene"),
            QStringLiteral("action=scene_state_rebuild_begin reason=current_scene_state count=%1")
                .arg(sceneStateRebuildCount_ + 1));
    }
    QElapsedTimer timer;
    if (rebuildNeeded) {
        timer.start();
    }
    miacode::timeline::TimelineSceneBuildRequest request;
    request.snapshot = stateBridge_->renderSnapshot();
    request.waveformData = stateBridge_->waveformData();
    request.muriMarkersByLocation = stateBridge_->muriMarkersByLocation();
    request.muriMarkerTooltips = stateBridge_->muriMarkerTooltips();
    request.viewportSize = viewportSize;
    request.headerLineNumberFont = stateBridge_->headerLineNumberFont();
    request.skinDirectory = stateBridge_->skinDirectory();
    request.horizontalScrollValue = stateBridge_->horizontalScrollValue();
    // Phase 7 — opt into scroll-bucket culling. Builder emits
    // primitives for visible viewport ± bucketSize px (so 3 total
    // viewports of horizontal coverage). Layers will rebuild their
    // QSG children when the bucket-bumped revision in
    // applyDynamicSceneState changes.
    request.horizontalCullPaddingPx = bucketSize;
    request.headerLeftLimit = headerLeftLimit_;
    request.headerRightLimit = headerRightLimit_ > 0 ? headerRightLimit_ : request.viewportSize.width();
    request.headerMarkerLeftLimit = headerMarkerLeftLimit_;
    request.headerMarkerRightLimit =
        headerMarkerRightLimit_ > 0 ? headerMarkerRightLimit_ : request.viewportSize.width();
    request.zoomScale = stateBridge_->zoomScale();
    request.contentScale = stateBridge_->contentScale();
    request.waveformBrightness = stateBridge_->waveformBrightness();
    request.measureLineBrightness = stateBridge_->measureLineBrightness();
    request.waveformPhaseCompensationSeconds = stateBridge_->waveformPhaseCompensationSeconds();
    request.playbackEntrySeconds = stateBridge_->playbackEntrySeconds();
    request.playheadSeconds = stateBridge_->playheadSeconds();
    request.cursorSeconds = stateBridge_->cursorSeconds();
    request.playheadUpperLimitSeconds = stateBridge_->playheadUpperLimitSeconds();
    request.showSlideTracks = stateBridge_->showSlideTracks();
    request.playheadIndicatorSuppressed = stateBridge_->playheadIndicatorSuppressed();
    request.dragActive = dragActive_;
    // Phase 9d-native — header-control state for native rendering of
    // the zoom button in the DComp pipeline.
    request.zoomControlPressedPart = zoomControlPressedPart_;
    request.zoomControlHoveredPart = zoomControlHoveredPart_;
    request.settingsControlHovered = settingsControlHovered_;
    request.settingsControlPressed = settingsControlPressed_;
    request.followPreviewEnabled = stateBridge_->followPreviewEnabled();
    request.followProgressEnabled = stateBridge_->followProgressEnabled();
    request.appearanceRevision = appearanceRevision_;
    request.gridRevision = stateBridge_->gridRevision();
    request.waveformRevision = stateBridge_->waveformRevision();
    request.headerRevision = stateBridge_->headerRevision();
    request.notesRevision = stateBridge_->notesRevision();
    request.overlayRevision = stateBridge_->overlayRevision();
    request.overlayDynamicRevision = overlayDynamicRevisionForState(stateBridge_, dragActive_);
    if (rebuildNeeded) {
        cachedSceneState_ = miacode::timeline::TimelineSceneStateBuilder::build(request);
        cachedSceneStateValid_ = true;
        cachedSceneBuildViewportSize_ = viewportSize;
        cachedScrollBucket_ = currentScrollBucket;
        cachedSceneBuildHeaderLeftLimit_ = headerLeftLimit_;
        cachedSceneBuildHeaderRightLimit_ = headerRightLimit_;
        cachedSceneBuildHeaderMarkerLeftLimit_ = headerMarkerLeftLimit_;
        cachedSceneBuildHeaderMarkerRightLimit_ = headerMarkerRightLimit_;
        cachedSceneBuildAppearanceRevision_ = appearanceRevision_;
        cachedSceneBuildGridRevision_ = stateBridge_->gridRevision();
        cachedSceneBuildWaveformRevision_ = stateBridge_->waveformRevision();
        cachedSceneBuildHeaderRevision_ = stateBridge_->headerRevision();
        cachedSceneBuildNotesRevision_ = stateBridge_->notesRevision();
        cachedSceneBuildOverlayRevision_ = stateBridge_->overlayRevision();
        // Phase 9d-native polish — record header-control state so the
        // next call's rebuildNeeded check can detect a change.
        cachedSceneBuildFollowPreviewEnabled_ = stateBridge_->followPreviewEnabled();
        cachedSceneBuildFollowProgressEnabled_ = stateBridge_->followProgressEnabled();
        cachedSceneBuildZoomScale_ = stateBridge_->zoomScale();
        cachedSceneBuildContentScale_ = stateBridge_->contentScale();
    }
    if (rebuildNeeded && miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_scene"),
            QStringLiteral("action=scene_state_rebuild_end reason=current_scene_state count=%1 elapsed_ms=%2 lines=%3")
                .arg(sceneStateRebuildCount_ + 1)
                .arg(timer.elapsed())
                .arg(stateBridge_->renderSnapshot().lines.size()));
    }
    if (rebuildNeeded) {
        ++sceneStateRebuildCount_;
    }
    miacode::timeline::TimelineSceneState state = cachedSceneState_;
    applyDynamicSceneState(&state, stateBridge_, dragActive_);
    if (miacode::debug_options::previewWaveformAlignmentDiagnosticsEnabled()
        && miacode::debug_options::runtimeDebugOutputEnabled()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const int intervalMs = miacode::debug_options::previewWaveformAlignmentDiagnosticSampleMs();
        if (renderMapLastLogMs_ == 0 || nowMs - renderMapLastLogMs_ >= intervalMs) {
            renderMapLastLogMs_ = nowMs;
            appendTimelineRenderMapDiagnostics(
                state,
                stateBridge_,
                currentScrollBucket,
                sceneStateRebuildCount_);
        }
    }
    return state;
}

double TimelineQuickItem::clampSceneSecond(double second) const
{
    const miacode::timeline::TimelineSceneState& state = currentSceneState();
    return qBound(0.0, second, state.maxNavigableSecond);
}

double TimelineQuickItem::viewportCenterSecondForScroll(int horizontalScrollValue) const
{
    miacode::timeline::TimelineSceneState state = currentSceneState();
    state.horizontalScrollValue = qMax(0, horizontalScrollValue);
    return clampSceneSecond(
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0));
}

bool TimelineQuickItem::playheadNearViewportCenter() const
{
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    return qAbs(
               (stateBridge_ != nullptr ? stateBridge_->playheadSeconds() : 0.0)
               - miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0))
        <= (0.5 / qMax(1.0, state.pixelsPerSecond));
}

void TimelineQuickItem::beginHeldHorizontalKeyScroll(int direction, int key)
{
    if (direction == 0) {
        return;
    }
    heldHorizontalKeyScrollDirection_ = direction > 0 ? 1 : -1;
    heldHorizontalKeyScrollKey_ = key;
    heldHorizontalKeyScrollLastElapsedMs_ = 0;
    heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    heldHorizontalKeyScrollElapsed_.invalidate();
}

void TimelineQuickItem::stopHeldHorizontalKeyScroll(int key)
{
    if (key != 0 && heldHorizontalKeyScrollKey_ != key) {
        return;
    }
    heldHorizontalKeyScrollDirection_ = 0;
    heldHorizontalKeyScrollKey_ = 0;
    heldHorizontalKeyScrollLastElapsedMs_ = 0;
    heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    heldHorizontalKeyScrollElapsed_.invalidate();
    heldHorizontalKeyScrollTimer_.stop();
}

QSGNode* TimelineQuickItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data)
{
    Q_UNUSED(data);

    // Phase 3e — when DComp-exclusive mode is on for the timeline, the
    // TimelineRenderView is the authoritative timeline renderer. This
    // QSG path produces nothing: discard any existing scene-graph
    // subtree and return null so Qt skips this layer entirely. The
    // QQuickItem itself stays alive (so DComp's tracked-item geometry
    // tracking still works), but its bounding rect contributes no
    // pixels to the QSG scene.
    //
    // Mirrors PreviewQuickSceneRoot::updatePaintNode's gate at line
    // 526 (`previewDCompExclusiveEnabled`) — same pattern, same
    // behaviour. Without this gate, both the QSG layers and the DComp
    // pipeline would render the same timeline content into different
    // surfaces, producing the "two timelines" symptom the user
    // observed (one rendered by QML+QSG, one by DComp; whichever DWM
    // composites on top wins visually, with the other showing through
    // transparent regions).
    if (miacode::debug_options::previewTimelineUseDCompEnabled()) {
#ifdef Q_OS_WIN
        if (oldNode != nullptr) {
            delete oldNode;
        }
        updateReadyState(true);
        // Still push state to DComp side as before.
        pushSceneStateToDComp();
        return nullptr;
#endif
    }

    QElapsedTimer paintNodeTimer;
    paintNodeTimer.start();
    if (!canBecomeReady()) {
        auto* root = ensureSlotRoot(oldNode);
        updateReadyState(false);
        return root;
    }

    const QString targetSkinDirectory = stateBridge_ != nullptr ? stateBridge_->skinDirectory() : QString();
    bool resetNodeTreeBeforeTextureInvalidation =
        textures_ != nullptr && textures_->requiresReset(window(), targetSkinDirectory);

    const qreal currentDpr = window()->effectiveDevicePixelRatio();
    if (!qFuzzyCompare(cachedDevicePixelRatio_ + 1.0, currentDpr + 1.0)) {
        cachedDevicePixelRatio_ = currentDpr;
        if (!pendingDprInvalidation_) {
            ++appearanceRevision_;
        }
        pendingDprInvalidation_ = true;
    }
    if (pendingDprInvalidation_ || pendingThemeInvalidation_) {
        resetNodeTreeBeforeTextureInvalidation = true;
    }

    miacode::timeline::TimelineSceneState state = currentSceneState();
    const quint64 themeSignature = timelineThemeSignatureHash(state);
    if (cachedThemeSignatureValid_ && cachedThemeSignature_ != themeSignature) {
        ++appearanceRevision_;
        pendingThemeInvalidation_ = true;
        resetNodeTreeBeforeTextureInvalidation = true;
        state = currentSceneState();
    }
    cachedThemeSignature_ = timelineThemeSignatureHash(state);
    cachedThemeSignatureValid_ = true;

    if (resetNodeTreeBeforeTextureInvalidation && oldNode != nullptr) {
        delete oldNode;
        oldNode = nullptr;
    }
    auto* root = ensureSlotRoot(oldNode);

    textures_->setWindow(window());
    textures_->setSkinDirectory(targetSkinDirectory);
    if (pendingDprInvalidation_) {
        textures_->invalidateDprDependent();
        pendingDprInvalidation_ = false;
        pendingThemeInvalidation_ = false;
    } else if (pendingThemeInvalidation_) {
        textures_->invalidateThemeDependent();
        pendingThemeInvalidation_ = false;
    }

    if (state.horizontalScrollValue != lastPaintedHorizontalScrollValue_
        && miacode::debug_options::timelineHotpathDiagnosticsEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_scene"),
            QStringLiteral("action=content_transform_update scroll=%1 max_scroll=%2")
                .arg(state.horizontalScrollValue)
                .arg(qMax(0, state.contentWidth - state.viewportSize.width())));
    }
    lastPaintedHorizontalScrollValue_ = state.horizontalScrollValue;
    int slotIndex = 0;
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return gridLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return headerLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return gridLinesLayer_->updateNode(oldChild, state, window());
    });
    // Waveform-on-top — the waveform now stacks ABOVE the grid lines (and
    // lane overlays) instead of below them. It is a TRANSLUCENT filled
    // silhouette (alpha baked into timelineWaveStroke), so the bar/note lines
    // show through the waveform and the audio shape reads on top of the grid
    // without hiding it. Still BELOW note sprites + the overlay (playhead/
    // cursor) which follow.
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return waveformLayer_->updateNode(oldChild, state);
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return notesLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return overlayLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateReadyState(true);

    const qint64 elapsedNs = paintNodeTimer.nsecsElapsed();
    ++updatePaintNodeCount_;
    // beta7 leak gauge (probe ④) — count every timeline present so the pause gauge can report how
    // many actually ran during the playback window (a stall = starved RHI deferred-release queue).
    miacode::diag::leak_gauge::noteTimelinePresent();
    updatePaintNodeSumNs_ += elapsedNs;
    if (elapsedNs > updatePaintNodeMaxNs_) updatePaintNodeMaxNs_ = elapsedNs;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (updatePaintNodeLastLogMs_ == 0) {
        updatePaintNodeLastLogMs_ = nowMs;
    } else if (nowMs - updatePaintNodeLastLogMs_ >= 1000) {
        const double avgMs = updatePaintNodeCount_ > 0
            ? static_cast<double>(updatePaintNodeSumNs_)
                / static_cast<double>(updatePaintNodeCount_) / 1.0e6
            : 0.0;
        const double maxMs =
            static_cast<double>(updatePaintNodeMaxNs_) / 1.0e6;
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/quick_scene"),
            QStringLiteral(
                "action=update_paint_node_stats samples=%1 avg_ms=%2 max_ms=%3 "
                "interval_ms=%4")
                .arg(updatePaintNodeCount_)
                .arg(avgMs, 0, 'f', 3)
                .arg(maxMs, 0, 'f', 3)
                .arg(nowMs - updatePaintNodeLastLogMs_));
        updatePaintNodeCount_ = 0;
        updatePaintNodeSumNs_ = 0;
        updatePaintNodeMaxNs_ = 0;
        updatePaintNodeLastLogMs_ = nowMs;
    }

    // beta7 leak gauge (probes 1.1 d_render / 1.2 nodes / 3.1 tex). Fires at most ONCE per pause
    // cycle — only when the GUI pause handler armed a render sample (itself gated on
    // runtimeDebugOutputEnabled). Never per-frame. Splits the per-cycle private-bytes growth into
    // the render-present window (d_render) and reports our live render-thread node + texture-cache
    // counts so a --debug reader can tell whether the 178 MB/cycle climb is OUR accumulation
    // (nodes/tex climb) or Qt-internal RHI deferred release (nodes/tex flat, private_mb climbs).
    {
        qint64 pausePrivBytes = -1;
        quint64 gaugeTxn = 0;
        if (miacode::diag::leak_gauge::takeRenderSample(&pausePrivBytes, &gaugeTxn)) {
            const qint64 presentPrivBytes = miacode::diag::processPrivateBytes();
            const qint64 gpuKb = timelineGpuProcessMemoryKb(window());
            SceneGraphStats total;
            accumulateSceneGraphStats(root, &total);
            // Per-layer breakdown (slot order matches updatePaintNode: grid, header, gridLines,
            // waveform, notes, overlay) — localises a climb to a specific layer.
            int layerNodes[kTimelineLayerSlotCount] = {0};
            qint64 layerGeomKb[kTimelineLayerSlotCount] = {0};
            for (int i = 0; i < kTimelineLayerSlotCount; ++i) {
                SceneGraphStats layerStats;
                accumulateSceneGraphStats(layerSlotAt(root, i), &layerStats);
                layerNodes[i] = layerStats.nodes;
                layerGeomKb[i] = layerStats.geomBytes / 1024;
            }
            int texCount = 0;
            int texPixCount = 0;
            int texHoldCount = 0;
            int texRotCount = 0;
            quint64 texCreateTotal = 0;
            if (textures_) {
                textures_->debugCacheStats(
                    &texCount, &texPixCount, &texHoldCount, &texRotCount, &texCreateTotal);
            }
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("timeline/leak_gauge"),
                QStringLiteral(
                    "txn=%1 priv_present_mb=%2 d_render_kb=%3 gpu_kb=%4 nodes=%5 gbytes_kb=%6 "
                    "geom_create=%7 tex=%8 tex_pix=%9 tex_hold=%10 tex_rot=%11 tex_create=%12 "
                    "layer_nodes=%13 layer_gkb=%14")
                    .arg(gaugeTxn)
                    .arg(presentPrivBytes >= 0 ? presentPrivBytes / (1024 * 1024) : -1)
                    .arg((presentPrivBytes >= 0 && pausePrivBytes >= 0)
                             ? (presentPrivBytes - pausePrivBytes) / 1024
                             : 0)
                    .arg(gpuKb)
                    .arg(total.nodes)
                    .arg(total.geomBytes / 1024)
                    .arg(timelineQuickGeometryCreateTotal())
                    .arg(texCount)
                    .arg(texPixCount)
                    .arg(texHoldCount)
                    .arg(texRotCount)
                    .arg(texCreateTotal)
                    .arg(QStringLiteral("%1,%2,%3,%4,%5,%6")
                             .arg(layerNodes[0]).arg(layerNodes[1]).arg(layerNodes[2])
                             .arg(layerNodes[3]).arg(layerNodes[4]).arg(layerNodes[5]))
                    .arg(QStringLiteral("%1,%2,%3,%4,%5,%6")
                             .arg(layerGeomKb[0]).arg(layerGeomKb[1]).arg(layerGeomKb[2])
                             .arg(layerGeomKb[3]).arg(layerGeomKb[4]).arg(layerGeomKb[5])));
        }
    }
    return root;
}

void TimelineQuickItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        if (stateBridge_ != nullptr) {
            stateBridge_->setQuickViewportSize(newGeometry.size().toSize());
        }
        syncSourceState();
    }
}

void TimelineQuickItem::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QQuickItem::mousePressEvent(event);
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    const double clickSecond = clampSceneSecond(
        miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, event->position().x()));
    const QRectF headerRect(state.timelineLeft, 0.0, width() - state.timelineLeft, 28.0);
    const QRectF bodyRect(state.timelineLeft, state.timelineTop, width() - state.timelineLeft, state.timelineHeight);
    if (headerRect.contains(event->position())) {
        emit timelineUserInteractionStarted();
        emit headerNavigateRequested(clickSecond);
        event->accept();
        return;
    }
    if (!bodyRect.contains(event->position())) {
        QQuickItem::mousePressEvent(event);
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier)) {
        emit timelineUserInteractionStarted();
        emit headerNavigateRequested(clickSecond);
        event->accept();
        return;
    }

    emit timelineUserInteractionStarted();
    if (followProgressEnabled() && !playheadNearViewportCenter()) {
        const double centerSecond = viewportCenterSecondForScroll(
            stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0);
        emit centerNavigateRequested(centerSecond);
    }
    dragActive_ = true;
    dragStartX_ = qRound(event->position().x());
    dragStartScrollValue_ = stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0;
    appendTimelineQuickInteractionLog(
        QStringLiteral("drag_begin"),
        QString("x=%1 scroll_start=%2")
            .arg(dragStartX_)
            .arg(dragStartScrollValue_));
    if (stateBridge_ != nullptr) {
        stateBridge_->focusPlayhead(false);
        if (followProgressEnabled()) {
            stateBridge_->suppressPlayheadIndicator();
        }
    }
    emit timelineDragStarted();
    update();
    event->accept();
}

void TimelineQuickItem::mouseMoveEvent(QMouseEvent* event)
{
    if (event == nullptr || !dragActive_ || stateBridge_ == nullptr) {
        QQuickItem::mouseMoveEvent(event);
        return;
    }
    const int newScroll = dragStartScrollValue_ - (qRound(event->position().x()) - dragStartX_);
    stateBridge_->setHorizontalScrollValue(newScroll);
    const double centerSecond = viewportCenterSecondForScroll(stateBridge_->horizontalScrollValue());
    if (followProgressEnabled()) {
        emit centerNavigateRequested(centerSecond);
    }
    event->accept();
}

void TimelineQuickItem::hoverMoveEvent(QHoverEvent* event)
{
    if (event == nullptr || window() == nullptr) {
        QQuickItem::hoverMoveEvent(event);
        return;
    }
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    QPointF hitPosition = event->position();
    hitPosition.rx() += state.horizontalScrollValue;
    QString tooltipText;
    for (const auto& glyph : state.muriDots) {
        if (!glyph.tooltipText.isEmpty() && glyph.rect.contains(hitPosition)) {
            tooltipText = glyph.tooltipText;
            break;
        }
    }
    if (tooltipText.isEmpty()) {
        QToolTip::hideText();
    } else {
        QToolTip::showText(window()->mapToGlobal(event->scenePosition().toPoint()), tooltipText);
    }
    QQuickItem::hoverMoveEvent(event);
}

void TimelineQuickItem::hoverLeaveEvent(QHoverEvent* event)
{
    QToolTip::hideText();
    QQuickItem::hoverLeaveEvent(event);
}

void TimelineQuickItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event != nullptr && event->button() == Qt::LeftButton && dragActive_) {
        dragActive_ = false;
        if (stateBridge_ != nullptr && followProgressEnabled()) {
            stateBridge_->restorePlayheadIndicator(true);
        }
        const double centerSecond = viewportCenterSecondForScroll(stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0);
        appendTimelineQuickInteractionLog(
            QStringLiteral("drag_end"),
            QString("scroll=%1 center_second=%2")
                .arg(stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0)
                .arg(centerSecond, 0, 'f', 6));
        emit timelineDragFinished(centerSecond);
        update();
        event->accept();
        return;
    }
    QQuickItem::mouseReleaseEvent(event);
}

void TimelineQuickItem::wheelEvent(QWheelEvent* event)
{
    if (event == nullptr || stateBridge_ == nullptr) {
        QQuickItem::wheelEvent(event);
        return;
    }

    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        QQuickItem::wheelEvent(event);
        return;
    }

    forceActiveFocus(Qt::MouseFocusReason);
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    appendTimelineQuickInteractionLog(
        QStringLiteral("wheel_scroll"),
        QString("delta=%1 modifiers=%2 scroll_before=%3")
            .arg(delta)
            .arg(static_cast<int>(event->modifiers()))
            .arg(stateBridge_->horizontalScrollValue()));
    const bool zoomInWheel = miacode::input_shortcut::wheelEventMatchesAnyGesture(
        event,
        stateBridge_->zoomInWheelShortcuts());
    const bool zoomOutWheel = miacode::input_shortcut::wheelEventMatchesAnyGesture(
        event,
        stateBridge_->zoomOutWheelShortcuts());
    if (zoomInWheel || zoomOutWheel) {
        const int steps = qMax(1, qAbs(qRound(static_cast<double>(delta) / 120.0)))
            * (zoomInWheel ? 1 : -1);
        stateBridge_->stepZoomPreset(
            steps,
            miacode::timeline::TimelineSceneStateBuilder::sceneXToSecond(state, width() / 2.0));
        event->accept();
        return;
    }

    emit timelineUserInteractionStarted();
    stateBridge_->focusPlayhead(false);
    stateBridge_->setHorizontalScrollValue(stateBridge_->horizontalScrollValue() - (delta / 2));
    const double centerSecond = viewportCenterSecondForScroll(stateBridge_->horizontalScrollValue());
    appendTimelineQuickInteractionLog(
        QStringLiteral("wheel_scroll_applied"),
        QString("scroll_after=%1 center_second=%2")
            .arg(stateBridge_->horizontalScrollValue())
            .arg(centerSecond, 0, 'f', 6));
    if (followProgressEnabled()) {
        emit timelineWheelNavigateRequested(centerSecond);
    }
    event->accept();
}

void TimelineQuickItem::keyPressEvent(QKeyEvent* event)
{
    if (event != nullptr
        && !event->isAutoRepeat()
        && event->modifiers() == Qt::NoModifier
        && event->key() == Qt::Key_Space) {
        emit previewPlayPauseRequested();
        event->accept();
        return;
    }
    if (event != nullptr
        && stateBridge_ != nullptr
        && event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const int direction = event->key() == Qt::Key_Left ? -1 : 1;
        if (!event->isAutoRepeat()) {
            emit timelineUserInteractionStarted();
            beginHeldHorizontalKeyScroll(direction, event->key());
            heldHorizontalKeyScrollLastElapsedMs_ = 0;
            heldHorizontalKeyScrollElapsed_.restart();
            heldHorizontalKeyScrollTimer_.start();
        }
        event->accept();
        return;
    }
    QQuickItem::keyPressEvent(event);
}

void TimelineQuickItem::keyReleaseEvent(QKeyEvent* event)
{
    if (event != nullptr
        && event->modifiers() == Qt::NoModifier
        && event->key() == Qt::Key_Space) {
        event->accept();
        return;
    }
    if (event != nullptr
        && event->modifiers() == Qt::NoModifier
        && !event->isAutoRepeat()
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        stopHeldHorizontalKeyScroll(event->key());
        event->accept();
        return;
    }
    QQuickItem::keyReleaseEvent(event);
}

void TimelineQuickItem::applyHeldHorizontalKeyScrollTick()
{
    if (stateBridge_ == nullptr
        || heldHorizontalKeyScrollDirection_ == 0
        || heldHorizontalKeyScrollKey_ == 0
        || !heldHorizontalKeyScrollElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(heldHorizontalKeyScrollElapsed_.elapsed());
    const int deltaMs = heldHorizontalKeyScrollLastElapsedMs_ > 0
        ? (elapsedMs - heldHorizontalKeyScrollLastElapsedMs_)
        : kTimelineKeyHoldTickIntervalMs;
    heldHorizontalKeyScrollLastElapsedMs_ = elapsedMs;
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    const double totalPixelDelta =
        (((static_cast<double>(deltaMs > 0 ? deltaMs : 1) / 1000.0)
          * timelineHeldKeyPlaybackRate(heldSeconds, qMax(0.0, stateBridge_->zoomScale() * 2.0))
          * state.pixelsPerSecond * heldHorizontalKeyScrollDirection_))
        + heldHorizontalKeyScrollRemainderPixels_;
    const int pixelDelta = qRound(totalPixelDelta);
    heldHorizontalKeyScrollRemainderPixels_ = totalPixelDelta - static_cast<double>(pixelDelta);
    if (pixelDelta != 0) {
        stateBridge_->setHorizontalScrollValue(stateBridge_->horizontalScrollValue() + pixelDelta);
        appendTimelineQuickInteractionLog(
            QStringLiteral("held_key_scroll"),
            QString("direction=%1 pixel_delta=%2 scroll=%3")
                .arg(heldHorizontalKeyScrollDirection_)
                .arg(pixelDelta)
                .arg(stateBridge_->horizontalScrollValue()));
    }
}
