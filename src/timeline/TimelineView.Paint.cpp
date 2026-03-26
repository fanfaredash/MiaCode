void TimelineView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    const UiTheme::Colors& c = UiTheme::colors();

    QPainter painter(viewport());
    if (!painter.isActive()) {
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), c.timelineWindow);

    const int left = timelineLeft();
    const int top = timelineTop();
    const int h = timelineHeight();
    const int laneH = laneHeight();
    const int xOffset = horizontalScrollBar()->value();
    const bool drawSlideTracks = effectiveShowSlideTracks();
    const QRect timelineRect(left, top, viewport()->width() - left, h);

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
    for (const TimelineBeatMarker& marker : beats_) {
        const int x = secondToX(marker.second) - xOffset;
        if (x < left - 1 || x > viewport()->width()) {
            continue;
        }
        painter.setPen(marker.major ? c.timelineGridMajor : c.timelineGridMinor);
        painter.drawLine(x, top, x, top + h);
        if (marker.major) {
            const int sourceLine = marker.sourceLine > 0 ? marker.sourceLine : lineNumberForSecond(marker.second);
            const QString labelText = QString::number(sourceLine);
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

    for (const TimelineNoteMarker& note : notes_) {
        if (!note.isFirework) {
            continue;
        }
        const QString noteType = note.type.toLower();
        if (noteType != "touch" && noteType != "touch_hold") {
            continue;
        }

        const double triggerSecond =
            (noteType == "touch")
            ? note.second
            : ((note.endSecond > note.second) ? note.endSecond : note.second);
        const double endSecond = triggerSecond + kTimelineFireworkDurationSeconds;
        if (endSecond <= triggerSecond) {
            continue;
        }

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

    QPixmap slideTrackTileLeft;
    QPixmap slideTrackTileRight;
    QPixmap slideTrackEachTileLeft;
    QPixmap slideTrackEachTileRight;
    QPixmap slideTrackBreakTileLeft;
    QPixmap slideTrackBreakTileRight;
    const QPixmap slideTrackBase = iconForType("slide_track");
    const QPixmap slideTrackEachBase = iconForType("slide_track_each");
    const QPixmap slideTrackBreakBase = iconForType("slide_track_break");
    if (!slideTrackBase.isNull()) {
        slideTrackTileLeft = slideTrackBase;
        slideTrackTileRight = slideTrackBase.transformed(QTransform().scale(-1.0, 1.0), Qt::SmoothTransformation);
    }
    if (!slideTrackEachBase.isNull()) {
        slideTrackEachTileLeft = slideTrackEachBase;
        slideTrackEachTileRight = slideTrackEachBase.transformed(QTransform().scale(-1.0, 1.0), Qt::SmoothTransformation);
    }
    if (!slideTrackBreakBase.isNull()) {
        slideTrackBreakTileLeft = slideTrackBreakBase;
        slideTrackBreakTileRight = slideTrackBreakBase.transformed(QTransform().scale(-1.0, 1.0), Qt::SmoothTransformation);
    }

    for (const TimelineNoteMarker& note : notes_) {
        if (note.lane < 1 || note.lane > kLaneCount) {
            continue;
        }
        const bool hasMuriWarning = muriMarkerKeys_.contains(makeMarkerAnalysisKey(note));
        const bool isHold = note.type.compare("hold", Qt::CaseInsensitive) == 0 && note.endSecond > note.second;
        const bool isTouchHold = note.type.compare("touch_hold", Qt::CaseInsensitive) == 0 && note.endSecond > note.second;
        const bool isSlideTrack = (note.type.compare("slide", Qt::CaseInsensitive) == 0
                || note.type.compare("wifi", Qt::CaseInsensitive) == 0)
            && note.slideTraceSecond > note.second
            && note.endSecond > note.slideTraceSecond;
        const int x = secondToX(note.second) - xOffset;
        const int holdEndX = isHold ? (secondToX(note.endSecond) - xOffset) : x;
        const int slideStartX = isSlideTrack ? (secondToX(note.slideTraceSecond) - xOffset) : x;
        const int slideEndX = isSlideTrack ? (secondToX(note.endSecond) - xOffset) : x;
        int extentLeft = x;
        int extentRight = x;
        if (isHold) {
            extentLeft = qMin(extentLeft, holdEndX);
            extentRight = qMax(extentRight, holdEndX);
        } else if (isTouchHold) {
            const int touchHoldEndX = secondToX(note.endSecond) - xOffset;
            extentLeft = qMin(extentLeft, touchHoldEndX);
            extentRight = qMax(extentRight, touchHoldEndX);
        }
        if (isSlideTrack && drawSlideTracks) {
            extentLeft = qMin(extentLeft, qMin(slideStartX, slideEndX));
            extentRight = qMax(extentRight, qMax(slideStartX, slideEndX));
        }
        if (extentRight < left - kNoteSize || extentLeft > viewport()->width() + kNoteSize) {
            continue;
        }

        const int rowTop = top + (note.lane - 1) * laneH;
        const int rowCenterY = rowTop + (laneH / 2);
        QString iconType = note.type.toLower();
        if (iconType == "tap" && note.isBreak) {
            iconType = "tap_break";
        } else if (iconType == "tap" && note.isEach) {
            iconType = "tap_each";
        } else if (iconType == "hold" && note.isBreak) {
            iconType = "hold_break";
        } else if (iconType == "hold" && note.isEach) {
            iconType = "hold_each";
        } else if (iconType == "touch" && note.isBreak) {
            iconType = "touch_break";
        } else if (iconType == "touch" && note.isEach) {
            iconType = "touch_each";
        } else if (iconType == "touch_hold" && note.isBreak) {
            iconType = "touch_hold_break";
        } else if (iconType == "touch_hold" && note.isEach) {
            iconType = "touch_hold_each";
        } else if (iconType == "slide" || iconType == "wifi") {
            // Star icon should follow the falling slide-head star(each) semantics,
            // while track color is controlled separately by slideEach.
            const bool headEach = note.headEach;
            if (note.headBreak && note.sameHeadSlide) {
                iconType = "star_break_double";
            } else if (note.headBreak) {
                iconType = "star_break";
            } else if (headEach && note.sameHeadSlide) {
                iconType = "star_each_double";
            } else if (note.sameHeadSlide) {
                iconType = "star_double";
            } else if (headEach) {
                iconType = "star_each";
            }
        }
        QPixmap icon = iconForType(iconType);
        if (zoomScale() <= 0.25 && !icon.isNull()) {
            icon = icon.scaled(
                qMax(1, icon.width() / 2),
                qMax(1, icon.height() / 2),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );
        }

        if (isHold) {
            QPixmap holdBaseIcon = iconForType(
                note.isBreak ? QString("hold_break") : (note.isEach ? QString("hold_each") : QString("hold"))
            );
            if (zoomScale() <= 0.25 && !holdBaseIcon.isNull()) {
                holdBaseIcon = holdBaseIcon.scaled(
                    qMax(1, holdBaseIcon.width() / 2),
                    qMax(1, holdBaseIcon.height() / 2),
                    Qt::KeepAspectRatio,
                    Qt::SmoothTransformation
                );
            }
            QPixmap holdCapPixmap;
            QPixmap holdCapLeftHalf;
            QPixmap holdCapRightHalf;
            int holdCapRightHalfOffset = 0;
            QImage holdBodySlice;

            if (!holdBaseIcon.isNull()) {
                QTransform transform;
                transform.rotate(90.0);
                holdCapPixmap = holdBaseIcon.transformed(transform, Qt::SmoothTransformation);
                if (!holdCapPixmap.isNull()) {
                    const QImage capImage = holdCapPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    const int sx = qBound(0, capImage.width() / 2, capImage.width() - 1);
                    holdBodySlice = capImage.copy(sx, 0, 1, capImage.height());
                    const int splitX = qMax(1, capImage.width() / 2);
                    holdCapLeftHalf = holdCapPixmap.copy(0, 0, splitX, holdCapPixmap.height());
                    holdCapRightHalf = holdCapPixmap.copy(splitX, 0, holdCapPixmap.width() - splitX, holdCapPixmap.height());
                    holdCapRightHalfOffset = splitX;
                }
            }

            if (holdCapPixmap.isNull() || holdBodySlice.isNull()) {
                continue;
            }
            const int capY = rowTop + (laneH - holdCapPixmap.height()) / 2;
            const int leftCapX = extentLeft - holdCapPixmap.width() / 2;
            const int rightCapX = extentRight - holdCapPixmap.width() / 2;
            const int splitX = holdCapLeftHalf.width();
            const int bodyStartX = leftCapX + splitX - 1;
            const int bodyEndX = rightCapX + splitX + 1;
            const int bodyWidth = qMax(0, bodyEndX - bodyStartX);
            if (bodyWidth > 0) {
                painter.drawImage(
                    QRect(bodyStartX, capY, bodyWidth, holdBodySlice.height()),
                    holdBodySlice,
                    QRect(0, 0, 1, holdBodySlice.height())
                );
            }
            painter.drawPixmap(leftCapX, capY, holdCapLeftHalf);
            painter.drawPixmap(rightCapX + holdCapRightHalfOffset, capY, holdCapRightHalf);
            continue;
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
            const QPixmap& normalTile = (forward && !slideTrackTileRight.isNull()) ? slideTrackTileRight : slideTrackTileLeft;
            const QPixmap& eachTile = (forward && !slideTrackEachTileRight.isNull()) ? slideTrackEachTileRight : slideTrackEachTileLeft;
            const QPixmap& breakTile = (forward && !slideTrackBreakTileRight.isNull()) ? slideTrackBreakTileRight : slideTrackBreakTileLeft;
            const bool useBreakTrack = note.trackBreak && !breakTile.isNull();
            const bool useEachTrack = !useBreakTrack && note.slideEach && !eachTile.isNull();
            const QPixmap& baseTile = useBreakTrack ? breakTile : (useEachTrack ? eachTile : normalTile);
            if (baseTile.isNull()) {
                continue;
            }
            const qreal trackScale = qBound<qreal>(0.25, zoomScale(), 1.0);
            QPixmap scaledBaseTile = baseTile;
            if (!qFuzzyCompare(trackScale, 1.0)) {
                scaledBaseTile = baseTile.scaled(
                    qMax(1, qRound(static_cast<qreal>(baseTile.width()) * trackScale)),
                    qMax(1, qRound(static_cast<qreal>(baseTile.height()) * trackScale)),
                    Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation
                );
            }

            QPixmap drawTile = scaledBaseTile;
            if (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy)) {
                const qreal angle = qRadiansToDegrees(qAtan2(dy, dx));
                drawTile = scaledBaseTile.transformed(QTransform().rotate(angle), Qt::SmoothTransformation);
            }

            if (drawTile.isNull()) {
                continue;
            }

            if (length < 1.0) {
                painter.drawPixmap(
                    QPointF(
                        static_cast<qreal>(slideStartX) - static_cast<qreal>(drawTile.width()) / 2.0,
                        static_cast<qreal>(startY) - static_cast<qreal>(drawTile.height()) / 2.0
                    ),
                    drawTile
                );
            } else {
                const qreal spacing = qMax<qreal>(4.0, static_cast<qreal>(scaledBaseTile.width()) * 0.72 + 1.0);
                const int steps = qMax(1, static_cast<int>(length / spacing));
                for (int i = 0; i <= steps; ++i) {
                    const qreal t = static_cast<qreal>(i) / static_cast<qreal>(steps);
                    const qreal cx = static_cast<qreal>(slideStartX) + dx * t;
                    const qreal cy = static_cast<qreal>(startY) + dy * t;
                    painter.drawPixmap(
                        QPointF(
                            cx - static_cast<qreal>(drawTile.width()) / 2.0,
                            cy - static_cast<qreal>(drawTile.height()) / 2.0
                        ),
                        drawTile
                    );
                }
            }
        }

        if (note.type.compare("touch_hold", Qt::CaseInsensitive) == 0) {
            const int startX = x;
            const int endX = note.endSecond > note.second ? (secondToX(note.endSecond) - xOffset) : x;
            const QPen holdPen(
                note.isEach ? QColor(255, 214, 64, 120) : QColor(44, 214, 255, 120),
                4.0,
                Qt::SolidLine,
                Qt::RoundCap
            );
            painter.save();
            painter.setPen(holdPen);
            painter.drawLine(QPointF(startX, rowCenterY), QPointF(endX, rowCenterY));
            painter.restore();
        }

        const bool isSlideLike = note.type.compare("slide", Qt::CaseInsensitive) == 0
            || note.type.compare("wifi", Qt::CaseInsensitive) == 0;
        const bool shouldDrawHead = !isSlideLike || note.hasHeadStar;
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

    const int playheadX = secondToX(playheadSeconds_) - xOffset;
    if (!playheadIndicatorSuppressed_ && playheadX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelinePlayhead, 2));
        painter.drawLine(playheadX, top, playheadX, top + h);
        painter.restore();
    }

    const int cursorX = secondToX(cursorSeconds_) - xOffset;
    if (cursorX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelineCursor, 2));
        painter.drawLine(cursorX, top, cursorX, top + h);
        painter.restore();
    }

    // Overlay the gutter last so timeline objects never bleed into the lane-number column.
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
