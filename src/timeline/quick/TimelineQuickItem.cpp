#include "timeline/quick/TimelineQuickItem.h"

#include "UiText.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGNode>
#include <QToolTip>
#include <QMetaObject>
#include <QtMath>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "common/TimelineThemeConfig.h"
#include "timeline/TimelineSceneStateBuilder.h"
#include "timeline/quick/TimelineQuickGridLayer.h"
#include "timeline/quick/TimelineQuickGridLinesLayer.h"
#include "timeline/quick/TimelineQuickHeaderLayer.h"
#include "timeline/quick/TimelineQuickNotesLayer.h"
#include "timeline/quick/TimelineQuickOverlayLayer.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "timeline/quick/TimelineQuickTextureCache.h"
#include "timeline/quick/TimelineQuickWaveformLayer.h"
#include "render/backend_d3d11/TimelineRenderView.h"

namespace {

constexpr double kTimelineKeyHoldAccelerationPerSecond = 1.0;
constexpr int kTimelineKeyHoldTickIntervalMs = 16;
constexpr int kTimelineLayerSlotCount = 6;  // Beta21-fix7: grid + wave + header + gridLines + notes + overlay

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
    // (TimelineQuickWaveformLayer / Header (grid) / Notes) only
    // rebuild their child node tree when the corresponding revision
    // counter changes. With Phase 7 build-time culling, the cached
    // primitive set is bucket-specific, but the bridge's revisions
    // only bump on data changes (chart edits, waveform load) — not
    // on scroll. Adding the current scroll bucket to each revision
    // here gives the layer a bucket-specific view of the revision
    // that flips when the user scrolls into a new bucket. Combined
    // with the matching cache invalidation in
    // TimelineQuickItem::currentSceneState, the layer rebuilds its
    // children to match the freshly emitted primitives. Within a
    // bucket the bumped revision is constant, so layers happily
    // skip rebuilds and the per-frame cost stays at "set transform".
    if (state->viewportSize.width() > 0) {
        const int bucketSize = state->viewportSize.width();
        const quint64 bucket =
            static_cast<quint64>(state->horizontalScrollValue / bucketSize);
        state->waveformRevision += bucket;
        state->gridRevision += bucket;
        state->notesRevision += bucket;
    }

    state->hasCursorLine = false;
    state->hasPlayheadLine = false;
    state->hasDragCenterLine = false;

    const miacode::timeline::TimelineThemeColors theme = miacode::timeline::timelineThemeColors();
    const int cursorX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneX(*state, stateBridge->cursorSeconds());
    if (cursorX > state->timelineLeft) {
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
    if (dcompWindowConnection_) {
        QObject::disconnect(dcompWindowConnection_);
        dcompWindowConnection_ = QMetaObject::Connection();
    }
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
    if (dcompView_ == nullptr) {
        return;
    }
    dcompView_->setSceneState(currentSceneState());
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
                          stateBridge_->zoomScale() + 1.0);
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
    request.horizontalScrollValue = stateBridge_->horizontalScrollValue();
    // Phase 7 — opt into scroll-bucket culling. Builder emits
    // primitives for visible viewport ± bucketSize px (so 3 total
    // viewports of horizontal coverage). Layers will rebuild their
    // QSG children when the bucket-bumped revision in
    // applyDynamicSceneState changes.
    request.horizontalCullPaddingPx = bucketSize;
    request.headerLeftLimit = headerLeftLimit_;
    request.headerRightLimit = headerRightLimit_ > 0 ? headerRightLimit_ : request.viewportSize.width();
    request.zoomScale = stateBridge_->zoomScale();
    request.playbackEntrySeconds = stateBridge_->playbackEntrySeconds();
    request.playheadSeconds = stateBridge_->playheadSeconds();
    request.cursorSeconds = stateBridge_->cursorSeconds();
    request.playheadUpperLimitSeconds = stateBridge_->playheadUpperLimitSeconds();
    request.showSlideTracks = stateBridge_->showSlideTracks();
    request.playheadIndicatorSuppressed = stateBridge_->playheadIndicatorSuppressed();
    request.dragActive = dragActive_;
    // Phase 9d-native — header-control state for native rendering of
    // the zoom button + follow checkbox in the DComp pipeline.
    request.followPreviewEnabled = stateBridge_->followPreviewEnabled();
    request.followProgressEnabled = stateBridge_->followProgressEnabled();
    // Use the app's UI language selector (UiText::isChineseUi()) — not
    // the OS locale. A user with a Chinese OS who selected English in
    // the preferences would otherwise still see Chinese labels instead of
    // English labels on the timeline header.
    request.isChineseUi = UiText::isChineseUi();
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
        if (oldNode != nullptr) {
            delete oldNode;
        }
        updateReadyState(true);
        // Still push state to DComp side as before.
        pushSceneStateToDComp();
        return nullptr;
    }

    QElapsedTimer paintNodeTimer;
    paintNodeTimer.start();
    auto* root = ensureSlotRoot(oldNode);
    if (!canBecomeReady()) {
        updateReadyState(false);
        return root;
    }
    textures_->setWindow(window());
    const qreal currentDpr = window()->effectiveDevicePixelRatio();
    if (!qFuzzyCompare(cachedDevicePixelRatio_ + 1.0, currentDpr + 1.0)) {
        cachedDevicePixelRatio_ = currentDpr;
        if (!pendingDprInvalidation_) {
            ++appearanceRevision_;
        }
        pendingDprInvalidation_ = true;
    }
    if (pendingDprInvalidation_) {
        textures_->invalidateDprDependent();
        pendingDprInvalidation_ = false;
        pendingThemeInvalidation_ = false;
    } else if (pendingThemeInvalidation_) {
        textures_->invalidateThemeDependent();
        pendingThemeInvalidation_ = false;
    }
    miacode::timeline::TimelineSceneState state = currentSceneState();
    const quint64 themeSignature = timelineThemeSignatureHash(state);
    if (cachedThemeSignatureValid_ && cachedThemeSignature_ != themeSignature) {
        ++appearanceRevision_;
        textures_->invalidateThemeDependent();
        state = currentSceneState();
    }
    cachedThemeSignature_ = timelineThemeSignatureHash(state);
    cachedThemeSignatureValid_ = true;
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
        return waveformLayer_->updateNode(oldChild, state);
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return headerLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    // Beta21-fix7 — grid lines now live in their own slot AFTER both
    // waveformLayer and headerLayer, BEFORE notesLayer. This guarantees
    // bar/note lines render on top of waveform regardless of any QSG
    // opaque-batch reorder, while staying below note sprites per the
    // user's "below notes" instruction.
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return gridLinesLayer_->updateNode(oldChild, state, window());
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
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                    : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
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
