void TimelineView::paintEvent(QPaintEvent* event)
{
    QElapsedTimer paintTimer;
    const bool logPerf = miacode::debug_options::runtimeDebugOutputEnabled();
    if (logPerf) {
        paintTimer.start();
    }
    const UiTheme::Colors& c = UiTheme::colors();
    const QRect dirtyRect = event != nullptr ? event->rect() : viewport()->rect();

    QPainter painter(viewport());
    if (!painter.isActive()) {
        return;
    }
    if (waveformOnlyPresentation()) {
        paintWaveformOnly(painter, dirtyRect);
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(dirtyRect);
    if (miacode::ui::paintAppBackgroundForWidget(viewport(), painter)) {
        QColor timelineSurface = c.timelineWindow;
        timelineSurface.setAlpha(c.dark ? 184 : 196);
        painter.fillRect(dirtyRect, timelineSurface);
    } else {
        painter.fillRect(dirtyRect, c.timelineWindow);
    }

    const int left = timelineLeft();
    const int top = timelineTop();
    const int h = timelineHeight();
    const int laneH = laneHeight();
    const int xOffset = horizontalScrollBar()->value();
    const bool drawSlideTracks = effectiveShowSlideTracks();
    const QRect timelineRect(left, top, viewport()->width() - left, h);
    VisibleLineRange beatRange;
    VisibleLineRange noteRange;
    double visibleStartSecond = xToSecond(left);
    double visibleEndSecond = xToSecond(viewport()->width());
    if (dirtyRect.right() >= left) {
        const int dirtyRight = qMin(viewport()->width(), dirtyRect.right() + 1);
        visibleStartSecond = xToSecond(qMax(left, dirtyRect.left()));
        visibleEndSecond = xToSecond(qMax(left, dirtyRight));
        beatRange = visibleLineRange(visibleStartSecond - 1.0, visibleEndSecond + 1.0);
        if (dirtyRect.bottom() >= top && dirtyRect.top() <= top + h) {
            const QVector<double>& noteVisualPrefixMax = (drawSlideTracks || !muriMarkerPlacementsByLocation_.isEmpty())
                ? noteVisualEndPrefixMaxWithSlideTracks_
                : noteVisualEndPrefixMaxWithoutSlideTracks_;
            if (noteVisualPrefixMax.size() == lines_.size()) {
                const TimelineVisibleLineRange sharedRange = timelineRenderVisibleNoteLineRange(
                    lines_,
                    noteVisualPrefixMax,
                    visibleStartSecond - 2.0,
                    visibleEndSecond + 2.0
                );
                noteRange.begin = sharedRange.begin;
                noteRange.end = sharedRange.end;
            } else {
                noteRange = visibleLineRange(visibleStartSecond - 2.0, visibleEndSecond + 2.0);
            }
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
    const auto muriMarkerAnchorY = [top, laneH, &noteSecond, &noteTraceSecond, &noteEndSecond](
                                       const TimelineRenderLine& line,
                                       const TimelineRenderNote& note,
                                       double triggerSecond) {
        const int startLane = qBound(1, note.lane, kPlayableLaneCount);
        const qreal startY = static_cast<qreal>(top + (startLane - 1) * laneH + laneH / 2);
        if (note.kind != TimelineRenderNoteKind::Slide && note.kind != TimelineRenderNoteKind::Wifi) {
            return startY;
        }

        const double startSecond = noteSecond(line, note);
        const double traceSecond = noteTraceSecond(line, note) >= 0.0 ? noteTraceSecond(line, note) : startSecond;
        const double endSecond = noteEndSecond(line, note) >= 0.0 ? noteEndSecond(line, note) : traceSecond;
        if (!(traceSecond > startSecond && endSecond > traceSecond)) {
            return startY;
        }

        const int endLane = qBound(1, note.endLane, kPlayableLaneCount);
        const qreal endY = static_cast<qreal>(top + (endLane - 1) * laneH + laneH / 2);
        if (triggerSecond <= traceSecond) {
            return startY;
        }
        if (triggerSecond >= endSecond) {
            return endY;
        }

        const qreal proportion = qBound<qreal>(
            0.0,
            static_cast<qreal>((triggerSecond - traceSecond) / (endSecond - traceSecond)),
            1.0
        );
        return startY + (endY - startY) * proportion;
    };

    painter.fillRect(QRect(0, 0, viewport()->width(), top), c.timelineHeader);
    painter.fillRect(QRect(0, top, left, h), c.timelineSidebar);
    painter.fillRect(timelineRect, c.timelineBase);
    const QPen borderPen(c.timelineBorder, 1.0);
    const QPen axisPen(c.timelineAxis, 1.0);
    // Beta21-fix11 — wired through the dedicated palette entries:
    //   bar lines  = c.timelineGridMajor
    //   note lines = c.timelineGridMinor
    // Tune those two palette entries directly in UiTheme.cpp.
    const QPen majorBeatPen(
        miacode::timeline::adjustedTimelineMeasureLineColor(c.timelineGridMajor, measureLineBrightness_),
        kTimelineBeatLineWidth);
    QColor laneDividerColor = c.timelineBorder;
    laneDividerColor.setAlpha(qMin(laneDividerColor.alpha(), 96));
    const QPen laneDividerPen(laneDividerColor, 1.0);
    const QPen minorBeatPen(
        miacode::timeline::adjustedTimelineMeasureLineColor(c.timelineGridMinor, measureLineBrightness_),
        1.0);
    // Tiered grid-line heights feature (see TimelineThemeConfig.h). The widget
    // timeline draws only the bar (小节) and per-comma (逗号) tiers — there are
    // no separate quarter-note (四分音符) subdivision lines in this path — so
    // here only the comma tier is shortened; bar lines stay full height.
    const int commaLineHeight = qRound(static_cast<qreal>(h)
        * miacode::timeline::timelineGridLineHeightFraction(
            miacode::timeline::kTimelineGridHeightFractionComma));
    painter.setPen(borderPen);
    painter.drawLine(0, top - 1, viewport()->width(), top - 1);

    QFont laneLabelFont(QStringLiteral("Consolas"));
    laneLabelFont.setStyleHint(QFont::Monospace);
    laneLabelFont.setPointSize(10);
    laneLabelFont.setWeight(QFont::DemiBold);

    // NOTE: the waveform is intentionally NOT drawn here anymore. It now
    // renders as a stroked contour AFTER the grid lines (further below), so
    // the audio shape sits on top of the grid while its transparent interior
    // keeps the bar/note lines visible. See the waveform block past the
    // measure-line loop.

    painter.setFont(laneLabelFont);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const int y = top + lane * laneH;
        const QColor rowColor = (lane % 2 == 0) ? c.timelineLaneEven : c.timelineLaneOdd;
        painter.fillRect(QRect(left, y, viewport()->width() - left, laneH), rowColor);
        painter.setPen(laneDividerPen);
        painter.drawLine(0, y + laneH, viewport()->width(), y + laneH);
        painter.setPen(c.timelineLabel);
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }

    painter.setPen(axisPen);
    painter.drawLine(left, top - 1, left, top + h);

    int headerLeftLimit = 0;
    if (zoomButton_ != nullptr) {
        headerLeftLimit = qMax(headerLeftLimit, zoomButton_->x() + zoomButton_->width() + 1);
    }
    int headerRightLimit = viewport()->width() - 4;
    if (followPreviewCheckBox_ != nullptr) {
        headerRightLimit = qMin(headerRightLimit, followPreviewCheckBox_->x() - 1);
    }
    if (viewportLockCheckBox_ != nullptr) {
        headerRightLimit = qMin(headerRightLimit, viewportLockCheckBox_->x() - 1);
    }
    if (followProgressCheckBox_ != nullptr) {
        headerRightLimit = qMin(headerRightLimit, followProgressCheckBox_->x() - 1);
    }
    const QVector<HeaderLineLabel> headerLabels = visibleHeaderLineLabels(
        visibleStartSecond,
        visibleEndSecond,
        xOffset,
        headerLeftLimit,
        headerRightLimit);

    for (int lineIndex = beatRange.begin; lineIndex < beatRange.end; ++lineIndex) {
        const TimelineRenderLine& line = lines_.at(lineIndex);
        for (const TimelineRenderBeat& marker : line.beats) {
            if (!shouldPaintTimelineBeatMarker(marker)) {
                continue;
            }
            const double absoluteSecond = timelineRenderAbsoluteSecond(line, marker.secondOffset);
            const int x = secondToX(absoluteSecond) - xOffset;
            if (x < left - 1 || x > viewport()->width()) {
                continue;
            }
            painter.setPen(minorBeatPen);
            painter.drawLine(x, top, x, top + commaLineHeight);
        }
    }

    const auto measureBeginIt = std::lower_bound(
        measureLineSeconds_.cbegin(),
        measureLineSeconds_.cend(),
        visibleStartSecond - 1.0);
    const auto measureEndIt = std::upper_bound(
        measureLineSeconds_.cbegin(),
        measureLineSeconds_.cend(),
        visibleEndSecond + 1.0);
    for (auto it = measureBeginIt; it != measureEndIt; ++it) {
        const double measureSecond = *it;
        const int x = secondToX(measureSecond) - xOffset;
        if (x < left - 1 || x > viewport()->width()) {
            continue;
        }
        painter.setPen(majorBeatPen);
        painter.drawLine(x, top, x, top + h);
    }
    if (trailingMeasureLineStepSeconds_ > 1e-6) {
        const double visibleLowerBound = visibleStartSecond - 1.0;
        const double visibleUpperBound = visibleEndSecond + 1.0;
        double extensionSecond = trailingMeasureLineStartSecond_;
        if (!measureLineSeconds_.isEmpty()) {
            const double lastMeasureSecond = measureLineSeconds_.constLast();
            if (extensionSecond <= lastMeasureSecond + 1e-6) {
                const double delta = lastMeasureSecond - extensionSecond;
                const qint64 steps = qMax<qint64>(
                    1,
                    static_cast<qint64>(qFloor(delta / trailingMeasureLineStepSeconds_)) + 1);
                extensionSecond += trailingMeasureLineStepSeconds_ * static_cast<double>(steps);
            }
        }
        if (extensionSecond <= visibleLowerBound + 1e-6) {
            const double delta = visibleLowerBound - extensionSecond;
            const qint64 steps = qMax<qint64>(
                1,
                static_cast<qint64>(qFloor(delta / trailingMeasureLineStepSeconds_)) + 1);
            extensionSecond += trailingMeasureLineStepSeconds_ * static_cast<double>(steps);
        }
        for (; extensionSecond <= visibleUpperBound + 1e-6; extensionSecond += trailingMeasureLineStepSeconds_) {
            const int x = secondToX(extensionSecond) - xOffset;
            if (x < left - 1 || x > viewport()->width()) {
                continue;
            }
            painter.setPen(majorBeatPen);
            painter.drawLine(x, top, x, top + h);
        }
    }

    // Waveform — drawn AFTER the grid lines (so it reads on top of the grid)
    // as a single ANTI-ALIASED filled silhouette: the top envelope left->right
    // then the bottom envelope right->left, closed into one polygon. This
    // avoids the per-column hard-edged "sawtooth" aliasing of discrete
    // fillRect bars and gives a smooth, clean waveform while preserving the
    // real min/max detail. Translucent (alpha in c.timelineWaveStroke) so the
    // grid shows through; stays below note sprites. Brightness scales the
    // fill's alpha via adjustedTimelineWaveformColor (hue fixed).
    if (waveformData_ && waveformData_->durationSeconds > 0.0) {
        const miacode::waveform::WaveformLevel* waveformLevel =
            miacode::waveform::selectWaveformLevelForVisibleRange(
                *waveformData_,
                qMax(0.001, visibleEndSecond - visibleStartSecond),
                qMax(1, timelineRect.width()));
        if (waveformLevel != nullptr && !waveformLevel->columns.isEmpty()) {
            const double phaseCompensationSeconds = qMax(0.0, waveformPhaseCompensationSeconds_);
            const QPair<int, int> visibleColumns =
                miacode::waveform::visibleWaveformColumnRange(
                    *waveformLevel,
                    qMax(0.0, visibleStartSecond + phaseCompensationSeconds),
                    qMax(0.0, visibleEndSecond + phaseCompensationSeconds));
            const qreal centerY = top + h / 2.0;
            const qreal maxAmplitude = (qMax<qreal>(8.0, h / 2.0 - 8.0) * 7.0) / 9.0;
            const QColor waveformColor = miacode::timeline::adjustedTimelineWaveformColor(
                c.timelineWaveStroke,
                waveformBrightness_);
            QPainterPath wavePath;
            QVector<QPointF> bottomEnvelope;
            bottomEnvelope.reserve(visibleColumns.second - visibleColumns.first);
            bool wavePathStarted = false;
            for (int index = visibleColumns.first; index < visibleColumns.second; ++index) {
                const miacode::waveform::WaveformColumn& column = waveformLevel->columns.at(index);
                // Hard waveform phase compensation: logs from multiple devices
                // (60 Hz and 120 Hz) show the authoritative audio clock leading
                // the timeline-rendered playhead by about one frame. The root
                // cause is still unclear, so keep this as an explicit rendering
                // compensation rather than folding it into waveform cache data.
                const double columnStartSecond =
                    waveformLevel->secondsPerColumn * static_cast<double>(index) - phaseCompensationSeconds;
                const double columnEndSecond = columnStartSecond + waveformLevel->secondsPerColumn;
                const qreal x0 = static_cast<qreal>(secondToX(columnStartSecond) - xOffset);
                qreal x1 = static_cast<qreal>(secondToX(columnEndSecond) - xOffset);
                if (x1 <= x0) {
                    x1 = x0 + 1.0;
                }
                if (x1 < left - 2.0 || x0 > viewport()->width() + 2.0) {
                    continue;
                }
                const qreal topY = centerY - (qBound(-1.0f, column.max, 1.0f) * maxAmplitude);
                const qreal bottomY = centerY - (qBound(-1.0f, column.min, 1.0f) * maxAmplitude);
                if (!wavePathStarted) {
                    wavePath.moveTo(x0, topY);
                    wavePathStarted = true;
                } else {
                    wavePath.lineTo(x0, topY);
                }
                wavePath.lineTo(x1, topY);
                bottomEnvelope.append(QPointF(x0, bottomY));
                bottomEnvelope.append(QPointF(x1, bottomY));
            }
            if (wavePathStarted) {
                for (int i = bottomEnvelope.size() - 1; i >= 0; --i) {
                    wavePath.lineTo(bottomEnvelope.at(i));
                }
                wavePath.closeSubpath();
                painter.save();
                painter.setClipRect(QRect(left, top, viewport()->width() - left, h));
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(waveformColor);
                painter.drawPath(wavePath);
                painter.restore();
            }
        }
    }

    if (!headerLabels.isEmpty()) {
        const qreal timelineHeaderScale = static_cast<qreal>(headerContentScale());
        auto scaledHeaderFont = [](const QFont& sourceFont, qreal scale) {
            QFont scaledFont(sourceFont);
            const qreal clampedScale = qMax(0.1, scale);
            if (scaledFont.pointSizeF() > 0.0) {
                scaledFont.setPointSizeF(qMax(1.0, scaledFont.pointSizeF() * clampedScale));
            } else if (scaledFont.pointSize() > 0) {
                scaledFont.setPointSizeF(qMax(1.0, static_cast<qreal>(scaledFont.pointSize()) * clampedScale));
            } else if (scaledFont.pixelSize() > 0) {
                scaledFont.setPixelSize(qMax(1, qRound(static_cast<qreal>(scaledFont.pixelSize()) * clampedScale)));
            }
            return scaledFont;
        };
        const QFont oneDigitFont =
            scaledHeaderFont(headerLineNumberFont_, kTimelineHeaderSingleDigitFontScale * timelineHeaderScale);
        const QFontMetricsF oneDigitMetrics(oneDigitFont);
        const QFontMetricsF baseHeaderMetrics(scaledHeaderFont(headerLineNumberFont_, timelineHeaderScale));
        qreal singleDigitWidth = 0.0;
        for (QChar digit = QLatin1Char('0'); digit <= QLatin1Char('9'); digit = QChar(digit.unicode() + 1)) {
            singleDigitWidth = qMax(singleDigitWidth, static_cast<qreal>(oneDigitMetrics.horizontalAdvance(digit)));
        }
        const qreal multiDigitWidthBudget = qMax<qreal>(
            8.0,
            static_cast<qreal>(kTimelineHeaderLineLabelMinSpacingPx - kTimelineHeaderMultiDigitLabelSideGapPx)
        );
        auto headerFontForDigitCount = [&](int digitCount) {
            if (digitCount <= 1) {
                return oneDigitFont;
            }
            const QString widthSample(digitCount, QLatin1Char('8'));
            const qreal widthScale = multiDigitWidthBudget
                / qMax<qreal>(1.0, static_cast<qreal>(baseHeaderMetrics.horizontalAdvance(widthSample)));
            return scaledHeaderFont(
                headerLineNumberFont_,
                qMin(kTimelineHeaderMultiDigitBaseFontScale, widthScale) * timelineHeaderScale
            );
        };
        const qreal markerTipY =
            static_cast<qreal>(top) - scaledTimelineHeaderMetric(kTimelineTopMarkerTipOffsetPx);
        const qreal headerTextBottom = (static_cast<qreal>(top - oneDigitMetrics.height()) * 0.5)
            + oneDigitMetrics.height();
        const qreal maxMarkerHeight = qMax(
            0.0,
            markerTipY - headerTextBottom
                - scaledTimelineHeaderMetric(static_cast<qreal>(kTimelineHeaderAnchorMarkerTextGapPx)));
        const qreal legacyHeightLimitedMarkerHeight =
            singleDigitWidth * kTimelineHeaderAnchorMarkerLegacyWidthFactor
            * kTimelineHeaderAnchorMarkerLegacyHeightFactor;
        const qreal markerHeight = qMin(maxMarkerHeight, legacyHeightLimitedMarkerHeight);
        if (markerHeight >= 2.0) {
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setPen(Qt::NoPen);
            painter.setBrush(c.textSecondary);
            const qreal markerBaseY = markerTipY - markerHeight;
            const qreal markerHalfWidth = markerHeight * kTimelineTopMarkerHalfWidthPerHeight;
            for (const HeaderLineLabel& label : headerLabels) {
                const qreal markerCenterX = static_cast<qreal>(label.screenX);
                const QPointF markerPoints[3] = {
                    QPointF(markerCenterX - markerHalfWidth, markerBaseY),
                    QPointF(markerCenterX + markerHalfWidth, markerBaseY),
                    QPointF(markerCenterX, markerTipY),
                };
                painter.drawConvexPolygon(markerPoints, 3);
            }
            painter.restore();
        }
        painter.setPen(c.textSecondary);
        for (const HeaderLineLabel& label : headerLabels) {
            const QString labelText = QString::number(label.lineNumber);
            const QFont labelFont = headerFontForDigitCount(labelText.size());
            const QFontMetricsF labelMetrics(labelFont);
            const qreal baselineY = headerTextBottom - labelMetrics.descent();
            const qreal textLeft = static_cast<qreal>(label.screenX) - (labelMetrics.horizontalAdvance(labelText) * 0.5);
            painter.setFont(labelFont);
            painter.drawText(QPointF(textLeft, baselineY), labelText);
        }
        painter.setFont(laneLabelFont);
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

    const TimelineVisibleLineRange visibleNoteRange{noteRange.begin, noteRange.end};
    const QVector<TimelineVisibleNoteRef> visibleNoteRefs = timelineRenderVisibleNotePaintOrder(lines_, visibleNoteRange);
    struct TimelineHeadLayerNoteRef {
        TimelineVisibleNoteRef ref;
        double second = 0.0;
        int sourceSequence = 0;
    };
    QVector<TimelineHeadLayerNoteRef> holdTapHeadRefs;
    holdTapHeadRefs.reserve(visibleNoteRefs.size());
    for (int sequence = 0; sequence < visibleNoteRefs.size(); ++sequence) {
        const TimelineVisibleNoteRef& visibleRef = visibleNoteRefs.at(sequence);
        const TimelineRenderLine& line = lines_.at(visibleRef.lineIndex);
        const TimelineRenderNote& note = line.notes.at(visibleRef.noteIndex);
        switch (note.kind) {
        case TimelineRenderNoteKind::Tap:
        case TimelineRenderNoteKind::Hold:
        case TimelineRenderNoteKind::Slide:
        case TimelineRenderNoteKind::Wifi:
            holdTapHeadRefs.append(TimelineHeadLayerNoteRef{visibleRef, noteSecond(line, note), sequence});
            break;
        default:
            break;
        }
    }
    std::sort(holdTapHeadRefs.begin(), holdTapHeadRefs.end(), [](const TimelineHeadLayerNoteRef& a, const TimelineHeadLayerNoteRef& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second > b.second;
        }
        return a.sourceSequence > b.sourceSequence;
    });

    const auto tapIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx) {
        if (isEx && isBreak) {
            return QStringLiteral("tap_break_ex");
        }
        if (isEx && isEach) {
            return QStringLiteral("tap_each_ex");
        }
        if (isEx) {
            return QStringLiteral("tap_ex");
        }
        if (isBreak) {
            return QStringLiteral("tap_break");
        }
        if (isEach) {
            return QStringLiteral("tap_each");
        }
        return QStringLiteral("tap");
    };
    const auto holdIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx) {
        if (isEx && isBreak) {
            return QStringLiteral("hold_break_ex");
        }
        if (isEx && isEach) {
            return QStringLiteral("hold_each_ex");
        }
        if (isEx) {
            return QStringLiteral("hold_ex");
        }
        if (isBreak) {
            return QStringLiteral("hold_break");
        }
        if (isEach) {
            return QStringLiteral("hold_each");
        }
        return QStringLiteral("hold");
    };
    const auto starIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx, bool isDouble) {
        if (isEx && isBreak && isDouble) {
            return QStringLiteral("star_break_ex_double");
        }
        if (isEx && isBreak) {
            return QStringLiteral("star_break_ex");
        }
        if (isEx && isEach && isDouble) {
            return QStringLiteral("star_each_ex_double");
        }
        if (isEx && isEach) {
            return QStringLiteral("star_each_ex");
        }
        if (isEx && isDouble) {
            return QStringLiteral("star_ex_double");
        }
        if (isEx) {
            return QStringLiteral("star_ex");
        }
        if (isBreak && isDouble) {
            return QStringLiteral("star_break_double");
        }
        if (isBreak) {
            return QStringLiteral("star_break");
        }
        if (isEach && isDouble) {
            return QStringLiteral("star_each_double");
        }
        if (isDouble) {
            return QStringLiteral("star_double");
        }
        if (isEach) {
            return QStringLiteral("star_each");
        }
        return QStringLiteral("slide");
    };

    const auto paintVisibleNote = [&](const TimelineVisibleNoteRef& visibleRef, bool drawTrackLayer) {
        const TimelineRenderLine& line = lines_.at(visibleRef.lineIndex);
        const TimelineRenderNote& note = line.notes.at(visibleRef.noteIndex);
        if (note.lane < 1 || note.lane > kLaneCount) {
            return;
        }

        const double startSecond = noteSecond(line, note);
        const double endSecond = noteEndSecond(line, note);
        const double traceSecond = noteTraceSecond(line, note);
        const quint64 locationId = timelineRenderLocationId(line, note);
        const QVector<miacode::timeline::TimelineMuriMarkerPlacement>* noteMuriMarkers = nullptr;
        if (const auto markersIt = muriMarkerPlacementsByLocation_.constFind(locationId);
            markersIt != muriMarkerPlacementsByLocation_.constEnd()) {
            noteMuriMarkers = &markersIt.value();
        }
        const bool isHold = note.kind == TimelineRenderNoteKind::Hold && endSecond >= startSecond;
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
        if (noteMuriMarkers != nullptr) {
            for (const miacode::timeline::TimelineMuriMarkerPlacement& markerPlacement : *noteMuriMarkers) {
                if (!markerPlacement.useTriggerSecond) {
                    continue;
                }
                const int markerX = secondToX(markerPlacement.second) - xOffset;
                extentLeft = qMin(extentLeft, markerX);
                extentRight = qMax(extentRight, markerX);
            }
        }
        const int noteCullPadding = notePixelSize();
        if (extentRight < left - noteCullPadding || extentLeft > viewport()->width() + noteCullPadding) {
            return;
        }

        const int rowTop = top + (note.lane - 1) * laneH;
        const int rowCenterY = rowTop + (laneH / 2);

        QString iconType;
        switch (note.kind) {
        case TimelineRenderNoteKind::Tap:
            if (timelineRenderFlagSet(note, TimelineRenderFlagTapUsesStarMaterial)) {
                iconType = starIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEx),
                    timelineRenderFlagSet(note, TimelineRenderFlagTapStarDouble)
                );
            } else {
                iconType = tapIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEx)
                );
            }
            break;
        case TimelineRenderNoteKind::Hold:
            iconType = holdIconTypeForFlags(
                timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                timelineRenderFlagSet(note, TimelineRenderFlagIsEx)
            );
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
            if (timelineRenderFlagSet(note, TimelineRenderFlagSlideHeadUsesTapMaterial)) {
                iconType = tapIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEx)
                );
            } else {
                iconType = starIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEx),
                    timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide)
                );
            }
            break;
        default:
            iconType = QStringLiteral("tap");
            break;
        }

        // Mine notes (simai `m`) override the break/each icon (see the QSG
        // builder TimelineSceneStateBuilder.cpp for the parity logic).
        if (timelineRenderFlagSet(note, TimelineRenderFlagIsMine)) {
            switch (note.kind) {
            case TimelineRenderNoteKind::Tap:
                iconType = timelineRenderFlagSet(note, TimelineRenderFlagTapUsesStarMaterial)
                    ? QStringLiteral("star_mine")
                    : QStringLiteral("tap_mine");
                break;
            case TimelineRenderNoteKind::Hold:
                iconType = QStringLiteral("hold_mine");
                break;
            case TimelineRenderNoteKind::Touch:
                iconType = QStringLiteral("touch_mine");
                break;
            case TimelineRenderNoteKind::TouchHold:
                iconType = QStringLiteral("touch_hold_mine");
                break;
            case TimelineRenderNoteKind::Slide:
            case TimelineRenderNoteKind::Wifi:
                iconType = QStringLiteral("star_mine");
                break;
            default:
                break;
            }
        }

        if (drawTrackLayer) {
            if (!drawSlideTracks || !isSlideTrack) {
                return;
            }
            const int startLane = qBound(1, note.lane, kPlayableLaneCount);
            const int endLane = qBound(1, note.endLane, kPlayableLaneCount);
            const int startY = top + (startLane - 1) * laneH + laneH / 2;
            const int endY = top + (endLane - 1) * laneH + laneH / 2;
            const qreal dx = static_cast<qreal>(slideEndX - slideStartX);
            const qreal dy = static_cast<qreal>(endY - startY);
            const qreal length = qSqrt(dx * dx + dy * dy);
            const bool forward = slideEndX >= slideStartX;
            QString baseTrackType = QStringLiteral("slide_track");
            if (timelineRenderFlagSet(note, TimelineRenderFlagTrackMine)) {
                baseTrackType = QStringLiteral("slide_track_mine");
            } else if (timelineRenderFlagSet(note, TimelineRenderFlagTrackBreak)) {
                baseTrackType = QStringLiteral("slide_track_break");
            } else if (timelineRenderFlagSet(note, TimelineRenderFlagSlideEach)) {
                baseTrackType = QStringLiteral("slide_track_each");
            }
            const qreal zoomTrackScale = qBound<qreal>(0.25, zoomScale(), 1.0);
            const qreal trackScale = zoomScale() <= 0.25
                ? 0.5
                : qBound<qreal>(0.5, zoomTrackScale * static_cast<qreal>(contentScale()), 1.0);
            const qreal angleDegrees = (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy))
                ? qRadiansToDegrees(qAtan2(dy, dx))
                : 0.0;
            const QPixmap& drawTile = transformedIconForType(baseTrackType, trackScale, angleDegrees, forward);
            const QPixmap& spacingTile = transformedIconForType(baseTrackType, trackScale, 0.0, forward);
            if (drawTile.isNull()) {
                return;
            }
            if (length < 1.0) {
                painter.drawPixmap(
                    QPointF(
                        static_cast<qreal>(slideStartX) - static_cast<qreal>(drawTile.width()) / 2.0,
                        static_cast<qreal>(startY) - static_cast<qreal>(drawTile.height()) / 2.0),
                    drawTile);
                return;
            }
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
            return;
        }

        const qreal baseIconScale = zoomScale() <= 0.25 ? 0.5 : static_cast<qreal>(contentScale());
        const qreal iconScale = isHold ? holdScaleForBaseIconScale(iconType, baseIconScale) : baseIconScale;
        const QPixmap& icon = transformedIconForType(iconType, iconScale);

        bool holdCapsDrawn = false;
        if (isHold) {
            const HoldPixmapParts& holdParts = holdPixmapPartsForType(iconType, iconScale);
            if (!holdParts.leftCap.isNull() && !holdParts.rightCap.isNull() && !holdParts.bodySlice.isNull()) {
                holdCapsDrawn = true;
                const int capY = rowTop + (laneH - holdParts.leftCap.height()) / 2;
                const int leftCapX = extentLeft - holdParts.leftCap.width();
                const int rightCapX = extentRight;
                const int bodyStartX = leftCapX + holdParts.leftCap.width();
                const int bodyEndX = rightCapX;
                const int bodyWidth = qMax(0, bodyEndX - bodyStartX);
                if (bodyWidth > 0) {
                    painter.drawImage(
                        QRect(bodyStartX, capY, bodyWidth, holdParts.bodySlice.height()),
                        holdParts.bodySlice,
                        QRect(0, 0, 1, holdParts.bodySlice.height())
                    );
                }
                painter.drawPixmap(leftCapX, capY, holdParts.leftCap);
                painter.drawPixmap(rightCapX, capY, holdParts.rightCap);
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
            if (!isHold || !holdCapsDrawn) {
                painter.drawPixmap(x - icon.width() / 2, iconY, icon);
            }
            if (isHold && !holdCapsDrawn) {
                painter.drawPixmap(holdEndX - icon.width() / 2, iconY, icon);
            }
        } else if (shouldDrawHead) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(c.timelineCursor);
            painter.drawEllipse(QRectF(x - 3, rowCenterY - 4, 8, 8));
            if (isHold && !holdCapsDrawn) {
                painter.drawEllipse(QRectF(holdEndX - 3, rowCenterY - 4, 8, 8));
            }
        }

        if (noteMuriMarkers != nullptr) {
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 146, 43, 230));
            for (const miacode::timeline::TimelineMuriMarkerPlacement& markerPlacement : *noteMuriMarkers) {
                const qreal markerX = markerPlacement.useTriggerSecond
                    ? static_cast<qreal>(secondToX(markerPlacement.second) - xOffset)
                    : static_cast<qreal>(x);
                const qreal markerY = markerPlacement.useTriggerSecond
                    ? muriMarkerAnchorY(line, note, markerPlacement.second)
                    : static_cast<qreal>(rowCenterY);
                painter.drawEllipse(QRectF(markerX + 4.0, markerY - 8.0, 7.0, 7.0));
            }
            painter.restore();
        }
    };

    for (int refIndex = visibleNoteRefs.size() - 1; refIndex >= 0; --refIndex) {
        paintVisibleNote(visibleNoteRefs.at(refIndex), true);
    }
    for (const TimelineHeadLayerNoteRef& headRef : holdTapHeadRefs) {
        paintVisibleNote(headRef.ref, false);
    }
    for (const TimelineVisibleNoteRef& visibleRef : visibleNoteRefs) {
        const TimelineRenderLine& line = lines_.at(visibleRef.lineIndex);
        if (line.notes.at(visibleRef.noteIndex).kind == TimelineRenderNoteKind::Touch) {
            paintVisibleNote(visibleRef, false);
        }
    }
    for (const TimelineVisibleNoteRef& visibleRef : visibleNoteRefs) {
        const TimelineRenderLine& line = lines_.at(visibleRef.lineIndex);
        if (line.notes.at(visibleRef.noteIndex).kind == TimelineRenderNoteKind::TouchHold) {
            paintVisibleNote(visibleRef, false);
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
        const qreal entryMarkerTipY =
            static_cast<qreal>(top) - scaledTimelineHeaderMetric(kTimelineTopMarkerTipOffsetPx);
        const qreal entryMarkerBaseY = qMax<qreal>(
            0.0,
            entryMarkerTipY - scaledTimelineHeaderMetric(kTimelinePlaybackEntryMarkerHeightPx));
        const qreal entryMarkerHalfWidth = scaledTimelineHeaderMetric(kTimelinePlaybackEntryMarkerHalfWidthPx);
        QPainterPath entryMarker;
        entryMarker.moveTo(entryX, entryMarkerTipY);
        entryMarker.lineTo(entryX - entryMarkerHalfWidth, entryMarkerBaseY);
        entryMarker.lineTo(entryX + entryMarkerHalfWidth, entryMarkerBaseY);
        entryMarker.closeSubpath();
        painter.drawPath(entryMarker);
        painter.restore();
    }

    const int playheadX = secondToX(playheadSeconds_) - xOffset;
    const int cursorX = secondToX(cursorSeconds_) - xOffset;
    if (cursorX > left) {
        painter.save();
        painter.setClipRect(QRect(left, 0, viewport()->width() - left, top));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(239, 68, 68, 230));
        const qreal cursorMarkerTipY =
            static_cast<qreal>(top) - scaledTimelineHeaderMetric(kTimelineTopMarkerTipOffsetPx);
        const qreal cursorMarkerBaseY = qMax<qreal>(
            0.0,
            cursorMarkerTipY - scaledTimelineHeaderMetric(kTimelinePlaybackEntryMarkerHeightPx));
        const qreal cursorMarkerHalfWidth = scaledTimelineHeaderMetric(kTimelinePlaybackEntryMarkerHalfWidthPx);
        QPainterPath cursorMarker;
        cursorMarker.moveTo(cursorX, cursorMarkerTipY);
        cursorMarker.lineTo(cursorX - cursorMarkerHalfWidth, cursorMarkerBaseY);
        cursorMarker.lineTo(cursorX + cursorMarkerHalfWidth, cursorMarkerBaseY);
        cursorMarker.closeSubpath();
        painter.drawPath(cursorMarker);
        painter.restore();
    }
    if (!playheadIndicatorSuppressed_ && playheadX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelinePlayhead, 2));
        painter.drawLine(playheadX, top, playheadX, top + h);
        painter.restore();
    }

    if (cursorX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(c.timelineCursor, 2));
        painter.drawLine(cursorX, top, cursorX, top + h);
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
        painter.setPen(laneDividerPen);
        painter.drawLine(0, y + laneH, left, y + laneH);
        painter.setPen(c.timelineLabel);
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }
    painter.setPen(c.timelineAxis);
    painter.drawLine(left, top - 1, left, top + h);
    painter.setPen(c.timelineBorder);
    painter.drawRect(QRect(0, 0, viewport()->width() - 1, top + h));

    if (logPerf) {
        const qint64 elapsedNs = paintTimer.nsecsElapsed();
        ++debugPaintSampleCount_;
        debugPaintTotalElapsedNs_ += elapsedNs;
        debugPaintMaxElapsedNs_ = qMax(debugPaintMaxElapsedNs_, elapsedNs);
        if (elapsedNs >= 16000000) {
            appendTimelineUiPerfLog(
                QStringLiteral("kind=paint count=%1 elapsed_ms=%2 avg_ms=%3 max_ms=%4 dirty=%5x%6 visible_notes=%7 hbar=%8")
                    .arg(debugPaintSampleCount_)
                    .arg(elapsedNs / 1000000.0, 0, 'f', 3)
                    .arg(
                        debugPaintSampleCount_ > 0
                            ? (static_cast<double>(debugPaintTotalElapsedNs_) / static_cast<double>(debugPaintSampleCount_ * 1000000.0))
                            : 0.0,
                        0,
                        'f',
                        3
                    )
                    .arg(debugPaintMaxElapsedNs_ / 1000000.0, 0, 'f', 3)
                    .arg(dirtyRect.width())
                    .arg(dirtyRect.height())
                    .arg(visibleNoteRefs.size())
                    .arg(horizontalScrollBar() != nullptr ? horizontalScrollBar()->value() : 0)
            );
        }
    }
}

void TimelineView::paintWaveformOnly(QPainter& painter, const QRect& dirtyRect)
{
    const UiTheme::Colors& c = UiTheme::colors();
    const int left = timelineLeft();
    const int top = timelineTop();
    const int h = timelineHeight();
    const int xOffset = horizontalScrollBar()->value();
    const QRectF cardRect(
        static_cast<qreal>(left),
        static_cast<qreal>(top),
        qMax(1.0, static_cast<qreal>(viewport()->width() - left - 8)),
        static_cast<qreal>(h));

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(dirtyRect);
    painter.fillRect(dirtyRect, c.windowAltBg);
    painter.setPen(QPen(c.border, 1.0));
    painter.setBrush(c.cardBg);
    painter.drawRoundedRect(cardRect, 8.0, 8.0);

    if (cardRect.width() <= 2.0 || cardRect.height() <= 2.0) {
        return;
    }

    const double visibleStartSecond = xToSecond(left);
    const double visibleEndSecond = xToSecond(viewport()->width() - 8);
    const QPen minorBeatPen(
        miacode::timeline::adjustedTimelineMeasureLineColor(c.timelineGridMinor, measureLineBrightness_),
        1.0);
    const QPen majorBeatPen(
        miacode::timeline::adjustedTimelineMeasureLineColor(c.timelineGridMajor, measureLineBrightness_),
        1.5);

    painter.save();
    painter.setClipRect(cardRect.adjusted(1.0, 1.0, -1.0, -1.0));

    for (const TimelineRenderLine& line : lines_) {
        for (const TimelineRenderBeat& marker : line.beats) {
            if (!shouldPaintTimelineBeatMarker(marker)) {
                continue;
            }
            const double absoluteSecond = timelineRenderAbsoluteSecond(line, marker.secondOffset);
            if (absoluteSecond < visibleStartSecond - 1e-6 || absoluteSecond > visibleEndSecond + 1e-6) {
                continue;
            }
            const int x = secondToX(absoluteSecond) - xOffset;
            painter.setPen(minorBeatPen);
            painter.drawLine(x, top + 2, x, top + h - 2);
        }
    }

    const auto measureBeginIt = std::lower_bound(
        measureLineSeconds_.cbegin(),
        measureLineSeconds_.cend(),
        visibleStartSecond - 1.0);
    const auto measureEndIt = std::upper_bound(
        measureLineSeconds_.cbegin(),
        measureLineSeconds_.cend(),
        visibleEndSecond + 1.0);
    for (auto it = measureBeginIt; it != measureEndIt; ++it) {
        const int x = secondToX(*it) - xOffset;
        painter.setPen(majorBeatPen);
        painter.drawLine(x, top + 2, x, top + h - 2);
    }

    if (waveformData_ && waveformData_->durationSeconds > 0.0) {
        const miacode::waveform::WaveformLevel* waveformLevel =
            miacode::waveform::selectWaveformLevelForVisibleRange(
                *waveformData_,
                qMax(0.001, visibleEndSecond - visibleStartSecond),
                qMax(1, static_cast<int>(cardRect.width())));
        if (waveformLevel != nullptr && !waveformLevel->columns.isEmpty()) {
            const double phaseCompensationSeconds = qMax(0.0, waveformPhaseCompensationSeconds_);
            const QPair<int, int> visibleColumns =
                miacode::waveform::visibleWaveformColumnRange(
                    *waveformLevel,
                    qMax(0.0, visibleStartSecond + phaseCompensationSeconds),
                    qMax(0.0, visibleEndSecond + phaseCompensationSeconds));
            const qreal centerY = cardRect.center().y();
            const qreal maxAmplitude = cardRect.height() * 0.38;
            const QColor waveformColor = miacode::timeline::adjustedTimelineWaveformColor(
                c.timelineWaveStroke,
                waveformBrightness_);
            // Single anti-aliased filled silhouette (top envelope L->R, bottom
            // R->L) — matches the main timeline; smooth instead of the old
            // per-column hard-edged bars.
            QPainterPath wavePath;
            QVector<QPointF> bottomEnvelope;
            bottomEnvelope.reserve(visibleColumns.second - visibleColumns.first);
            bool wavePathStarted = false;
            for (int index = visibleColumns.first; index < visibleColumns.second; ++index) {
                const miacode::waveform::WaveformColumn& column = waveformLevel->columns.at(index);
                // Hard waveform phase compensation: logs from multiple devices
                // (60 Hz and 120 Hz) show the authoritative audio clock leading
                // the timeline-rendered playhead by about one frame. The root
                // cause is still unclear, so keep this as an explicit rendering
                // compensation rather than folding it into waveform cache data.
                const double columnStartSecond =
                    waveformLevel->secondsPerColumn * static_cast<double>(index) - phaseCompensationSeconds;
                const double columnEndSecond = columnStartSecond + waveformLevel->secondsPerColumn;
                const qreal x0 = static_cast<qreal>(secondToX(columnStartSecond) - xOffset);
                qreal x1 = static_cast<qreal>(secondToX(columnEndSecond) - xOffset);
                if (x1 <= x0) {
                    x1 = x0 + 1.0;
                }
                const qreal topY = centerY - (qBound(-1.0f, column.max, 1.0f) * maxAmplitude);
                const qreal bottomY = centerY - (qBound(-1.0f, column.min, 1.0f) * maxAmplitude);
                if (!wavePathStarted) {
                    wavePath.moveTo(x0, topY);
                    wavePathStarted = true;
                } else {
                    wavePath.lineTo(x0, topY);
                }
                wavePath.lineTo(x1, topY);
                bottomEnvelope.append(QPointF(x0, bottomY));
                bottomEnvelope.append(QPointF(x1, bottomY));
            }
            if (wavePathStarted) {
                for (int i = bottomEnvelope.size() - 1; i >= 0; --i) {
                    wavePath.lineTo(bottomEnvelope.at(i));
                }
                wavePath.closeSubpath();
                painter.save();
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setPen(Qt::NoPen);
                painter.setBrush(waveformColor);
                painter.drawPath(wavePath);
                painter.restore();
            }
        }
    }

    painter.setPen(QPen(c.textPrimary, 1.0));
    painter.drawLine(
        QPointF(cardRect.left(), cardRect.center().y()),
        QPointF(cardRect.right(), cardRect.center().y()));

    if (!playheadIndicatorSuppressed_) {
        const int playheadX = secondToX(playheadSeconds_) - xOffset;
        painter.setPen(QPen(c.timelinePlayhead, 2.0));
        painter.drawLine(playheadX, top, playheadX, top + h);
    }
    if (timelineDragActive_) {
        const int dragCenterX = viewport()->width() / 2;
        painter.setPen(QPen(c.timelinePlayhead, 2.0));
        painter.drawLine(dragCenterX, top, dragCenterX, top + h);
    }

    painter.restore();
}
