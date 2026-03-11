#include "TimelineView.h"
#include "common/AssetPaths.h"
#include "UiText.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QDockWidget>
#include <QEvent>
#include <QPalette>
#include <QToolButton>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <array>

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
constexpr double kTimelineFireworkDurationSeconds = 1.3333334;
const std::array<QColor, 5> kTimelineFireworkBandColors = {
    QColor(232, 124, 72),
    QColor(208, 106, 182),
    QColor(102, 180, 236),
    QColor(178, 202, 84),
    QColor(226, 206, 104),
};

QString laneLabelForIndex(int laneIndex)
{
    if (laneIndex >= 0 && laneIndex < kPlayableLaneCount) {
        return QString::number(laneIndex + 1);
    }
    return QString();
}

bool hasTimelineNavigateModifier(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);
}
}  // namespace

TimelineView::TimelineView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMinimumHeight(kHeaderHeight + kTimelineTopMargin + (kLaneCount * kLaneHeight) + 10);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAutoFillBackground(false);
    QPalette vp = viewport()->palette();
    vp.setColor(QPalette::Window, QColor("#F5F5F5"));
    vp.setColor(QPalette::Base, QColor("#F7F8FA"));
    vp.setColor(QPalette::Highlight, QColor("#D7EBFF"));
    viewport()->setPalette(vp);

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
    connect(zoomButton_, &QToolButton::clicked, this, [this]() { cycleZoomPreset(); });

    syncButton_ = new QToolButton(this);
    syncButton_->setAutoRaise(false);
    syncButton_->setCursor(Qt::PointingHandCursor);
    syncButton_->setStyleSheet(
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
    syncButton_->setText(UiText::isChineseUi() ? QStringLiteral("谱面位置同步") : QStringLiteral("Sync Position"));
    syncButton_->setToolTip(UiText::isChineseUi()
        ? QStringLiteral("将预览当前位置同步到光标与时间轴")
        : QStringLiteral("Sync preview position to cursor and timeline"));
    connect(syncButton_, &QToolButton::clicked, this, [this]() { emit syncPreviewRequested(); });

    updateZoomButtonAppearance();
    loadNoteIcons();
    updateHorizontalRange();
}

bool TimelineView::viewportEvent(QEvent* event)
{
    if (event == nullptr) {
        return QAbstractScrollArea::viewportEvent(event);
    }
    if (event->type() == QEvent::Paint) {
        paintEvent(static_cast<QPaintEvent*>(event));
        return true;
    }
    if (event->type() == QEvent::Show
        || event->type() == QEvent::Resize
        || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange) {
        viewport()->update();
    }
    return QAbstractScrollArea::viewportEvent(event);
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


#include "TimelineView.Paint.cpp"
#include "TimelineView.Interaction.cpp"
#include "TimelineView.Core.cpp"
