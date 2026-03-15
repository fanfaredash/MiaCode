void TimelineView::updateHorizontalRange()
{
    const int fullWidth = contentWidth();
    const int page = viewport()->width();
    horizontalScrollBar()->setPageStep(page);
    horizontalScrollBar()->setRange(0, qMax(0, fullWidth - page));
}

int TimelineView::contentWidth() const
{
    const double timelineSeconds = qMax(durationSeconds_, playheadSeconds_) + 1.0;
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

int TimelineView::secondToX(double second) const
{
    return timelineLeft() + qRound(second * pixelsPerSecond_);
}

double TimelineView::xToSecond(int x) const
{
    return qMax(
        0.0,
        static_cast<double>(x + horizontalScrollBar()->value() - timelineLeft()) / pixelsPerSecond_
    );
}

bool TimelineView::effectiveShowSlideTracks() const
{
    return showSlideTracks_ && zoomScale() > 0.5;
}

void TimelineView::cycleZoomPreset()
{
    zoomPresetIndex_ = (zoomPresetIndex_ + 1) % zoomPresets_.size();
    pixelsPerSecond_ = 120.0 * zoomScale();
    updateZoomButtonAppearance();
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::updateZoomButtonAppearance()
{
    if (zoomButton_ == nullptr) {
        return;
    }

    const double currentScale = zoomScale();
    const double nextScale = zoomPresets_.value((zoomPresetIndex_ + 1) % zoomPresets_.size(), currentScale);
    const QString sign = nextScale < currentScale ? "-" : "+";

    QPixmap iconPixmap(20, 20);
    iconPixmap.fill(Qt::transparent);
    QPainter p(&iconPixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#4A5568"), 1.8));
    p.drawEllipse(QRectF(3.0, 3.0, 10.0, 10.0));
    p.drawLine(QPointF(11.5, 11.5), QPointF(17.0, 17.0));
    QFont font = p.font();
    font.setBold(true);
    font.setPointSize(8);
    p.setFont(font);
    p.drawText(QRectF(13.5, 0.0, 6.5, 10.0), Qt::AlignCenter, sign);
    p.end();

    zoomButton_->setIcon(QIcon(iconPixmap));
    zoomButton_->setIconSize(iconPixmap.size());
    zoomButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    zoomButton_->setText(QString("%1x").arg(currentScale, 0, 'f', currentScale == qRound(currentScale) ? 0 : 2));
    zoomButton_->setToolTip(QString("Timeline zoom: %1x").arg(currentScale, 0, 'f', currentScale == qRound(currentScale) ? 0 : 2));
    zoomButton_->adjustSize();
    zoomButton_->setFixedHeight(24);
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
        followPreviewCheckBox_->setFixedHeight(24);
        const int y = qMax(0, (timelineTop() - followPreviewCheckBox_->height()) / 2);
        const int rightX = qMax(leftBaseX, viewport()->width() - followPreviewCheckBox_->width() - rightMargin);
        followPreviewCheckBox_->move(rightX, y);
    }
}

int TimelineView::lineNumberForSecond(double second) const
{
    if (notes_.isEmpty()) {
        return qMax(1, qRound(second));
    }
    for (const TimelineNoteMarker& note : notes_) {
        if (note.second + 1e-6 >= second) {
            return qMax(1, note.sourceLine);
        }
    }
    return qMax(1, notes_.constLast().sourceLine);
}

QPixmap TimelineView::iconForType(const QString& type) const
{
    const QString key = type.toLower();
    if (noteIcons_.contains(key)) {
        return noteIcons_.value(key);
    }
    return noteIcons_.value("tap");
}

void TimelineView::loadNoteIcons()
{
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
    const QPixmap touchBreakBorder = loadRawIcon({"touch_break.png", "touch_border_2.png", "touch.png", "touch_each.png", "each.png", "tap.png"});
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

    const QPixmap touchHoldComposite = buildTouchCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold", touchHoldComposite);
    }
    const QPixmap touchHoldBreakComposite = buildTouchCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_break.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touch_break_point.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldBreakComposite.isNull()) {
        noteIcons_.insert("touch_hold_break", touchHoldBreakComposite);
    } else if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold_break", touchHoldComposite);
    }

}

const TimelineNoteMarker* TimelineView::nearestNoteForViewportPos(const QPointF& pos) const
{
    const int xOffset = horizontalScrollBar()->value();
    const int top = timelineTop();
    const int laneH = laneHeight();
    const QRect timelineRect(timelineLeft(), top, viewport()->width() - timelineLeft(), timelineHeight());
    if (!timelineRect.contains(pos.toPoint())) {
        return nullptr;
    }

    const TimelineNoteMarker* best = nullptr;
    qreal bestDistanceSq = 0.0;
    for (const TimelineNoteMarker& note : notes_) {
        if (note.lane < 1 || note.lane > kLaneCount) {
            continue;
        }
        const qreal noteX = secondToX(note.second) - xOffset;
        const qreal noteY = top + (note.lane - 1) * laneH + (laneH / 2.0);
        const qreal dx = noteX - pos.x();
        const qreal dy = noteY - pos.y();
        const qreal distanceSq = dx * dx + dy * dy;
        if (best == nullptr || distanceSq < bestDistanceSq) {
            best = &note;
            bestDistanceSq = distanceSq;
        }
    }
    return best;
}
