void TimelineView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    layoutHeaderButtons();
    updateHorizontalRange();
}

void TimelineView::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const QRect headerRect(timelineLeft(), 0, viewport()->width() - timelineLeft(), kHeaderHeight);
    if (headerRect.contains(event->position().toPoint())) {
        const double maxSecond = playheadUpperLimitSeconds_ > 0.0
            ? playheadUpperLimitSeconds_
            : qMax(durationSeconds_, playheadSeconds_);
        double second = qMax(0.0, xToSecond(qRound(event->position().x())));
        if (maxSecond > 0.0) {
            second = qMin(second, maxSecond);
        }
        emit headerNavigateRequested(second);
        event->accept();
        return;
    }

    const QRect timelineRect(timelineLeft(), timelineTop(), viewport()->width() - timelineLeft(), timelineHeight());
    if (!timelineRect.contains(event->position().toPoint())) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (hasTimelineNavigateModifier(event->modifiers())) {
        if (const TimelineNoteMarker* note = nearestNoteForViewportPos(event->position())) {
            emit noteNavigateRequested(note->sourceLine, note->sourceCol);
        }
        event->accept();
        return;
    }

    timelineDragActive_ = true;
    timelineDragStartX_ = qRound(event->position().x());
    timelineDragStartScrollValue_ = horizontalScrollBar()->value();
    viewport()->setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void TimelineView::mouseMoveEvent(QMouseEvent* event)
{
    if (event != nullptr && timelineDragActive_) {
        const int deltaX = qRound(event->position().x()) - timelineDragStartX_;
        horizontalScrollBar()->setValue(timelineDragStartScrollValue_ - deltaX);
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
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TimelineView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (delta / 2));
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

