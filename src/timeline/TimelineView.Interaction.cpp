void TimelineView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    layoutHeaderButtons();
    updateHorizontalRange();
}

void TimelineView::keyPressEvent(QKeyEvent* event)
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
        && event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        const int direction = event->key() == Qt::Key_Left ? -1 : 1;
        if (!event->isAutoRepeat()) {
            emit timelineUserInteractionStarted();
            beginHeldHorizontalKeyScroll(direction, event->key());
            QAbstractScrollArea::keyPressEvent(event);
            return;
        }
        if (heldHorizontalKeyScrollKey_ != event->key()) {
            event->accept();
            return;
        }
        if (heldHorizontalKeyScrollTimer_ != nullptr && !heldHorizontalKeyScrollTimer_->isActive()) {
            heldHorizontalKeyScrollLastElapsedMs_ = 0;
            heldHorizontalKeyScrollElapsed_.restart();
            heldHorizontalKeyScrollTimer_->start();
            QAbstractScrollArea::keyPressEvent(event);
        }
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void TimelineView::keyReleaseEvent(QKeyEvent* event)
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
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
        && heldHorizontalKeyScrollKey_ == event->key()) {
        stopHeldHorizontalKeyScroll(event->key());
        event->accept();
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(event);
}

void TimelineView::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const auto clickSecondForX = [this](qreal x) {
        double second = qMax(0.0, xToSecond(qRound(x)));
        const double maxSecond = playheadUpperLimitSeconds_ > 0.0
            ? playheadUpperLimitSeconds_
            : maxNavigableSecond();
        if (maxSecond > 0.0) {
            second = qMin(second, maxSecond);
        }
        return second;
    };

    const QRect headerRect(timelineLeft(), 0, viewport()->width() - timelineLeft(), kHeaderHeight);
    if (headerRect.contains(event->position().toPoint())) {
        setFocus(Qt::MouseFocusReason);
        emit timelineUserInteractionStarted();
        emit headerNavigateRequested(clickSecondForX(event->position().x()));
        event->accept();
        return;
    }

    const QRect timelineRect(timelineLeft(), timelineTop(), viewport()->width() - timelineLeft(), timelineHeight());
    if (!timelineRect.contains(event->position().toPoint())) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (hasTimelineNavigateModifier(event->modifiers())) {
        setFocus(Qt::MouseFocusReason);
        emit timelineUserInteractionStarted();
        emit headerNavigateRequested(clickSecondForX(event->position().x()));
        event->accept();
        return;
    }

    setFocus(Qt::MouseFocusReason);
    emit timelineUserInteractionStarted();
    focusPlayhead(false);
    if (!playheadNearViewportCenter()) {
        const double centerSecond = viewportCenterSecond();
        setPlayheadSeconds(centerSecond, false);
        emit centerNavigateRequested(centerSecond);
    }
    timelineDragActive_ = true;
    timelineDragStartX_ = qRound(event->position().x());
    timelineDragStartScrollValue_ = horizontalScrollBar()->value();
    viewport()->setCursor(Qt::ClosedHandCursor);
    suppressPlayheadIndicatorForInteraction();
    emit timelineDragStarted();
    event->accept();
}

void TimelineView::mouseMoveEvent(QMouseEvent* event)
{
    if (event != nullptr && timelineDragActive_) {
        const int deltaX = qRound(event->position().x()) - timelineDragStartX_;
        horizontalScrollBar()->setValue(timelineDragStartScrollValue_ - deltaX);
        updatePlayheadToViewportCenter();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event != nullptr && event->button() == Qt::LeftButton && timelineDragActive_) {
        timelineDragActive_ = false;
        viewport()->unsetCursor();
        restorePlayheadIndicatorAfterInteraction(true);
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TimelineView::wheelEvent(QWheelEvent* event)
{
    if (handleAltZoomWheel(event)) {
        setFocus(Qt::MouseFocusReason);
        return;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta != 0) {
        setFocus(Qt::MouseFocusReason);
        emit timelineUserInteractionStarted();
        focusPlayhead(false);
        if (!playheadNearViewportCenter()) {
            const double centerSecond = viewportCenterSecond();
            setPlayheadSeconds(centerSecond, false);
            emit centerNavigateRequested(centerSecond);
        }
        suppressPlayheadIndicatorForInteraction();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (delta / 2));
        updatePlayheadToViewportCenter();
        restorePlayheadIndicatorAfterInteraction();
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void TimelineView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    // This view has fixed (non-scrolling) painted regions on the left/header.
    // Force full repaint to avoid stale artifacts from scroll blit optimization.
    viewport()->update();
}

bool TimelineView::stepViewportBySeconds(double deltaSeconds)
{
    if (!qIsFinite(deltaSeconds) || deltaSeconds == 0.0) {
        return false;
    }
    QScrollBar* hbar = horizontalScrollBar();
    if (hbar == nullptr) {
        return false;
    }

    const double totalPixelDelta = (deltaSeconds * pixelsPerSecond_) + heldHorizontalKeyScrollRemainderPixels_;
    const int pixelDelta = qRound(totalPixelDelta);
    heldHorizontalKeyScrollRemainderPixels_ = totalPixelDelta - static_cast<double>(pixelDelta);
    if (pixelDelta == 0) {
        return true;
    }

    hbar->setValue(qBound(hbar->minimum(), hbar->value() + pixelDelta, hbar->maximum()));
    return true;
}

void TimelineView::beginHeldHorizontalKeyScroll(int direction, int key)
{
    if (direction == 0) {
        return;
    }
    if (heldHorizontalKeyScrollTimer_ != nullptr) {
        heldHorizontalKeyScrollTimer_->stop();
    }
    heldHorizontalKeyScrollDirection_ = direction > 0 ? 1 : -1;
    heldHorizontalKeyScrollKey_ = key;
    heldHorizontalKeyScrollLastElapsedMs_ = 0;
    heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    heldHorizontalKeyScrollElapsed_.invalidate();
}

void TimelineView::stopHeldHorizontalKeyScroll(int key)
{
    if (key != 0 && heldHorizontalKeyScrollKey_ != key) {
        return;
    }
    heldHorizontalKeyScrollDirection_ = 0;
    heldHorizontalKeyScrollKey_ = 0;
    heldHorizontalKeyScrollLastElapsedMs_ = 0;
    heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    heldHorizontalKeyScrollElapsed_.invalidate();
    if (heldHorizontalKeyScrollTimer_ != nullptr) {
        heldHorizontalKeyScrollTimer_->stop();
    }
}

void TimelineView::applyHeldHorizontalKeyScrollTick()
{
    if (heldHorizontalKeyScrollDirection_ == 0
        || heldHorizontalKeyScrollKey_ == 0
        || !heldHorizontalKeyScrollElapsed_.isValid()) {
        return;
    }

    const int elapsedMs = static_cast<int>(heldHorizontalKeyScrollElapsed_.elapsed());
    const int deltaMs = heldHorizontalKeyScrollLastElapsedMs_ > 0
        ? (elapsedMs - heldHorizontalKeyScrollLastElapsedMs_)
        : kTimelineKeyHoldTickIntervalMs;
    heldHorizontalKeyScrollLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    const double maxPlaybackRate = qMax(0.0, zoomScale() * 2.0);
    const double deltaSeconds = (static_cast<double>(deltaMs > 0 ? deltaMs : 1) / 1000.0)
        * timelineHeldKeyPlaybackRate(heldSeconds, maxPlaybackRate);
    stepViewportBySeconds(static_cast<double>(heldHorizontalKeyScrollDirection_) * deltaSeconds);
}
