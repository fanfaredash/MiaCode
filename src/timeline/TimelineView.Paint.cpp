void TimelineView::paintEvent(QPaintEvent* event)
{
    const UiTheme::Colors& c = UiTheme::colors();
    const QRect dirtyRect = event != nullptr ? event->rect() : viewport()->rect();

    QPainter painter(viewport());
    if (!painter.isActive()) {
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(dirtyRect);
    painter.fillRect(dirtyRect, c.timelineWindow);

    const int left = timelineLeft();
    const int top = timelineTop();
    const int h = timelineHeight();
    const int laneH = laneHeight();
    const int xOffset = horizontalScrollBar()->value();
    const bool drawSlideTracks = effectiveShowSlideTracks();
    const QRect timelineRect(left, top, viewport()->width() - left, h);
    VisibleLineRange beatRange;
    VisibleLineRange noteRange;
    if (dirtyRect.right() >= left) {
        const int dirtyRight = qMin(viewport()->width(), dirtyRect.right() + 1);
        const double visibleStartSecond = xToSecond(qMax(left, dirtyRect.left()));
        const double visibleEndSecond = xToSecond(qMax(left, dirtyRight));
        beatRange = visibleLineRange(visibleStartSecond - 1.0, visibleEndSecond + 1.0);
        if (dirtyRect.bottom() >= top && dirtyRect.top() <= top + h) {
            noteRange = visibleLineRange(visibleStartSecond - 2.0, visibleEndSecond + 2.0);
        }
    }

    const auto noteSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return timelineRenderAbsoluteSecond(line, note.secondOffset);
    };
    const auto noteEndSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return note.endSecondOffset >= 0.0 ? timelineRenderAbsoluteSecond(line, note.endSecondOffset) : -1.0;
    };
    const auto noteTraceSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return note.slideTraceSecondOffset >= 0.0 ? timelineRenderAbsoluteSecond(line, note.slideTraceSecondOffset) : -1.0;
    };

    painter.fillRect(QRect(0, 0, viewport()->width(), top), c.timelineHeader);
    painter.fillRect(QRect(0, top, left, h), c.timelineSidebar);
    painter.fillRect(timelineRect, c.timelineBase);
    painter.setPen(c.timelineBorder);
    painter.drawLine(0, top - 1, viewport()->width(), top - 1);

    QFont laneLabelFont(QStringLiteral("Consolas"));
    laneLabelFont.setStyleHint(QFont::Monospace);
    laneLabelFont.setPointSize(10);
    laneLabelFont.setWeight(QFont::DemiBold);

    if (!waveformPeaks_.isEmpty() && waveformDurationSeconds_ > 0.0) {
        painter.save();
        painter.setClipRect(QRect(left, top, viewport()->width() - left, h));
        QPainterPath waveformPath;
        const qreal centerY = top + h / 2.0;
        const qreal maxAmplitude = qMax<qreal>(8.0, h / 2.0 - 8.0);
        const int sampleCount = waveformPeaks_.size();
        bool started = false;
        for (int i = 0; i < sampleCount; ++i) {
            const qreal peak = qBound<qreal>(0.0, waveformPeaks_.at(i), 1.0);
            const double second = waveformStartSeconds_
                + waveformDurationSeconds_ * (static_cast<double>(i) / qMax(1, sampleCount - 1));
            const qreal x = secondToX(second) - xOffset;
            const qreal y = centerY - peak * maxAmplitude;
            if (!started) {
                waveformPath.moveTo(x, y);
                started = true;
            } else {
                waveformPath.lineTo(x, y);
            }
        }
        for (int i = sampleCount - 1; i >= 0; --i) {
            const qreal peak = qBound<qreal>(0.0, waveformPeaks_.at(i), 1.0);
            const double second = waveformStartSeconds_
                + waveformDurationSeconds_ * (static_cast<double>(i) / qMax(1, sampleCount - 1));
            const qreal x = secondToX(second) - xOffset;
            const qreal y = centerY + peak * maxAmplitude;
            waveformPath.lineTo(x, y);
        }
        waveformPath.closeSubpath();
        painter.fillPath(waveformPath, c.timelineWaveFill);
        painter.setPen(QPen(c.timelineWaveStroke, 1.0));
        painter.drawPath(waveformPath);
        painter.restore();
    }

    painter.setFont(laneLabelFont);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const int y = top + lane * laneH;
        const QColor rowColor = (lane % 2 == 0) ? c.timelineLaneEven : c.timelineLaneOdd;
        painter.fillRect(QRect(left, y, viewport()->width() - left, laneH), rowColor);
        painter.setPen(c.timelineBorder);
        painter.drawLine(0, y + laneH, viewport()->width(), y + laneH);
        painter.setPen(c.timelineLabel);
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }

    painter.setPen(c.timelineAxis);
    painter.drawLine(left, top - 1, left, top + h);

    int lastLabelScreenX = -1000000;
    int headerLeftLimit = 0;
    if (zoomButton_ != nullptr) {
        headerLeftLimit = qMax(headerLeftLimit, zoomButton_->x() + zoomButton_->width() + 8);
    }
    int headerRightLimit = viewport()->width() - 4;
    if (followPreviewCheckBox_ != nullptr) {
        headerRightLimit = qMin(headerRightLimit, followPreviewCheckBox_->x() - 8);
    }

    for (int lineIndex = beatRange.begin; lineIndex < beatRange.end; ++lineIndex) {
        const TimelineRenderLine& line = lines_.at(lineIndex);
        for (const TimelineRenderBeat& marker : line.beats) {
            const double absoluteSecond = timelineRenderAbsoluteSecond(line, marker.secondOffset);
            const int x = secondToX(absoluteSecond) - xOffset;
            if (x < left - 1 || x > viewport()->width()) {
                continue;
            }
            painter.setPen(marker.major ? c.timelineGridMajor : c.timelineGridMinor);
            painter.drawLine(x, top, x, top + h);
            if (marker.major) {
                const QString labelText = QString::number(line.lineNumber);
                const int labelWidth = 56;
                const int labelX = x - (labelWidth / 2);
                if (labelX >= headerLeftLimit
                    && labelX + labelWidth <= headerRightLimit
                    && labelX - lastLabelScreenX >= 22) {
                    painter.setFont(headerLineNumberFont_);
                    painter.setPen(c.textSecondary);
                    painter.drawText(labelX, 0, labelWidth, top, Qt::AlignHCenter | Qt::AlignVCenter, labelText);
                    painter.setFont(laneLabelFont);
                    lastLabelScreenX = labelX;
                }
            }
        }
    }

    for (int lineIndex = noteRange.begin; lineIndex < noteRange.end; ++lineIndex) {
        const TimelineRenderLine& line = lines_.at(lineIndex);
        for (const TimelineRenderNote& note : line.notes) {
            if (!timelineRenderFlagSet(note, TimelineRenderFlagIsFirework)) {
                continue;
            }
            if (note.kind != TimelineRenderNoteKind::Touch && note.kind != TimelineRenderNoteKind::TouchHold) {
                continue;
            }

            const double triggerSecond = (note.kind == TimelineRenderNoteKind::TouchHold && noteEndSecond(line, note) > noteSecond(line, note))
                ? noteEndSecond(line, note)
                : noteSecond(line, note);
            const double endSecond = triggerSecond + kTimelineFireworkDurationSeconds;
            const int fireX0 = secondToX(triggerSecond) - xOffset;
            const int fireX1 = secondToX(endSecond) - xOffset;
            const int fireLeft = qMin(fireX0, fireX1);
            const int fireRight = qMax(fireX0, fireX1);
            if (fireRight < left || fireLeft > viewport()->width()) {
                continue;
            }

            const int lane = qBound(1, note.lane, kLaneCount);
            const int rowTop = top + (lane - 1) * laneH;
            const QRectF fireRect(
                static_cast<qreal>(fireLeft),
                static_cast<qreal>(rowTop + 2),
                qMax<qreal>(1.0, static_cast<qreal>(fireRight - fireLeft)),
                qMax<qreal>(1.0, static_cast<qreal>(laneH - 4))
            );
            painter.save();
            painter.setClipRect(fireRect);
            const qreal rowHeight = qMax<qreal>(1.0, fireRect.height());
            const qreal bandHeight = rowHeight / static_cast<qreal>(kTimelineFireworkBandColors.size());
            for (int bandIndex = 0; bandIndex < static_cast<int>(kTimelineFireworkBandColors.size()); ++bandIndex) {
                const qreal bandTop = fireRect.top() + bandHeight * static_cast<qreal>(bandIndex);
                QRectF bandRect(
                    fireRect.left(),
                    bandTop,
                    fireRect.width(),
                    (bandIndex + 1 == static_cast<int>(kTimelineFireworkBandColors.size()))
                        ? (fireRect.bottom() - bandTop + 1.0)
                        : bandHeight
                );
                const QColor base = kTimelineFireworkBandColors.at(bandIndex);
                QLinearGradient alphaGrad(bandRect.topLeft(), bandRect.topRight());
                QColor leftColor = base;
                leftColor.setAlpha(190);
                QColor rightColor = base;
                rightColor.setAlpha(28);
                alphaGrad.setColorAt(0.0, leftColor);
                alphaGrad.setColorAt(1.0, rightColor);
                painter.fillRect(bandRect, alphaGrad);
            }
            painter.restore();
        }
    }

    for (int lineIndex = noteRange.end - 1; lineIndex >= noteRange.begin; --lineIndex) {
        const TimelineRenderLine& line = lines_.at(lineIndex);
        for (int noteIndex = line.notes.size() - 1; noteIndex >= 0; --noteIndex) {
            const TimelineRenderNote& note = line.notes.at(noteIndex);
            if (note.lane < 1 || note.lane > kLaneCount) {
                continue;
            }

            const double startSecond = noteSecond(line, note);
            const double endSecond = noteEndSecond(line, note);
            const double traceSecond = noteTraceSecond(line, note);
            const bool isHold = note.kind == TimelineRenderNoteKind::Hold && endSecond > startSecond;
            const bool isTouchHold = note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond;
            const bool isSlideLike = note.kind == TimelineRenderNoteKind::Slide || note.kind == TimelineRenderNoteKind::Wifi;
            const bool isSlideTrack = isSlideLike && traceSecond > startSecond && endSecond > traceSecond;

            const int x = secondToX(startSecond) - xOffset;
            const int holdEndX = isHold ? (secondToX(endSecond) - xOffset) : x;
            const int slideStartX = isSlideTrack ? (secondToX(traceSecond) - xOffset) : x;
            const int slideEndX = isSlideTrack ? (secondToX(endSecond) - xOffset) : x;
            int extentLeft = x;
            int extentRight = x;
            if (isHold || isTouchHold) {
                const int endX = secondToX(endSecond) - xOffset;
                extentLeft = qMin(extentLeft, endX);
                extentRight = qMax(extentRight, endX);
            }
            if (drawSlideTracks && isSlideTrack) {
                extentLeft = qMin(extentLeft, qMin(slideStartX, slideEndX));
                extentRight = qMax(extentRight, qMax(slideStartX, slideEndX));
            }
            if (extentRight < left - kNoteSize || extentLeft > viewport()->width() + kNoteSize) {
                continue;
            }

            const bool hasMuriWarning = muriMarkerLocationIds_.contains(timelineRenderLocationId(line, note));

            const int rowTop = top + (note.lane - 1) * laneH;
            const int rowCenterY = rowTop + (laneH / 2);

            QString iconType;
            switch (note.kind) {
            case TimelineRenderNoteKind::Tap:
                if (timelineRenderFlagSet(note, TimelineRenderFlagIsEx)) {
                    iconType = QStringLiteral("tap_ex");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)) {
                    iconType = QStringLiteral("tap_break");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagIsEach)) {
                    iconType = QStringLiteral("tap_each");
                } else {
                    iconType = QStringLiteral("tap");
                }
                break;
            case TimelineRenderNoteKind::Hold:
                if (timelineRenderFlagSet(note, TimelineRenderFlagIsEx)) {
                    iconType = QStringLiteral("hold_ex");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)) {
                    iconType = QStringLiteral("hold_break");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagIsEach)) {
                    iconType = QStringLiteral("hold_each");
                } else {
                    iconType = QStringLiteral("hold");
                }
                break;
            case TimelineRenderNoteKind::Touch:
                iconType = timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)
                    ? QStringLiteral("touch_break")
                    : (timelineRenderFlagSet(note, TimelineRenderFlagIsEach) ? QStringLiteral("touch_each") : QStringLiteral("touch"));
                break;
            case TimelineRenderNoteKind::TouchHold:
                iconType = timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)
                    ? QStringLiteral("touch_hold_break")
                    : (timelineRenderFlagSet(note, TimelineRenderFlagIsEach) ? QStringLiteral("touch_hold_each")
                                                                             : QStringLiteral("touch_hold"));
                break;
            case TimelineRenderNoteKind::Slide:
            case TimelineRenderNoteKind::Wifi:
                if (timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak) && timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide)) {
                    iconType = QStringLiteral("star_break_double");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak)) {
                    iconType = QStringLiteral("star_break");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagHeadEx)
                           && timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide)) {
                    iconType = QStringLiteral("star_ex_double");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagHeadEx)) {
                    iconType = QStringLiteral("star_ex");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagHeadEach)
                           && timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide)) {
                    iconType = QStringLiteral("star_each_double");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide)) {
                    iconType = QStringLiteral("star_double");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagHeadEach)) {
                    iconType = QStringLiteral("star_each");
                } else {
                    iconType = QStringLiteral("slide");
                }
                break;
            default:
                iconType = QStringLiteral("tap");
                break;
            }

            const qreal iconScale = zoomScale() <= 0.25 ? 0.5 : 1.0;
            const QPixmap& icon = transformedIconForType(iconType, iconScale);

            if (isHold) {
                const HoldPixmapParts& holdParts = holdPixmapPartsForType(iconType, iconScale);
                if (!holdParts.cap.isNull() && !holdParts.bodySlice.isNull()) {
                    const int capY = rowTop + (laneH - holdParts.cap.height()) / 2;
                    const int leftCapX = extentLeft - holdParts.cap.width() / 2;
                    const int rightCapX = extentRight - holdParts.cap.width() / 2;
                    const int splitX = holdParts.leftHalf.width();
                    const int bodyStartX = leftCapX + splitX - 1;
                    const int bodyEndX = rightCapX + splitX + 1;
                    const int bodyWidth = qMax(0, bodyEndX - bodyStartX);
                    if (bodyWidth > 0) {
                        painter.drawImage(
                            QRect(bodyStartX, capY, bodyWidth, holdParts.bodySlice.height()),
                            holdParts.bodySlice,
                            QRect(0, 0, 1, holdParts.bodySlice.height())
                        );
                    }
                    painter.drawPixmap(leftCapX, capY, holdParts.leftHalf);
                    painter.drawPixmap(rightCapX + holdParts.rightHalfOffset, capY, holdParts.rightHalf);
                }
            }

            if (drawSlideTracks && isSlideTrack) {
                const int startLane = qBound(1, note.lane, kPlayableLaneCount);
                const int endLane = qBound(1, note.endLane, kPlayableLaneCount);
                const int startY = top + (startLane - 1) * laneH + laneH / 2;
                const int endY = top + (endLane - 1) * laneH + laneH / 2;
                const qreal dx = static_cast<qreal>(slideEndX - slideStartX);
                const qreal dy = static_cast<qreal>(endY - startY);
                const qreal length = qSqrt(dx * dx + dy * dy);
                const bool forward = slideEndX >= slideStartX;
                QString baseTrackType = QStringLiteral("slide_track");
                if (timelineRenderFlagSet(note, TimelineRenderFlagTrackBreak)) {
                    baseTrackType = QStringLiteral("slide_track_break");
                } else if (timelineRenderFlagSet(note, TimelineRenderFlagSlideEach)) {
                    baseTrackType = QStringLiteral("slide_track_each");
                }
                const qreal trackScale = qBound<qreal>(0.25, zoomScale(), 1.0);
                const qreal angleDegrees = (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy))
                    ? qRadiansToDegrees(qAtan2(dy, dx))
                    : 0.0;
                const QPixmap& drawTile = transformedIconForType(baseTrackType, trackScale, angleDegrees, forward);
                const QPixmap& spacingTile = transformedIconForType(baseTrackType, trackScale, 0.0, forward);
                if (!drawTile.isNull()) {
                    if (length < 1.0) {
                        painter.drawPixmap(
                            QPointF(
                                static_cast<qreal>(slideStartX) - static_cast<qreal>(drawTile.width()) / 2.0,
                                static_cast<qreal>(startY) - static_cast<qreal>(drawTile.height()) / 2.0),
                            drawTile);
                    } else {
                        const qreal spacing = qMax<qreal>(4.0, static_cast<qreal>(spacingTile.width()) * 0.72 + 1.0);
                        const int steps = qMax(1, static_cast<int>(length / spacing));
                        for (int step = 0; step <= steps; ++step) {
                            const qreal t = static_cast<qreal>(step) / static_cast<qreal>(steps);
                            const qreal cx = static_cast<qreal>(slideStartX) + dx * t;
                            const qreal cy = static_cast<qreal>(startY) + dy * t;
                            painter.drawPixmap(
                                QPointF(
                                    cx - static_cast<qreal>(drawTile.width()) / 2.0,
                                    cy - static_cast<qreal>(drawTile.height()) / 2.0),
                                drawTile);
                        }
                    }
                }
            }

            if (isTouchHold) {
                const QPen holdPen(
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEach) ? QColor(255, 214, 64, 120) : QColor(44, 214, 255, 120),
                    4.0,
                    Qt::SolidLine,
                    Qt::RoundCap
                );
                painter.save();
                painter.setPen(holdPen);
                painter.drawLine(QPointF(x, rowCenterY), QPointF(secondToX(endSecond) - xOffset, rowCenterY));
                painter.restore();
            }

            const bool shouldDrawHead = !isSlideLike || timelineRenderFlagSet(note, TimelineRenderFlagHasHeadStar);
            if (shouldDrawHead && !icon.isNull()) {
                const int iconY = rowTop + (laneH - icon.height()) / 2;
                painter.drawPixmap(x - icon.width() / 2, iconY, icon);
                if (isHold) {
                    painter.drawPixmap(holdEndX - icon.width() / 2, iconY, icon);
                }
            } else if (shouldDrawHead) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(c.timelineCursor);
                painter.drawEllipse(QRectF(x - 3, rowCenterY - 4, 8, 8));
                if (isHold) {
                    painter.drawEllipse(QRectF(holdEndX - 3, rowCenterY - 4, 8, 8));
                }
            }

            if (hasMuriWarning) {
                painter.save();
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 146, 43, 230));
                painter.drawEllipse(QRectF(x + 4, rowCenterY - 8, 7, 7));
                painter.restore();
            }
        }
    }

    const int entryX = secondToX(playbackEntrySeconds_) - xOffset;
    if (entryX > left) {
        painter.save();
        painter.setClipRect(QRect(left, 0, viewport()->width() - left, top));
        painter.setPen(Qt::NoPen);
        QColor entryColor = c.timelinePlayhead;
        entryColor.setAlpha(220);
        painter.setBrush(entryColor);
        QPainterPath entryMarker;
        entryMarker.moveTo(entryX, top - 1);
        entryMarker.lineTo(entryX - 6, qMax(0, top - 9));
        entryMarker.lineTo(entryX + 6, qMax(0, top - 9));
        entryMarker.closeSubpath();
        painter.drawPath(entryMarker);
        painter.restore();
    }

    const int playheadX = secondToX(playheadSeconds_) - xOffset;
    const int cursorX = secondToX(cursorSeconds_) - xOffset;
    if (cursorX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelineCursor, 2));
        painter.drawLine(cursorX, top, cursorX, top + h);
        painter.restore();
    }

    if (!playheadIndicatorSuppressed_ && playheadX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelinePlayhead, 2));
        painter.drawLine(playheadX, top, playheadX, top + h);
        painter.restore();
    }

    if (timelineDragActive_) {
        const int dragCenterX = viewport()->width() / 2;
        if (dragCenterX > left) {
            painter.save();
            painter.setClipRect(timelineRect);
            painter.setPen(QPen(c.timelinePlayhead, 2));
            painter.drawLine(dragCenterX, top, dragCenterX, top + h);
            painter.restore();
        }
    }

    painter.fillRect(QRect(0, top - 1, left + 1, h + 2), c.timelineSidebar);
    painter.setFont(laneLabelFont);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const int y = top + lane * laneH;
        painter.setPen(c.timelineBorder);
        painter.drawLine(0, y + laneH, left, y + laneH);
        painter.setPen(c.timelineLabel);
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }
    painter.setPen(c.timelineAxis);
    painter.drawLine(left, top - 1, left, top + h);
    painter.setPen(c.timelineBorder);
    painter.drawRect(QRect(0, 0, viewport()->width() - 1, top + h));
}
