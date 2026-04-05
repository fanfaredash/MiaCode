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

int TimelineView::iconBasePixelSizeForType(const QString& type) const
{
    auto directIt = noteIconBasePixelSizes_.constFind(type);
    if (directIt != noteIconBasePixelSizes_.constEnd()) {
        return directIt.value();
    }
    const QString lower = type.toLower();
    if (lower != type) {
        auto lowerIt = noteIconBasePixelSizes_.constFind(lower);
        if (lowerIt != noteIconBasePixelSizes_.constEnd()) {
            return lowerIt.value();
        }
    }
    auto fallbackIt = noteIconBasePixelSizes_.constFind(QStringLiteral("tap"));
    if (fallbackIt != noteIconBasePixelSizes_.constEnd()) {
        return fallbackIt.value();
    }
    return notePixelSize();
}

QSize TimelineView::targetSizeForIconType(const QString& type, qreal scale) const
{
    const QPixmap& base = iconForType(type);
    if (base.isNull()) {
        return QSize();
    }

    const qreal normalizedScale = scale > 0.0 ? scale : 1.0;
    const int targetBox = qMax(1, qRound(static_cast<qreal>(iconBasePixelSizeForType(type)) * normalizedScale));
    QSize targetSize(base.width(), base.height());
    targetSize.scale(targetBox, targetBox, Qt::KeepAspectRatio);
    targetSize.setWidth(qMax(1, targetSize.width()));
    targetSize.setHeight(qMax(1, targetSize.height()));
    return targetSize;
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
    const int rotationTenths = transformedPixmapRotationTenths(rotationDegrees);
    if (!mirrorX && rotationTenths == 0 && targetSize == base.size()) {
        return base;
    }

    const QString cacheKey = transformedPixmapCacheKey(type, normalizedScale, rotationDegrees, mirrorX);
    auto cacheIt = transformedIconCache_.constFind(cacheKey);
    if (cacheIt != transformedIconCache_.constEnd()) {
        return cacheIt.value();
    }

    QPixmap transformed = base;
    if (targetSize != base.size()) {
        transformed = transformed.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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

qreal TimelineView::holdScaleForBaseIconScale(const QString& type, qreal baseIconScale) const
{
    const qreal normalizedBaseScale = baseIconScale > 0.0 ? baseIconScale : 1.0;
    const QPixmap& tapReference = transformedIconForType(QStringLiteral("tap"), normalizedBaseScale);
    const QPixmap& holdReference = transformedIconForType(type, normalizedBaseScale, 90.0, false);
    if (tapReference.isNull() || holdReference.isNull() || holdReference.height() <= 0) {
        return normalizedBaseScale;
    }

    const qreal desiredThickness =
        static_cast<qreal>(tapReference.height()) * kTimelineHoldThicknessRelativeToTap;
    return normalizedBaseScale * (desiredThickness / static_cast<qreal>(holdReference.height()));
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
    if (!holdCapPixmap.isNull()) {
        const QImage capImage = holdCapPixmap.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int midX = qBound(0, capImage.width() / 2, capImage.width() - 1);
        const int maxCapWidth = qMax(1, capImage.width() / 2);
        const int capWidth = qMax(
            1,
            qMin(
                maxCapWidth,
                qRound(
                    static_cast<qreal>(capImage.width())
                    * static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioNumerator)
                    / static_cast<qreal>(miacode::preview_skin::kHoldCapSliceRatioDenominator))
            ));
        parts.bodySlice = capImage.copy(midX, 0, 1, capImage.height());
        parts.leftCap = holdCapPixmap.copy(0, 0, capWidth, holdCapPixmap.height());
        parts.rightCap = holdCapPixmap.copy(
            qMax(0, holdCapPixmap.width() - capWidth),
            0,
            capWidth,
            holdCapPixmap.height());
    }
    return holdPixmapPartsCache_.insert(cacheKey, parts).value();
}

void TimelineView::loadNoteIcons()
{
    noteIcons_.clear();
    noteIconBasePixelSizes_.clear();
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

    const auto putIcon = [this](const QString& key, const QPixmap& pix, int basePixelSize) {
        if (pix.isNull()) {
            return;
        }
        noteIcons_.insert(key, pix);
        noteIconBasePixelSizes_.insert(key, qMax(1, basePixelSize));
    };

    const auto loadIcon = [&loadRawIcon, &putIcon](const QString& key, const QStringList& fileNames, int basePixelSize) {
        putIcon(key, loadRawIcon(fileNames), basePixelSize);
    };

    const auto buildCenteredCompositeIcon = [](std::initializer_list<const QPixmap*> layers) -> QPixmap {
        int canvasWidth = 0;
        int canvasHeight = 0;
        for (const QPixmap* layer : layers) {
            if (layer == nullptr || layer->isNull()) {
                continue;
            }
            canvasWidth = qMax(canvasWidth, layer->width());
            canvasHeight = qMax(canvasHeight, layer->height());
        }
        if (canvasWidth <= 0 || canvasHeight <= 0) {
            return QPixmap();
        }

        QPixmap canvas(canvasWidth, canvasHeight);
        canvas.fill(Qt::transparent);

        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (const QPixmap* layer : layers) {
            if (layer == nullptr || layer->isNull()) {
                continue;
            }
            p.drawPixmap((canvasWidth - layer->width()) / 2, (canvasHeight - layer->height()) / 2, *layer);
        }
        p.end();
        return canvas;
    };

    const auto buildTouchCompositeIcon = [&buildCenteredCompositeIcon](const QPixmap& borderBase, const QPixmap& pointBase) -> QPixmap {
        if (borderBase.isNull() || pointBase.isNull()) {
            return QPixmap();
        }
        return buildCenteredCompositeIcon({&borderBase, &pointBase});
    };

    const auto buildTouchHoldCompositeIcon = [&buildCenteredCompositeIcon](const QPixmap& borderBase, const QPixmap& holdBodyBase, const QPixmap& pointBase) -> QPixmap {
        if (borderBase.isNull() || holdBodyBase.isNull()) {
            return QPixmap();
        }
        if (pointBase.isNull()) {
            return buildCenteredCompositeIcon({&borderBase, &holdBodyBase});
        }
        return buildCenteredCompositeIcon({&borderBase, &holdBodyBase, &pointBase});
    };

    const auto buildOverlayCompositeIcon = [&buildCenteredCompositeIcon](const QPixmap& base, const QPixmap& overlay) -> QPixmap {
        if (base.isNull()) {
            return QPixmap();
        }
        return overlay.isNull()
            ? buildCenteredCompositeIcon({&base})
            : buildCenteredCompositeIcon({&base, &overlay});
    };

    const auto putOverlayCompositeIcon =
        [&buildOverlayCompositeIcon, &loadRawIcon, &loadIcon, &putIcon](
            const QString& key,
            const QStringList& baseFileNames,
            const QStringList& overlayFileNames,
            int basePixelSize) {
            const QPixmap composite = buildOverlayCompositeIcon(loadRawIcon(baseFileNames), loadRawIcon(overlayFileNames));
            if (!composite.isNull()) {
                putIcon(key, composite, basePixelSize);
            } else {
                loadIcon(key, baseFileNames, basePixelSize);
            }
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
    loadIcon("slide_track", {"slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("slide_track_each", {"slide_each.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("slide_track_break", {"slide_break.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track", {"wifi_0.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track_each", {"wifi_each_0.png", "wifi_0.png", "slide_each.png", "slide.png"}, kSlideTrackBasePixelSize);
    loadIcon("wifi_track_break", {"wifi_break_0.png", "wifi_0.png", "slide_break.png", "slide.png"}, kSlideTrackBasePixelSize);

    putOverlayCompositeIcon("tap_ex", {"tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("tap_break_ex", {"tap_break.png", "tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("tap_each_ex", {"tap_each.png", "each.png", "tap.png"}, {"tap_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_ex", {"hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_break_ex", {"hold_break.png", "hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("hold_each_ex", {"hold_each.png", "hold.png"}, {"hold_ex.png"}, kNoteSize);
    putOverlayCompositeIcon("star_ex", {"star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon("star_break_ex", {"star_break.png", "star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon("star_each_ex", {"star_each.png", "star.png"}, {"star_ex.png"}, kNoteSize + 3);
    putOverlayCompositeIcon(
        "star_ex_double",
        {"star_double.png", "star.png"},
        {"star_ex_double.png", "star_ex.png"},
        kNoteSize + 3
    );
    putOverlayCompositeIcon(
        "star_break_ex_double",
        {"star_break_double.png", "star_break.png", "star.png"},
        {"star_ex_double.png", "star_ex.png"},
        kNoteSize + 3
    );
    putOverlayCompositeIcon(
        "star_each_ex_double",
        {"star_each_double.png", "star_double.png", "star_each.png", "star.png"},
        {"star_ex_double.png", "star_ex.png"},
        kNoteSize + 3
    );

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
        putIcon("touch", touchComposite, kNoteSize + 3);
    } else {
        loadIcon("touch", {"touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize + 3);
    }
    if (!touchEachComposite.isNull()) {
        putIcon("touch_each", touchEachComposite, kNoteSize + 3);
    } else {
        loadIcon("touch_each", {"touch_each.png", "touch.png", "each.png", "tap.png"}, kNoteSize + 3);
    }
    if (!touchBreakComposite.isNull()) {
        putIcon("touch_break", touchBreakComposite, kNoteSize + 3);
    } else {
        loadIcon("touch_break", {"touch_break.png", "touch.png", "touch_each.png", "each.png", "tap.png"}, kNoteSize + 3);
    }

    const QPixmap touchHoldComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point.png", "tap.png"})
    );
    if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold", touchHoldComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldEachComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_point_each.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldEachComposite.isNull()) {
        putIcon("touch_hold_each", touchHoldEachComposite, kNoteSize + 3);
    } else if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold_each", touchHoldComposite, kNoteSize + 3);
    }
    const QPixmap touchHoldBreakComposite = buildTouchHoldCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_break.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "tap.png"}),
        loadRawIcon({"touch_break_point.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldBreakComposite.isNull()) {
        putIcon("touch_hold_break", touchHoldBreakComposite, kNoteSize + 3);
    } else if (!touchHoldComposite.isNull()) {
        putIcon("touch_hold_break", touchHoldComposite, kNoteSize + 3);
    }
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
