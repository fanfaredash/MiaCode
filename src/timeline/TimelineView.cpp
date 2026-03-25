#include "TimelineView.h"
#include "common/AssetPaths.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QIcon>
#include <QCheckBox>
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
constexpr int kLaneHeight = 20;
constexpr int kTimelineLeftMargin = 40;
constexpr int kTimelineTopMargin = 6;
constexpr int kTimelineRightPadding = 24;
constexpr int kNoteSize = 14;
constexpr double kTimelineDisplayLeadInSeconds = 0.5;
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

QVector<double> makeTimelineZoomPresets()
{
    QVector<double> presets;
    presets.reserve(((200 - 25) / 5) + 1);
    for (int percent = 25; percent <= 200; percent += 5) {
        presets.append(static_cast<double>(percent) / 100.0);
    }
    return presets;
}

QVector<double> makeTimelineButtonZoomPresets()
{
    return {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
}
}  // namespace

TimelineView::TimelineView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAutoFillBackground(false);

    zoomButton_ = new QToolButton(this);
    zoomButton_->setAutoRaise(false);
    zoomButton_->setCursor(Qt::PointingHandCursor);
    connect(zoomButton_, &QToolButton::clicked, this, [this]() { cycleZoomPreset(); });
    followPreviewCheckBox_ = new QCheckBox(this);
    followPreviewCheckBox_->setCursor(Qt::PointingHandCursor);
    followPreviewCheckBox_->setText(
        UiText::isChineseUi()
            ? QStringLiteral("\u8ddf\u968f\u9884\u89c8")
            : QStringLiteral("Follow Preview")
    );
    followPreviewCheckBox_->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("\u6309\u9884\u89c8\u65f6\u95f4\u5f3a\u5236\u8ddf\u968f\u5b9a\u4f4d\u7f16\u8f91\u5668\u5149\u6807")
            : QStringLiteral("Force editor cursor to follow current preview time")
    );
    connect(followPreviewCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        emit followPreviewToggled(enabled);
    });

    headerLineNumberFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    headerLineNumberFont_.setStyleHint(QFont::Monospace);
    headerLineNumberFont_.setFixedPitch(true);
    zoomPresets_ = makeTimelineZoomPresets();
    buttonZoomPresets_ = makeTimelineButtonZoomPresets();
    zoomPresetIndex_ = qMax(0, zoomPresets_.indexOf(1.0));

    refreshTheme();
    updateZoomButtonAppearance();
    loadNoteIcons();
    refreshMinimumHeightForCurrentDevice();
    playheadIndicatorRestoreTimer_ = new QTimer(this);
    playheadIndicatorRestoreTimer_->setSingleShot(true);
    playheadIndicatorRestoreTimer_->setInterval(180);
    connect(playheadIndicatorRestoreTimer_, &QTimer::timeout, this, [this]() {
        if (timelineDragActive_) {
            return;
        }
        playheadIndicatorSuppressed_ = false;
        viewport()->update();
    });
    updateHorizontalRange();
}

QSize TimelineView::minimumSizeHint() const
{
    return QSize(kTimelineLeftMargin + 240, minimumContentHeightForCurrentDevice());
}

QSize TimelineView::sizeHint() const
{
    return minimumSizeHint();
}

void TimelineView::setHeaderLineNumberFont(const QFont& font)
{
    if (headerLineNumberFont_ == font) {
        return;
    }
    headerLineNumberFont_ = font;
    refreshMinimumHeightForCurrentDevice();
    viewport()->update();
}

void TimelineView::refreshTheme()
{
    viewport()->setPalette(UiTheme::timelineViewportPalette());
    if (zoomButton_ != nullptr) {
        zoomButton_->setStyleSheet(UiTheme::timelineZoomButtonStyleSheet());
    }
    if (followPreviewCheckBox_ != nullptr) {
        followPreviewCheckBox_->setStyleSheet(UiTheme::timelineCheckBoxStyleSheet());
    }
    refreshMinimumHeightForCurrentDevice();
    viewport()->update();
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
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::setWaveformData(const QVector<float>& peaks, double startSecond, double durationSeconds)
{
    waveformPeaks_ = peaks;
    waveformStartSeconds_ = startSecond;
    waveformDurationSeconds_ = qMax(0.0, durationSeconds);
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
}

void TimelineView::clear()
{
    beats_.clear();
    notes_.clear();
    muriMarkerKeys_.clear();
    durationSeconds_ = 0.0;
    playheadSeconds_ = 0.0;
    cursorSeconds_ = 0.0;
    playheadUpperLimitSeconds_ = -1.0;
    displayStartSeconds_ = -kTimelineDisplayLeadInSeconds;
    displayEndSeconds_ = 1.0;
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
    updateDisplayBounds();
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
    updateDisplayBounds();
    updateHorizontalRange();
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

void TimelineView::setCursorSeconds(double second, bool centerView)
{
    const double clamped = qIsFinite(second) ? second : 0.0;
    const bool changed = !qFuzzyCompare(cursorSeconds_ + 1.0, clamped + 1.0);
    if (changed) {
        cursorSeconds_ = clamped;
        updateDisplayBounds();
        updateHorizontalRange();
    }
    if (centerView) {
        const int cursorX = secondToX(cursorSeconds_);
        const int targetX = cursorX - (viewport()->width() / 2);
        horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
    }
    if (changed || centerView) {
        viewport()->update();
    }
}

double TimelineView::playheadSeconds() const
{
    return playheadSeconds_;
}

double TimelineView::cursorSeconds() const
{
    return cursorSeconds_;
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

void TimelineView::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    QSet<QString> nextKeys;
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        if (!diagnostic.markerKey.isEmpty()) {
            nextKeys.insert(diagnostic.markerKey);
        }
    }
    if (muriMarkerKeys_ == nextKeys) {
        return;
    }
    muriMarkerKeys_ = nextKeys;
    viewport()->update();
}

int TimelineView::minimumContentHeightForCurrentDevice() const
{
    const int baseHeaderHeight = kHeaderHeight + kTimelineTopMargin;
    int controlBandHeight = 0;
    if (zoomButton_ != nullptr) {
        controlBandHeight = qMax(
            controlBandHeight,
            qMax(zoomButton_->minimumSizeHint().height(), zoomButton_->sizeHint().height())
        );
    }
    if (followPreviewCheckBox_ != nullptr) {
        controlBandHeight = qMax(
            controlBandHeight,
            qMax(followPreviewCheckBox_->minimumSizeHint().height(), followPreviewCheckBox_->sizeHint().height())
        );
    }
    if (!headerLineNumberFont_.family().isEmpty()) {
        controlBandHeight = qMax(controlBandHeight, QFontMetrics(headerLineNumberFont_).height());
    }
    const int headerHeight = qMax(baseHeaderHeight, controlBandHeight + 10);
    return headerHeight + timelineHeight() + 10;
}

void TimelineView::refreshMinimumHeightForCurrentDevice()
{
    const int targetHeight = minimumContentHeightForCurrentDevice();
    if (minimumHeight() == targetHeight) {
        return;
    }
    setMinimumHeight(targetHeight);
    updateGeometry();
}

void TimelineView::updateDisplayBounds()
{
    double minSecond = 0.0;
    double maxSecond = qMax(durationSeconds_, qMax(playheadSeconds_, qMax(0.0, cursorSeconds_)));
    if (playheadUpperLimitSeconds_ > 0.0) {
        maxSecond = qMax(maxSecond, playheadUpperLimitSeconds_);
    }
    if (waveformDurationSeconds_ > 0.0) {
        minSecond = qMin(minSecond, waveformStartSeconds_);
        maxSecond = qMax(maxSecond, waveformStartSeconds_ + waveformDurationSeconds_);
    }
    for (const TimelineBeatMarker& beat : beats_) {
        minSecond = qMin(minSecond, beat.second);
        maxSecond = qMax(maxSecond, beat.second);
    }
    for (const TimelineNoteMarker& note : notes_) {
        minSecond = qMin(minSecond, note.second);
        maxSecond = qMax(maxSecond, note.second);
        if (note.endSecond >= 0.0) {
            minSecond = qMin(minSecond, note.endSecond);
            maxSecond = qMax(maxSecond, note.endSecond);
        }
        if (note.slideTraceSecond >= 0.0) {
            minSecond = qMin(minSecond, note.slideTraceSecond);
            maxSecond = qMax(maxSecond, note.slideTraceSecond);
        }
    }
    displayStartSeconds_ = qMin(-kTimelineDisplayLeadInSeconds, minSecond - kTimelineDisplayLeadInSeconds);
    displayEndSeconds_ = qMax(displayStartSeconds_ + 1.0, maxSecond + 1.0);
}

double TimelineView::zoomScale() const
{
    return zoomPresets_.value(zoomPresetIndex_, 1.0);
}

void TimelineView::setFollowPreviewEnabled(bool enabled)
{
    if (followPreviewCheckBox_ == nullptr) {
        return;
    }
    followPreviewCheckBox_->setChecked(enabled);
}

bool TimelineView::followPreviewEnabled() const
{
    return followPreviewCheckBox_ != nullptr && followPreviewCheckBox_->isChecked();
}


#include "TimelineView.Paint.cpp"
#include "TimelineView.Interaction.cpp"
#include "TimelineView.Core.cpp"
