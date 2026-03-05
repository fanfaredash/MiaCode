#include "TimelineView.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QDockWidget>
#include <QToolButton>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

namespace {
constexpr int kPlayableLaneCount = 8;
constexpr int kTouchLane = kPlayableLaneCount + 1;
constexpr int kLaneCount = kTouchLane;
constexpr int kHeaderHeight = 28;
constexpr int kLaneHeight = 22;
constexpr int kTimelineLeftMargin = 40;
constexpr int kTimelineTopMargin = 6;
constexpr int kTimelineRightPadding = 24;
constexpr int kNoteSize = 14;

QString laneLabelForIndex(int laneIndex)
{
    if (laneIndex >= 0 && laneIndex < kPlayableLaneCount) {
        return QString::number(laneIndex + 1);
    }
    return QString();
}
}  // namespace

TimelineView::TimelineView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setMinimumHeight(kHeaderHeight + kTimelineTopMargin + (kLaneCount * kLaneHeight) + 10);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMouseTracking(true);
    zoomButton_ = new QToolButton(this);
    zoomButton_->setAutoRaise(false);
    zoomButton_->setCursor(Qt::PointingHandCursor);
    zoomButton_->setStyleSheet(
        "QToolButton {"
        " color: #1F2E41;"
        " background: #FFFFFF;"
        " border: 1px solid #B8C7DA;"
        " border-radius: 6px;"
        " padding: 1px 8px;"
        " font-weight: 600;"
        "}"
        "QToolButton:hover { background: #F1F6FC; border-color: #89A7CB; }"
        "QToolButton:pressed { background: #E5EFFA; }"
    );
    horizontalScrollBar()->setStyleSheet(
        "QScrollBar:horizontal {"
        " background: #F4F7FB;"
        " height: 14px;"
        " margin: 2px 4px 2px 4px;"
        " border: 1px solid #D1DDEA;"
        " border-radius: 7px;"
        "}"
        "QScrollBar::handle:horizontal {"
        " background: #9AB2CC;"
        " min-width: 42px;"
        " border-radius: 6px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #7F9FBE; }"
        "QScrollBar::sub-line:horizontal, QScrollBar::add-line:horizontal {"
        " width: 20px;"
        " border: none;"
        " background: transparent;"
        "}"
        "QScrollBar::left-arrow:horizontal, QScrollBar::right-arrow:horizontal {"
        " width: 0px;"
        " height: 0px;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        " background: transparent;"
        "}"
    );
    connect(zoomButton_, &QToolButton::clicked, this, [this]() { cycleZoomPreset(); });
    updateZoomButtonAppearance();
    loadNoteIcons();
    updateHorizontalRange();
}

void TimelineView::setTimelineData(
    const QVector<TimelineBeatMarker>& beats,
    const QVector<TimelineNoteMarker>& notes,
    double durationSeconds
)
{
    beats_ = beats;
    notes_ = notes;
    std::sort(beats_.begin(), beats_.end(), [](const TimelineBeatMarker& a, const TimelineBeatMarker& b) {
        return a.second < b.second;
    });
    durationSeconds_ = qMax(0.0, durationSeconds);
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::setWaveformData(const QVector<float>& peaks, double startSecond, double durationSeconds)
{
    waveformPeaks_ = peaks;
    waveformStartSeconds_ = startSecond;
    waveformDurationSeconds_ = qMax(0.0, durationSeconds);
    viewport()->update();
}

void TimelineView::clear()
{
    beats_.clear();
    notes_.clear();
    durationSeconds_ = 0.0;
    playheadSeconds_ = 0.0;
    cursorSeconds_ = 0.0;
    playheadUpperLimitSeconds_ = -1.0;
    waveformPeaks_.clear();
    waveformStartSeconds_ = 0.0;
    waveformDurationSeconds_ = 0.0;
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::setPlayheadUpperLimitSeconds(double second)
{
    if (second > 0.0) {
        playheadUpperLimitSeconds_ = second;
    } else {
        playheadUpperLimitSeconds_ = -1.0;
    }
    if (playheadUpperLimitSeconds_ > 0.0 && playheadSeconds_ > playheadUpperLimitSeconds_) {
        playheadSeconds_ = playheadUpperLimitSeconds_;
    }
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::setPlayheadSeconds(double second, bool centerView)
{
    double clamped = qMax(0.0, second);
    if (playheadUpperLimitSeconds_ > 0.0) {
        clamped = qMin(clamped, playheadUpperLimitSeconds_);
    }
    if (qFuzzyCompare(playheadSeconds_, clamped)) {
        return;
    }

    playheadSeconds_ = clamped;
    const int playheadX = secondToX(playheadSeconds_);
    if (centerView) {
        const int targetX = playheadX - (viewport()->width() / 2);
        horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
    } else {
        const int visibleLeft = horizontalScrollBar()->value();
        const int visibleRight = visibleLeft + viewport()->width();
        const int keepMargin = 96;
        if (playheadX < visibleLeft + keepMargin || playheadX > visibleRight - keepMargin) {
            const int targetX = playheadX - (viewport()->width() / 2);
            horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
        }
    }
    viewport()->update();
    emit playheadChanged(playheadSeconds_);
}

void TimelineView::setCursorSeconds(double second)
{
    const double clamped = qMax(0.0, second);
    if (qFuzzyCompare(cursorSeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    cursorSeconds_ = clamped;
    viewport()->update();
}

double TimelineView::playheadSeconds() const
{
    return playheadSeconds_;
}

double TimelineView::durationSeconds() const
{
    return durationSeconds_;
}

void TimelineView::setShowSlideTracks(bool show)
{
    if (showSlideTracks_ == show) {
        return;
    }
    showSlideTracks_ = show;
    viewport()->update();
}

bool TimelineView::showSlideTracks() const
{
    return showSlideTracks_;
}

double TimelineView::zoomScale() const
{
    return zoomPresets_.value(zoomPresetIndex_, 1.0);
}

void TimelineView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), QColor("#F5F5F5"));

    const int left = timelineLeft();
    const int top = timelineTop();
    const int h = timelineHeight();
    const int laneH = laneHeight();
    const int xOffset = horizontalScrollBar()->value();
    const bool drawSlideTracks = effectiveShowSlideTracks();
    const QRect timelineRect(left, top, viewport()->width() - left, h);

    painter.fillRect(QRect(0, 0, viewport()->width(), top), QColor("#F3F5F8"));
    painter.fillRect(QRect(0, top, left, h), QColor("#E8E8E8"));
    painter.fillRect(timelineRect, QColor("#F7F8FA"));
    painter.setPen(QColor("#CDD7E3"));
    painter.drawLine(0, top - 1, viewport()->width(), top - 1);
    QFont laneLabelFont(QStringLiteral("Consolas"));
    laneLabelFont.setStyleHint(QFont::Monospace);
    laneLabelFont.setPointSize(12);
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
        painter.fillPath(waveformPath, QColor(88, 112, 148, 80));
        painter.setPen(QPen(QColor(88, 112, 148, 140), 1.0));
        painter.drawPath(waveformPath);
        painter.restore();
    }

    painter.setFont(laneLabelFont);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const int y = top + lane * laneH;
        const QColor rowColor = (lane % 2 == 0) ? QColor(251, 251, 251, 180) : QColor(242, 242, 242, 180);
        painter.fillRect(QRect(left, y, viewport()->width() - left, laneH), rowColor);
        painter.setPen(QColor("#D4D4D4"));
        painter.drawLine(0, y + laneH, viewport()->width(), y + laneH);
        painter.setPen(QColor("#4D5C6D"));
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }

    painter.setPen(QColor("#A8B2BE"));
    painter.drawLine(left, top - 1, left, top + h);

    int lastLabelScreenX = -1000000;
    const int headerLeftLimit = zoomButton_ != nullptr ? (zoomButton_->x() + zoomButton_->width() + 8) : 0;
    const int headerRightLimit = viewport()->width() - 4;
    for (const TimelineBeatMarker& marker : beats_) {
        const int x = secondToX(marker.second) - xOffset;
        if (x < left - 1 || x > viewport()->width()) {
            continue;
        }
        painter.setPen(marker.major ? QColor("#B6C1CE") : QColor("#D0D8E2"));
        painter.drawLine(x, top, x, top + h);
        if (marker.major) {
            const int sourceLine = marker.sourceLine > 0 ? marker.sourceLine : lineNumberForSecond(marker.second);
            const QString labelText = QString::number(sourceLine);
            const int labelWidth = 56;
            const int labelX = x - (labelWidth / 2);
            if (labelX >= headerLeftLimit
                && labelX + labelWidth <= headerRightLimit
                && labelX - lastLabelScreenX >= 22) {
                painter.setPen(QColor("#666666"));
                painter.drawText(labelX, 0, labelWidth, kHeaderHeight, Qt::AlignHCenter | Qt::AlignVCenter, labelText);
                lastLabelScreenX = labelX;
            }
        }
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
        } else if (iconType == "tap" && note.isEx) {
            iconType = "tap_ex";
        } else if (iconType == "tap" && note.isEach) {
            iconType = "tap_each";
        } else if (iconType == "hold" && note.isBreak) {
            iconType = "hold_break";
        } else if (iconType == "hold" && note.isEx) {
            iconType = "hold_ex";
        } else if (iconType == "hold" && note.isEach) {
            iconType = "hold_each";
        } else if (iconType == "touch" && note.isEach) {
            iconType = "touch_each";
        } else if (iconType == "slide" || iconType == "wifi") {
            // Star icon should follow the falling slide-head star(each) semantics,
            // while track color is controlled separately by slideEach.
            const bool headEach = note.headEach;
            if (note.headBreak && note.sameHeadSlide) {
                iconType = "star_break_double";
            } else if (note.headBreak) {
                iconType = "star_break";
            } else if (note.headEx && note.sameHeadSlide) {
                iconType = "star_ex_double";
            } else if (note.headEx) {
                iconType = "star_ex";
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
            QPixmap drawTile = baseTile;
            if (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy)) {
                const qreal angle = qRadiansToDegrees(qAtan2(dy, dx));
                drawTile = baseTile.transformed(QTransform().rotate(angle), Qt::SmoothTransformation);
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
                const qreal spacing = qMax<qreal>(6.0, static_cast<qreal>(baseTile.width()) * 0.72 + 1.5);
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
                note.isEach ? QColor(44, 214, 255, 120) : QColor(231, 76, 60, 120),
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
            painter.setBrush(QColor("#3A7AFE"));
            painter.drawEllipse(QRectF(x - 3, rowCenterY - 4, 8, 8));
            if (isHold) {
                painter.drawEllipse(QRectF(holdEndX - 3, rowCenterY - 4, 8, 8));
            }
        }
    }

    const int cursorX = secondToX(cursorSeconds_) - xOffset;
    if (cursorX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(QColor("#4A90E2"), 2));
        painter.drawLine(cursorX, top, cursorX, top + h);
        painter.restore();
    }

    const int playheadX = secondToX(playheadSeconds_) - xOffset;
    if (playheadX > left) {
        painter.save();
        painter.setClipRect(timelineRect);
        painter.setPen(QPen(QColor("#E84D4D"), 2));
        painter.drawLine(playheadX, top, playheadX, top + h);
        painter.restore();
    }

    // Overlay the gutter last so timeline objects never bleed into the lane-number column.
    painter.fillRect(QRect(0, top - 1, left + 1, h + 2), QColor("#E8E8E8"));
    painter.setFont(laneLabelFont);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const int y = top + lane * laneH;
        painter.setPen(QColor("#CCD6E2"));
        painter.drawLine(0, y + laneH, left, y + laneH);
        painter.setPen(QColor("#4D5C6D"));
        painter.drawText(4, y + 1, left - 8, laneH - 1, Qt::AlignRight | Qt::AlignVCenter, laneLabelForIndex(lane));
    }
    painter.setPen(QColor("#A8B2BE"));
    painter.drawLine(left, top - 1, left, top + h);
    painter.setPen(QColor("#C8D3E0"));
    painter.drawRect(QRect(0, 0, viewport()->width() - 1, top + h));
}

void TimelineView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    if (zoomButton_ != nullptr) {
        const int y = qMax(0, (timelineTop() - zoomButton_->height()) / 2);
        const int x = qMax(4, (timelineLeft() - zoomButton_->width()) / 2);
        zoomButton_->move(x, y);
    }
    updateHorizontalRange();
}

void TimelineView::mousePressEvent(QMouseEvent* event)
{
    if (event != nullptr
        && event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ControlModifier)) {
        const int top = timelineTop();
        const int laneH = laneHeight();
        const int laneIndex = (event->position().y() >= top)
            ? static_cast<int>((event->position().y() - top) / laneH)
            : -1;
        const int lane = (laneIndex >= 0 && laneIndex < kLaneCount) ? (laneIndex + 1) : -1;
        const double second = xToSecond(static_cast<int>(event->position().x()));
        emit ctrlClickNavigateRequested(second, lane);
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
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
    const int y = qMax(0, (timelineTop() - zoomButton_->height()) / 2);
    const int x = qMax(4, (timelineLeft() - zoomButton_->width()) / 2);
    zoomButton_->move(x, y);
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) {
        dock->setWindowTitle(QString("Timeline - %1x").arg(currentScale, 0, 'f', currentScale == qRound(currentScale) ? 0 : 2));
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
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString notesDir = QDir::cleanPath(appDir.filePath("..\\..\\assets\\skin"));
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
    const QPixmap touchEachBorder = loadRawIcon({"touch_border_2_each.png", "touch_border_2.png", "touch_each.png", "touch.png", "each.png", "tap.png"});
    const QPixmap touchEachPoint = loadRawIcon({"touch_point_each.png", "touch_point.png", "tap.png"});

    const QPixmap touchComposite = buildTouchCompositeIcon(touchBorder, touchPoint);
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

    const QPixmap touchHoldComposite = buildTouchCompositeIcon(
        loadRawIcon({"touchhold_border.png", "touch_border_2.png", "touch.png", "tap.png"}),
        loadRawIcon({"touchhold_1.png", "touch_point.png", "tap.png"})
    );
    if (!touchHoldComposite.isNull()) {
        noteIcons_.insert("touch_hold", touchHoldComposite);
    }
}
