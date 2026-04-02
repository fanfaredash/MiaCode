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
