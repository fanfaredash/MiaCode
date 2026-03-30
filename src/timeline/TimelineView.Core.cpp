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
    if (waveformDurationSeconds_ > 0.0) {
        maxSecond = qMax(maxSecond, waveformStartSeconds_ + waveformDurationSeconds_);
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
    playheadIndicatorSuppressed_ = true;
    if (playheadIndicatorRestoreTimer_ != nullptr) {
        playheadIndicatorRestoreTimer_->stop();
    }
    viewport()->update();
}

void TimelineView::restorePlayheadIndicatorAfterInteraction(bool immediate)
{
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
    const int rightMargin = 4;
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
        followPreviewCheckBox_->setFixedHeight(checkBoxHeight);
        const int y = qMax(0, (timelineTop() - followPreviewCheckBox_->height()) / 2);
        const int rightX = qMax(leftBaseX, viewport()->width() - followPreviewCheckBox_->width() - rightMargin);
        followPreviewCheckBox_->move(rightX, y);
    }
}

int TimelineView::lineNumberForSecond(double second) const
{
    if (lines_.isEmpty()) {
        return qMax(1, qRound(second));
    }
    const auto it = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        second,
        [](double targetSecond, const TimelineRenderLine& line) {
            return targetSecond < (line.startSecond + 1e-6);
        });
    if (it == lines_.cbegin()) {
        return qMax(1, lines_.constFirst().lineNumber);
    }
    return qMax(1, std::prev(it)->lineNumber);
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
    const int rotationTenths = transformedPixmapRotationTenths(rotationDegrees);
    if (!mirrorX && qFuzzyCompare(normalizedScale, 1.0) && rotationTenths == 0) {
        return base;
    }

    const QString cacheKey = transformedPixmapCacheKey(type, normalizedScale, rotationDegrees, mirrorX);
    auto cacheIt = transformedIconCache_.constFind(cacheKey);
    if (cacheIt != transformedIconCache_.constEnd()) {
        return cacheIt.value();
    }

    QPixmap transformed = base;
    if (!qFuzzyCompare(normalizedScale, 1.0)) {
        transformed = transformed.scaled(
            qMax(1, qRound(static_cast<qreal>(transformed.width()) * normalizedScale)),
            qMax(1, qRound(static_cast<qreal>(transformed.height()) * normalizedScale)),
            Qt::IgnoreAspectRatio,
            Qt::SmoothTransformation);
    }
    if (mirrorX) {
        transformed = transformed.transformed(QTransform().scale(-1.0, 1.0), Qt::SmoothTransformation);
    }
    if (rotationTenths != 0) {
        transformed = transformed.transformed(
            QTransform().rotate(static_cast<qreal>(rotationTenths) / 10.0),
            Qt::SmoothTransformation);
    }
    return transformedIconCache_.insert(cacheKey, transformed).value();
}

const TimelineView::HoldPixmapParts& TimelineView::holdPixmapPartsForType(const QString& type, qreal scale) const
{
    const qreal normalizedScale = scale > 0.0 ? scale : 1.0;
    const QString cacheKey = holdPixmapCacheKey(type, normalizedScale);
    auto cacheIt = holdPixmapPartsCache_.constFind(cacheKey);
    if (cacheIt != holdPixmapPartsCache_.constEnd()) {
        return cacheIt.value();
    }

    HoldPixmapParts parts;
    const QPixmap& holdCapPixmap = transformedIconForType(type, normalizedScale, 90.0, false);
    parts.cap = holdCapPixmap;
    if (!holdCapPixmap.isNull()) {
        const QImage capImage = holdCapPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int sx = qBound(0, capImage.width() / 2, capImage.width() - 1);
        parts.bodySlice = capImage.copy(sx, 0, 1, capImage.height());
        const int splitX = qMax(1, capImage.width() / 2);
        parts.leftHalf = holdCapPixmap.copy(0, 0, splitX, holdCapPixmap.height());
        parts.rightHalf = holdCapPixmap.copy(splitX, 0, holdCapPixmap.width() - splitX, holdCapPixmap.height());
        parts.rightHalfOffset = splitX;
    }
    return holdPixmapPartsCache_.insert(cacheKey, parts).value();
}

void TimelineView::loadNoteIcons()
{
    noteIcons_.clear();
    transformedIconCache_.clear();
    holdPixmapPartsCache_.clear();
    const QString notesDir = miacode::assets::assetPath("skin");
    if (!QFileInfo::exists(QDir(notesDir).filePath("tap.png"))) {
        return;
    }

    const auto loadRawIcon = [notesDir](const QStringList& fileNames) -> QPixmap {
        for (const QString& fileName : fileNames) {
            const QString path = QDir(notesDir).filePath(fileName);
            QPixmap pix(path);
            if (pix.isNull()) {
                continue;
            }
            return pix;
        }
        return QPixmap();
    };

    const auto putScaledIcon = [this](const QString& key, const QPixmap& pix, int pixelSize) {
        if (pix.isNull()) {
            return;
        }
        noteIcons_.insert(key, pix.scaled(pixelSize, pixelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    const auto loadIcon = [&loadRawIcon, &putScaledIcon](const QString& key, const QStringList& fileNames, int pixelSize) {
        putScaledIcon(key, loadRawIcon(fileNames), pixelSize);
    };

    const auto buildTouchCompositeIcon = [this](const QPixmap& borderBase, const QPixmap& pointBase) -> QPixmap {
        if (borderBase.isNull() || pointBase.isNull()) {
            return QPixmap();
        }
        const int iconSize = kNoteSize + 3;
        QPixmap canvas(iconSize, iconSize);
        canvas.fill(Qt::transparent);

        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QPixmap border = borderBase.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((iconSize - border.width()) / 2, (iconSize - border.height()) / 2, border);

        const int pointSize = qMax(1, qRound(static_cast<qreal>(iconSize) * 0.30));
        const QPixmap point = pointBase.scaled(pointSize, pointSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((iconSize - point.width()) / 2, (iconSize - point.height()) / 2, point);
        p.end();
        return canvas;
    };

    const auto buildTouchHoldCompositeIcon = [this](const QPixmap& borderBase, const QPixmap& holdBodyBase, const QPixmap& pointBase) -> QPixmap {
        if (borderBase.isNull() || holdBodyBase.isNull()) {
            return QPixmap();
        }
        const int iconSize = kNoteSize + 3;
        QPixmap canvas(iconSize, iconSize);
        canvas.fill(Qt::transparent);

        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QPixmap border = borderBase.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((iconSize - border.width()) / 2, (iconSize - border.height()) / 2, border);

        const int bodySize = qMax(1, qRound(static_cast<qreal>(iconSize) * 0.30));
        const QPixmap body = holdBodyBase.scaled(bodySize, bodySize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((iconSize - body.width()) / 2, (iconSize - body.height()) / 2, body);

        if (!pointBase.isNull()) {
            const int pointSize = qMax(1, qRound(static_cast<qreal>(iconSize) * 0.24));
            const QPixmap point = pointBase.scaled(pointSize, pointSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawPixmap((iconSize - point.width()) / 2, (iconSize - point.height()) / 2, point);
        }

        p.end();
        return canvas;
    };

    const auto buildOverlayCompositeIcon = [](const QPixmap& base, const QPixmap& overlay, int iconSize) -> QPixmap {
        if (base.isNull()) {
            return QPixmap();
        }
        QPixmap canvas(iconSize, iconSize);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QPixmap scaledBase = base.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap((iconSize - scaledBase.width()) / 2, (iconSize - scaledBase.height()) / 2, scaledBase);
        if (!overlay.isNull()) {
            const QPixmap scaledOverlay = overlay.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawPixmap((iconSize - scaledOverlay.width()) / 2, (iconSize - scaledOverlay.height()) / 2, scaledOverlay);
        }
        p.end();
        return canvas;
    };

    loadIcon("tap", {"tap.png"}, kNoteSize);
    loadIcon("tap_break", {"tap_break.png", "tap.png"}, kNoteSize);
    loadIcon("tap_each", {"tap_each.png", "each.png", "tap.png"}, kNoteSize);
    loadIcon("hold", {"hold.png"}, kNoteSize);
    loadIcon("hold_break", {"hold_break.png", "hold.png"}, kNoteSize);
    loadIcon("hold_each", {"hold_each.png", "hold.png"}, kNoteSize);
    loadIcon("slide", {"star.png"}, kNoteSize + 3);
    loadIcon("wifi", {"star.png"}, kNoteSize + 3);
    loadIcon("star_break", {"star_break.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_break_double", {"star_break_double.png", "star_break.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_each", {"star_each.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_double", {"star_double.png", "star.png"}, kNoteSize + 3);
    loadIcon("star_each_double", {"star_each_double.png", "star_double.png", "star_each.png", "star.png"}, kNoteSize + 3);
    loadIcon("slide_track", {"slide.png"}, kNoteSize + 1);
    loadIcon("slide_track_each", {"slide_each.png", "slide.png"}, kNoteSize + 1);
    loadIcon("slide_track_break", {"slide_break.png", "slide.png"}, kNoteSize + 1);
    loadIcon("wifi_track", {"wifi_0.png", "slide.png"}, kNoteSize + 1);
    loadIcon("wifi_track_each", {"wifi_each_0.png", "wifi_0.png", "slide_each.png", "slide.png"}, kNoteSize + 1);
    loadIcon("wifi_track_break", {"wifi_break_0.png", "wifi_0.png", "slide_break.png", "slide.png"}, kNoteSize + 1);

    const QPixmap tapExComposite = buildOverlayCompositeIcon(loadRawIcon({"tap.png"}), loadRawIcon({"tap_ex.png"}), kNoteSize);
    if (!tapExComposite.isNull()) {
        noteIcons_.insert("tap_ex", tapExComposite);
    } else {
        loadIcon("tap_ex", {"tap.png"}, kNoteSize);
    }
    const QPixmap holdExComposite = buildOverlayCompositeIcon(loadRawIcon({"hold.png"}), loadRawIcon({"hold_ex.png"}), kNoteSize);
    if (!holdExComposite.isNull()) {
        noteIcons_.insert("hold_ex", holdExComposite);
    } else {
        loadIcon("hold_ex", {"hold.png"}, kNoteSize);
    }
    const QPixmap starExComposite = buildOverlayCompositeIcon(loadRawIcon({"star.png"}), loadRawIcon({"star_ex.png"}), kNoteSize + 3);
    if (!starExComposite.isNull()) {
        noteIcons_.insert("star_ex", starExComposite);
    } else {
        loadIcon("star_ex", {"star.png"}, kNoteSize + 3);
    }
    const QPixmap starExDoubleComposite = buildOverlayCompositeIcon(
        loadRawIcon({"star_double.png", "star.png"}),
        loadRawIcon({"star_ex_double.png", "star_ex.png"}),
        kNoteSize + 3
    );
    if (!starExDoubleComposite.isNull()) {
        noteIcons_.insert("star_ex_double", starExDoubleComposite);
    } else {
        loadIcon("star_ex_double", {"star_double.png", "star.png"}, kNoteSize + 3);
    }

    const QPixmap touchBorder = loadRawIcon({"touch_border_2.png", "touch.png", "touch_each.png", "each.png", "tap.png"});
    const QPixmap touchPoint = loadRawIcon({"touch_point.png", "touch_point_each.png", "tap.png"});
    const QPixmap touchBreakBorder = loadRawIcon({"touch_break_border_2.png", "touch_break.png", "touch_border_2.png", "touch.png", "touch_each.png", "each.png", "tap.png"});
    const QPixmap touchBreakPoint = loadRawIcon({"touch_break_point.png", "touch_point.png", "touch_point_each.png", "tap.png"});
    const QPixmap touchEachBorder = loadRawIcon({"touch_border_2_each.png", "touch_border_2.png", "touch_each.png", "touch.png", "each.png", "tap.png"});
    const QPixmap touchEachPoint = loadRawIcon({"touch_point_each.png", "touch_point.png", "tap.png"});

    const QPixmap touchComposite = buildTouchCompositeIcon(touchBorder, touchPoint);
    const QPixmap touchBreakComposite = buildTouchCompositeIcon(touchBreakBorder, touchBreakPoint);
    const QPixmap touchEachComposite = buildTouchCompositeIcon(touchEachBorder, touchEachPoint);
    if (!touchComposite.isNull()) {
        noteIcons_.insert("touch", touchComposite);
    } else {
        loadIcon("touch", {"touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize);
    }
    if (!touchEachComposite.isNull()) {
        noteIcons_.insert("touch_each", touchEachComposite);
    } else {
        loadIcon("touch_each", {"touch_each.png", "touch.png", "each.png", "tap.png"}, kNoteSize);
    }
    if (!touchBreakComposite.isNull()) {
        noteIcons_.insert("touch_break", touchBreakComposite);
    } else {
        loadIcon("touch_break", {"touch_break.png", "touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize);
    }

    const QPixmap touchHoldComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point.png", "tap.png"})
    );
    if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold", touchHoldComposite);
    }
    const QPixmap touchHoldEachComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point_each.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldEachComposite.isNull()) {
        noteIcons_.insert("touch_hold_each", touchHoldEachComposite);
    } else if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold_each", touchHoldComposite);
    }
    const QPixmap touchHoldBreakComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_break.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_break_point.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldBreakComposite.isNull()) {
        noteIcons_.insert("touch_hold_break", touchHoldBreakComposite);
    } else if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold_break", touchHoldComposite);
    }

}
