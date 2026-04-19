#include "timeline/quick/TimelineQuickItem.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGNode>
#include <QToolTip>
#include <QMetaObject>
#include <QtMath>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "timeline/TimelineSceneStateBuilder.h"
#include "timeline/quick/TimelineQuickGridLayer.h"
#include "timeline/quick/TimelineQuickHeaderLayer.h"
#include "timeline/quick/TimelineQuickNotesLayer.h"
#include "timeline/quick/TimelineQuickOverlayLayer.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "timeline/quick/TimelineQuickTextureCache.h"
#include "timeline/quick/TimelineQuickWaveformLayer.h"

namespace {

constexpr double kTimelineKeyHoldAccelerationPerSecond = 1.0;
constexpr int kTimelineKeyHoldTickIntervalMs = 16;
constexpr int kTimelineLayerSlotCount = 5;

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

QString timelineThemeSignature(const miacode::timeline::TimelineSceneState& state)
{
    QString themeSignature;
    for (const auto& label : state.laneLabels) {
        themeSignature += label.font.toString();
        themeSignature += label.color.name(QColor::HexArgb);
    }
    for (const auto& label : state.headerLabels) {
        themeSignature += label.font.toString();
        themeSignature += label.color.name(QColor::HexArgb);
    }
    for (const auto& marker : state.headerMarkers) {
        themeSignature += marker.color.name(QColor::HexArgb);
    }
    if (state.hasEntryMarker) {
        themeSignature += state.entryMarker.color.name(QColor::HexArgb);
    }
    return themeSignature;
}

}  // namespace

TimelineQuickItem::TimelineQuickItem(QQuickItem* parent)
    : QQuickItem(parent)
    , textures_(std::make_unique<TimelineQuickTextureCache>())
    , gridLayer_(std::make_unique<TimelineQuickGridLayer>())
    , waveformLayer_(std::make_unique<TimelineQuickWaveformLayer>())
    , headerLayer_(std::make_unique<TimelineQuickHeaderLayer>())
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
}

TimelineQuickItem::~TimelineQuickItem()
 = default;

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
    cachedThemeSignature_.clear();
    ++appearanceRevision_;
    pendingThemeInvalidation_ = true;
    update();
}

void TimelineQuickItem::syncSourceState()
{
    const qreal nextZoom = stateBridge_ != nullptr ? stateBridge_->zoomScale() : 0.5;
    const bool nextFollow = stateBridge_ != nullptr && stateBridge_->followPreviewEnabled();
    const int nextTimelineTop = static_cast<int>(currentSceneState().timelineTop);
    if (!qFuzzyCompare(cachedZoomScale_ + 1.0, nextZoom + 1.0)) {
        cachedZoomScale_ = nextZoom;
        emit zoomScaleChanged();
    }
    if (cachedFollowPreviewEnabled_ != nextFollow) {
        cachedFollowPreviewEnabled_ = nextFollow;
        emit followPreviewEnabledChanged();
    }
    if (cachedTimelineTop_ != nextTimelineTop) {
        cachedTimelineTop_ = nextTimelineTop;
        emit sceneMetricsChanged();
    }
    if (!canBecomeReady()) {
        updateReadyState(false);
    }
    update();
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
    miacode::timeline::TimelineSceneBuildRequest request;
    request.snapshot = stateBridge_->renderSnapshot();
    request.waveformData = stateBridge_->waveformData();
    request.muriMarkerLocationIds = stateBridge_->muriMarkerLocationIds();
    request.muriMarkerTooltips = stateBridge_->muriMarkerTooltips();
    request.viewportSize = QSize(qMax(1, qRound(width())), qMax(1, qRound(height())));
    request.headerLineNumberFont = stateBridge_->headerLineNumberFont();
    request.horizontalScrollValue = stateBridge_->horizontalScrollValue();
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
    request.appearanceRevision = appearanceRevision_;
    request.gridRevision = stateBridge_->gridRevision();
    request.waveformRevision = stateBridge_->waveformRevision();
    request.headerRevision = stateBridge_->headerRevision();
    request.notesRevision = stateBridge_->notesRevision();
    request.overlayRevision = stateBridge_->overlayRevision();
    return miacode::timeline::TimelineSceneStateBuilder::build(request);
}

double TimelineQuickItem::clampSceneSecond(double second) const
{
    const miacode::timeline::TimelineSceneState state = currentSceneState();
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
    const QString themeSignature = timelineThemeSignature(state);
    if (!cachedThemeSignature_.isEmpty() && cachedThemeSignature_ != themeSignature) {
        ++appearanceRevision_;
        pendingThemeInvalidation_ = true;
        textures_->invalidateThemeDependent();
        pendingThemeInvalidation_ = false;
        state = currentSceneState();
    }
    cachedThemeSignature_ = timelineThemeSignature(state);
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
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return notesLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateLayerSlot(layerSlotAt(root, slotIndex++), [&](QSGNode* oldChild) {
        return overlayLayer_->updateNode(oldChild, state, window(), textures_.get());
    });
    updateReadyState(true);
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
    if (!playheadNearViewportCenter()) {
        const double centerSecond = viewportCenterSecondForScroll(stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0);
        if (stateBridge_ != nullptr) {
            stateBridge_->setPlayheadSeconds(centerSecond, false);
        }
        emit centerNavigateRequested(centerSecond);
    }
    dragActive_ = true;
    dragStartX_ = qRound(event->position().x());
    dragStartScrollValue_ = stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0;
    if (stateBridge_ != nullptr) {
        stateBridge_->focusPlayhead(false);
        stateBridge_->suppressPlayheadIndicator();
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
    stateBridge_->setPlayheadSeconds(centerSecond, false);
    emit centerNavigateRequested(centerSecond);
    event->accept();
}

void TimelineQuickItem::hoverMoveEvent(QHoverEvent* event)
{
    if (event == nullptr || window() == nullptr) {
        QQuickItem::hoverMoveEvent(event);
        return;
    }
    const miacode::timeline::TimelineSceneState state = currentSceneState();
    QString tooltipText;
    for (const auto& glyph : state.muriDots) {
        if (!glyph.tooltipText.isEmpty() && glyph.rect.contains(event->position())) {
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
        if (stateBridge_ != nullptr) {
            stateBridge_->restorePlayheadIndicator(true);
        }
        const double centerSecond = viewportCenterSecondForScroll(stateBridge_ != nullptr ? stateBridge_->horizontalScrollValue() : 0);
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
    if (!playheadNearViewportCenter()) {
        const double centerSecond = viewportCenterSecondForScroll(stateBridge_->horizontalScrollValue());
        stateBridge_->setPlayheadSeconds(centerSecond, false);
        emit centerNavigateRequested(centerSecond);
    }
    stateBridge_->suppressPlayheadIndicator();
    stateBridge_->setHorizontalScrollValue(stateBridge_->horizontalScrollValue() - (delta / 2));
    const double centerSecond = viewportCenterSecondForScroll(stateBridge_->horizontalScrollValue());
    stateBridge_->setPlayheadSeconds(centerSecond, false);
    emit centerNavigateRequested(centerSecond);
    stateBridge_->restorePlayheadIndicator(false);
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
    }
}
