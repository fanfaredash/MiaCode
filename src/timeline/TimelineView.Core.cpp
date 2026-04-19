void TimelineView::updateHorizontalRange()
{
    const int fullWidth = contentWidth();
    const int page = viewport()->width();
    horizontalScrollBar()->setPageStep(page);
    horizontalScrollBar()->setRange(0, qMax(0, fullWidth - page));
}

int TimelineView::contentWidth() const
{
    return rawContentWidth() + leadingCenteringPadding() + trailingCenteringPadding();
}

int TimelineView::rawContentWidth() const
{
    const double timelineSeconds = qMax(0.0, displayEndSeconds_ - displayStartSeconds_);
    return timelineLeft() + static_cast<int>(timelineSeconds * pixelsPerSecond_) + kTimelineRightPadding;
}

int TimelineView::timelineLeft() const
{
    return kTimelineLeftMargin;
}

int TimelineView::timelineTop() const
{
    return kHeaderHeight + kTimelineTopMargin;
}

int TimelineView::laneHeight() const
{
    return kLaneHeight;
}

int TimelineView::timelineHeight() const
{
    return kLaneCount * laneHeight();
}

int TimelineView::notePixelSize() const
{
    return kNoteSize;
}

int TimelineView::rawSecondToX(double second) const
{
    return timelineLeft() + qRound((second - displayStartSeconds_) * pixelsPerSecond_);
}

int TimelineView::secondToX(double second) const
{
    return rawSecondToX(second) + leadingCenteringPadding();
}

double TimelineView::xToSecond(int x) const
{
    return qMax(
        0.0,
        displayStartSeconds_
            + (static_cast<double>(
                   x + horizontalScrollBar()->value() - leadingCenteringPadding() - timelineLeft()
               )
               / pixelsPerSecond_)
    );
}

double TimelineView::maxNavigableSecond() const
{
    double maxSecond = qMax(
        0.0,
        qMax(maximumDataSecond_, qMax(durationSeconds_, qMax(playbackEntrySeconds_, qMax(playheadSeconds_, qMax(cursorSeconds_, 0.0)))))
    );
    if (playheadUpperLimitSeconds_ > 0.0) {
        maxSecond = qMax(maxSecond, playheadUpperLimitSeconds_);
    }
    if (waveformData_ && waveformData_->durationSeconds > 0.0) {
        maxSecond = qMax(maxSecond, waveformData_->durationSeconds);
    }
    return maxSecond;
}

int TimelineView::leadingCenteringPadding() const
{
    const int viewportCenterX = viewport()->width() / 2;
    return qMax(0, viewportCenterX - rawSecondToX(0.0));
}

int TimelineView::trailingCenteringPadding() const
{
    const int viewportCenterX = viewport()->width() / 2;
    return qMax(0, rawSecondToX(maxNavigableSecond()) + viewportCenterX - rawContentWidth());
}

double TimelineView::viewportCenterSecond() const
{
    return xToSecond(viewport()->width() / 2);
}

void TimelineView::updatePlayheadToViewportCenter(bool emitNavigate)
{
    const double centerSecond = viewportCenterSecond();
    const bool changed = !qFuzzyCompare(playheadSeconds_ + 1.0, centerSecond + 1.0);
    setPlayheadSeconds(centerSecond, false);
    if (emitNavigate && changed) {
        emit centerNavigateRequested(centerSecond);
    }
}

bool TimelineView::playheadNearViewportCenter() const
{
    return qAbs(playheadSeconds_ - viewportCenterSecond()) <= (0.5 / pixelsPerSecond_);
}

void TimelineView::suppressPlayheadIndicatorForInteraction()
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->suppressPlayheadIndicator();
        return;
    }
    playheadIndicatorSuppressed_ = true;
    if (playheadIndicatorRestoreTimer_ != nullptr) {
        playheadIndicatorRestoreTimer_->stop();
    }
    viewport()->update();
}

void TimelineView::restorePlayheadIndicatorAfterInteraction(bool immediate)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->restorePlayheadIndicator(immediate);
        return;
    }
    if (timelineDragActive_) {
        return;
    }
    if (immediate || playheadIndicatorRestoreTimer_ == nullptr) {
        if (playheadIndicatorRestoreTimer_ != nullptr) {
            playheadIndicatorRestoreTimer_->stop();
        }
        playheadIndicatorSuppressed_ = false;
        viewport()->update();
    } else {
        playheadIndicatorRestoreTimer_->start();
    }
}

bool TimelineView::effectiveShowSlideTracks() const
{
    return showSlideTracks_;
}

void TimelineView::cycleZoomPreset()
{
    if (buttonZoomPresets_.isEmpty()) {
        return;
    }
    const double currentScale = zoomScale();
    double nextScale = buttonZoomPresets_.constFirst();
    bool foundHigherPreset = false;
    for (double preset : buttonZoomPresets_) {
        if (preset > currentScale + 1e-6) {
            nextScale = preset;
            foundHigherPreset = true;
            break;
        }
    }
    if (!foundHigherPreset) {
        nextScale = buttonZoomPresets_.constFirst();
    }
    applyZoomPresetIndex(qMax(0, zoomPresets_.indexOf(nextScale)), viewportCenterSecond());
}

void TimelineView::applyZoomPresetIndex(int nextIndex, double anchorSecond)
{
    if (zoomPresets_.isEmpty()) {
        return;
    }
    nextIndex = qBound(0, nextIndex, zoomPresets_.size() - 1);
    if (nextIndex == zoomPresetIndex_) {
        return;
    }
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->stepZoomPreset(nextIndex - zoomPresetIndex_, anchorSecond);
        return;
    }
    zoomPresetIndex_ = nextIndex;
    pixelsPerSecond_ = 120.0 * zoomScale();
    updateZoomButtonAppearance();
    updateHorizontalRange();
    const int anchorX = secondToX(anchorSecond) - (viewport()->width() / 2);
    horizontalScrollBar()->setValue(
        qBound(horizontalScrollBar()->minimum(), anchorX, horizontalScrollBar()->maximum())
    );
    viewport()->update();
}

void TimelineView::stepZoomPreset(int deltaSteps, double anchorSecond)
{
    if (zoomPresets_.isEmpty() || deltaSteps == 0) {
        return;
    }
    applyZoomPresetIndex(zoomPresetIndex_ + deltaSteps, anchorSecond);
}

bool TimelineView::handleAltZoomWheel(QWheelEvent* event)
{
    if (event == nullptr || !event->modifiers().testFlag(Qt::AltModifier)) {
        return false;
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
        return false;
    }

    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    stepZoomPreset(steps, viewportCenterSecond());
    event->accept();
    return true;
}

void TimelineView::updateZoomButtonAppearance()
{
    if (zoomButton_ == nullptr) {
        return;
    }
    const UiTheme::Colors& c = UiTheme::colors();

    const double currentScale = zoomScale();
    double nextScale = buttonZoomPresets_.value(0, currentScale);
    for (double preset : buttonZoomPresets_) {
        if (preset > currentScale + 1e-6) {
            nextScale = preset;
            break;
        }
    }
    const QString sign = nextScale < currentScale ? "-" : "+";

    QPixmap iconPixmap(18, 18);
    iconPixmap.fill(Qt::transparent);
    QPainter p(&iconPixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(c.timelineLabel, 1.8));
    p.drawEllipse(QRectF(2.5, 2.5, 9.0, 9.0));
    p.drawLine(QPointF(10.5, 10.5), QPointF(15.2, 15.2));
    QFont font = p.font();
    font.setBold(true);
    font.setPointSize(7);
    p.setFont(font);
    p.drawText(QRectF(11.5, 0.0, 6.5, 9.0), Qt::AlignCenter, sign);
    p.end();

    zoomButton_->setIcon(QIcon(iconPixmap));
    zoomButton_->setIconSize(iconPixmap.size());
    zoomButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const int currentPercent = qRound(currentScale * 100.0);
    zoomButton_->setText(QString("%1%").arg(currentPercent));
    zoomButton_->setToolTip(QString("Timeline zoom: %1%").arg(currentPercent));
    zoomButton_->adjustSize();
    zoomButton_->setFixedHeight(22);
    layoutHeaderButtons();
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) {
        dock->setWindowTitle(QString("Timeline - %1x").arg(currentScale, 0, 'f', currentScale == qRound(currentScale) ? 0 : 2));
    }
}

void TimelineView::layoutHeaderButtons()
{
    const int leftBaseX = 4;
    const int rightMargin = 8;
    int x = leftBaseX;
    if (zoomButton_ != nullptr) {
        const int y = qMax(0, (timelineTop() - zoomButton_->height()) / 2);
        zoomButton_->move(x, y);
    }
    if (followPreviewCheckBox_ != nullptr) {
        followPreviewCheckBox_->adjustSize();
        const int checkBoxHeight = qMax(
            followPreviewCheckBox_->minimumSizeHint().height(),
            followPreviewCheckBox_->sizeHint().height()
        );
        const int checkBoxWidth = qMax(
            followPreviewCheckBox_->minimumSizeHint().width(),
            followPreviewCheckBox_->sizeHint().width()
        );
        followPreviewCheckBox_->setFixedWidth(checkBoxWidth);
        followPreviewCheckBox_->setFixedHeight(checkBoxHeight);
        const int y = qMax(0, (timelineTop() - followPreviewCheckBox_->height()) / 2);
        const int rightX = qMax(leftBaseX, viewport()->width() - followPreviewCheckBox_->width() - rightMargin);
        followPreviewCheckBox_->move(rightX, y);
    }
}

QVector<TimelineView::HeaderLineLabel> TimelineView::visibleHeaderLineLabels(
    double startSecond,
    double endSecond,
    int xOffset,
    int headerLeftLimit,
    int headerRightLimit) const
{
    QVector<HeaderLineLabel> labels;
    if (lines_.isEmpty() || viewport() == nullptr || headerRightLimit < headerLeftLimit) {
        return labels;
    }

    const double boundedStart = qMin(startSecond, endSecond);
    const double boundedEnd = qMax(startSecond, endSecond);
    const auto beginIt = std::lower_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedStart - kTimelineHeaderLineAnchorToleranceSeconds,
        [](const TimelineRenderLine& line, double targetSecond) {
            return line.startSecond < targetSecond;
        });
    const auto endIt = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedEnd + kTimelineHeaderLineAnchorToleranceSeconds,
        [](double targetSecond, const TimelineRenderLine& line) {
            return targetSecond < line.startSecond;
        });

    QVector<HeaderLineLabel> collapsed;
    collapsed.reserve(static_cast<int>(std::distance(beginIt, endIt)));
    const int left = timelineLeft();
    const int viewWidth = viewport()->width();

    for (auto it = beginIt; it != endIt; ++it) {
        const TimelineRenderLine& line = *it;
        const int screenX = secondToX(line.startSecond) - xOffset;
        if (screenX < left - 1 || screenX > viewWidth) {
            continue;
        }

        HeaderLineLabel label;
        label.lineNumber = qMax(1, line.lineNumber);
        label.second = line.startSecond;
        label.screenX = screenX;

        if (!collapsed.isEmpty()) {
            HeaderLineLabel& previous = collapsed.last();
            if (previous.screenX == label.screenX
                || qAbs(previous.second - label.second) <= kTimelineHeaderLineAnchorToleranceSeconds) {
                previous = label;
                continue;
            }
        }

        collapsed.append(label);
    }

    labels.reserve(collapsed.size());
    for (const HeaderLineLabel& label : collapsed) {
        const QString labelText = QString::number(label.lineNumber);
        const int labelHalfWidth = timelineHeaderLabelHalfWidthPx(headerLineNumberFont_, labelText);
        if (label.screenX - labelHalfWidth < headerLeftLimit
            || label.screenX + labelHalfWidth > headerRightLimit) {
            continue;
        }
        if (!labels.isEmpty()
            && label.screenX - labels.constLast().screenX < kTimelineHeaderLineLabelMinSpacingPx) {
            continue;
        }
        labels.append(label);
    }

    return labels;
}

TimelineView::VisibleLineRange TimelineView::visibleLineRange(double startSecond, double endSecond) const
{
    VisibleLineRange range;
    if (lines_.isEmpty()) {
        return range;
    }

    const double boundedStart = qMin(startSecond, endSecond);
    const double boundedEnd = qMax(startSecond, endSecond);
    const auto beginIt = std::lower_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedStart,
        [](const TimelineRenderLine& line, double targetSecond) {
            return line.startSecond < targetSecond;
        });
    const auto endIt = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedEnd,
        [](double targetSecond, const TimelineRenderLine& line) {
            return targetSecond < line.startSecond;
        });

    range.begin = static_cast<int>(std::distance(lines_.cbegin(), beginIt));
    range.end = static_cast<int>(std::distance(lines_.cbegin(), endIt));
    while (range.begin > 0 && lines_.at(range.begin - 1).endSecond + 1e-6 >= boundedStart) {
        --range.begin;
    }
    while (range.end < lines_.size() && lines_.at(range.end).startSecond <= boundedEnd + 1e-6) {
        ++range.end;
    }
    return range;
}

void TimelineView::updateTimelineMarkerStrip(double oldSecond, double newSecond, int halfWidth)
{
    if (viewport() == nullptr) {
        return;
    }

    QRect dirtyRect;
    const int xOffset = horizontalScrollBar()->value();
    const int top = timelineTop();
    const int height = timelineHeight();
    const QRect timelineRect(timelineLeft(), top, viewport()->width() - timelineLeft(), height);

    const auto addSecondRect = [&](double second) {
        if (!qIsFinite(second) || second < 0.0) {
            return;
        }
        const int x = secondToX(second) - xOffset;
        QRect lineRect(x - halfWidth - 2, top, halfWidth * 2 + 5, height);
        lineRect = lineRect.intersected(timelineRect);
        if (lineRect.isEmpty()) {
            return;
        }
        dirtyRect = dirtyRect.isNull() ? lineRect : dirtyRect.united(lineRect);
    };

    addSecondRect(oldSecond);
    addSecondRect(newSecond);
    if (!dirtyRect.isNull()) {
        viewport()->update(dirtyRect);
    } else {
        viewport()->update();
    }
}

const QPixmap& TimelineView::iconForType(const QString& type) const
{
    auto directIt = noteIcons_.constFind(type);
    if (directIt != noteIcons_.constEnd()) {
        return directIt.value();
    }
    const QString lower = type.toLower();
    if (lower != type) {
        auto lowerIt = noteIcons_.constFind(lower);
        if (lowerIt != noteIcons_.constEnd()) {
            return lowerIt.value();
        }
    }
    auto fallbackIt = noteIcons_.constFind(QStringLiteral("tap"));
    if (fallbackIt != noteIcons_.constEnd()) {
        return fallbackIt.value();
    }
    static const QPixmap kNullPixmap;
    return kNullPixmap;
}

int TimelineView::iconBasePixelSizeForType(const QString& type) const
{
    return miacode::timeline::noteIconBasePixelSizeForType(
        miacode::timeline::TimelineNoteAssetSet{noteIcons_, noteIconBasePixelSizes_},
        type,
        notePixelSize());
}

QSize TimelineView::targetSizeForIconType(const QString& type, qreal scale) const
{
    return miacode::timeline::targetSizeForNoteType(
        miacode::timeline::TimelineNoteAssetSet{noteIcons_, noteIconBasePixelSizes_},
        type,
        scale,
        notePixelSize());
}

const QPixmap& TimelineView::transformedIconForType(
    const QString& type,
    qreal scale,
    qreal rotationDegrees,
    bool mirrorX) const
{
    const QPixmap& base = iconForType(type);
    if (base.isNull()) {
        return base;
    }

    const qreal normalizedScale = scale > 0.0 ? scale : 1.0;
    const QSize targetSize = targetSizeForIconType(type, normalizedScale);
    if (!targetSize.isValid()) {
        return base;
    }
    const int rotationTenths = miacode::timeline::transformedPixmapRotationTenths(rotationDegrees);
    if (!mirrorX && rotationTenths == 0 && targetSize == base.size()) {
        return base;
    }

    const QString cacheKey = miacode::timeline::transformedPixmapCacheKey(type, normalizedScale, rotationDegrees, mirrorX);
    auto cacheIt = transformedIconCache_.constFind(cacheKey);
    if (cacheIt != transformedIconCache_.constEnd()) {
        return cacheIt.value();
    }

    const QPixmap transformed = miacode::timeline::transformNotePixmapToTargetSize(
        miacode::timeline::TimelineNoteAssetSet{noteIcons_, noteIconBasePixelSizes_},
        type,
        targetSize,
        rotationTenths != 0 ? (static_cast<qreal>(rotationTenths) / 10.0) : 0.0,
        mirrorX);
    return transformedIconCache_.insert(cacheKey, transformed).value();
}

qreal TimelineView::holdScaleForBaseIconScale(const QString& type, qreal baseIconScale) const
{
    return miacode::timeline::holdScaleForBaseIconScale(
        miacode::timeline::TimelineNoteAssetSet{noteIcons_, noteIconBasePixelSizes_},
        type,
        baseIconScale,
        notePixelSize());
}

const TimelineView::HoldPixmapParts& TimelineView::holdPixmapPartsForType(const QString& type, qreal scale) const
{
    const qreal normalizedScale = scale > 0.0 ? scale : 1.0;
    const QString cacheKey = miacode::timeline::holdPixmapCacheKey(type, normalizedScale);
    auto cacheIt = holdPixmapPartsCache_.constFind(cacheKey);
    if (cacheIt != holdPixmapPartsCache_.constEnd()) {
        return cacheIt.value();
    }

    const HoldPixmapParts parts = miacode::timeline::buildHoldPixmapParts(
        miacode::timeline::TimelineNoteAssetSet{noteIcons_, noteIconBasePixelSizes_},
        type,
        normalizedScale,
        notePixelSize());
    return holdPixmapPartsCache_.insert(cacheKey, parts).value();
}

void TimelineView::loadNoteIcons()
{
    const miacode::timeline::TimelineNoteAssetSet assets = miacode::timeline::loadTimelineNoteAssets();
    noteIcons_ = assets.noteIcons;
    noteIconBasePixelSizes_ = assets.noteIconBasePixelSizes;
    transformedIconCache_.clear();
    holdPixmapPartsCache_.clear();
    prewarmTransformedIconCache();
}

void TimelineView::prewarmTransformedIconCache()
{
    if (noteIcons_.isEmpty()) {
        return;
    }

    const QVector<qreal> baseScales = {0.5, 1.0};
    const QStringList iconTypes = noteIcons_.keys();
    for (const QString& iconType : iconTypes) {
        for (qreal baseScale : baseScales) {
            transformedIconForType(iconType, baseScale);
        }
    }

    const QStringList holdTypes = {
        QStringLiteral("hold"),
        QStringLiteral("hold_break"),
        QStringLiteral("hold_each"),
        QStringLiteral("hold_ex"),
        QStringLiteral("hold_break_ex"),
        QStringLiteral("hold_each_ex"),
    };
    for (const QString& holdType : holdTypes) {
        if (!noteIcons_.contains(holdType)) {
            continue;
        }
        for (qreal baseScale : baseScales) {
            const qreal holdScale = holdScaleForBaseIconScale(holdType, baseScale);
            transformedIconForType(holdType, holdScale);
            transformedIconForType(holdType, holdScale, 90.0, false);
            holdPixmapPartsForType(holdType, holdScale);
        }
    }

    const QVector<qreal> trackScales = {0.25, 0.5, 0.75, 1.0};
    const QStringList trackTypes = {
        QStringLiteral("slide_track"),
        QStringLiteral("slide_track_each"),
        QStringLiteral("slide_track_break"),
        QStringLiteral("wifi_track"),
        QStringLiteral("wifi_track_each"),
        QStringLiteral("wifi_track_break"),
    };
    for (const QString& trackType : trackTypes) {
        if (!noteIcons_.contains(trackType)) {
            continue;
        }
        for (qreal trackScale : trackScales) {
            transformedIconForType(trackType, trackScale, 0.0, false);
            transformedIconForType(trackType, trackScale, 0.0, true);
        }
    }
}
