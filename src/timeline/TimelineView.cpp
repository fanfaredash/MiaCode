#include "TimelineView.h"
#include "common/AssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewSkinConfig.h"
#include "common/TimelineThemeConfig.h"
#include "common/WaveformCache.h"
#include "timeline/TimelineNoteAssets.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"

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
constexpr int kTimelineMaxRenderedSubdivisionBeats = 32;
constexpr int kTimelineHeaderLineLabelMinSpacingPx = 22;
constexpr int kTimelineHeaderMultiDigitLabelSideGapPx = 2;
constexpr qreal kTimelineHeaderSingleDigitFontScale = 0.9;
constexpr qreal kTimelineHeaderMultiDigitBaseFontScale = 0.8;
constexpr qreal kTimelineTopMarkerTipOffsetPx = 1.0;
constexpr qreal kTimelinePlaybackEntryMarkerHalfWidthPx = 6.0;
constexpr qreal kTimelinePlaybackEntryMarkerHeightPx = 8.0;
constexpr qreal kTimelineTopMarkerHalfWidthPerHeight =
    kTimelinePlaybackEntryMarkerHalfWidthPx / kTimelinePlaybackEntryMarkerHeightPx;
constexpr qreal kTimelineHeaderAnchorMarkerLegacyWidthFactor = 0.85;
constexpr qreal kTimelineHeaderAnchorMarkerLegacyHeightFactor = 0.7;
constexpr int kTimelineHeaderAnchorMarkerTextGapPx = 0;
constexpr int kNoteSize = 14;
constexpr int kSlideTrackBasePixelSize =
    static_cast<int>((static_cast<double>(kNoteSize) * miacode::preview_skin::kSlideTrackLongSideRelativeToTap) + 0.5);
// Bar lines + secondary-strong subdivisions share this thickness; regular
// timeline lines are thinner so the bar-line hierarchy reads.
constexpr qreal kTimelineBeatLineWidth = 1.5;
constexpr qreal kTimelineSubdivisionLineWidth = 1.0;
constexpr qreal kTimelineHoldThicknessRelativeToTap =
    static_cast<qreal>(miacode::preview_skin::kHoldWidthRelativeToTap);
constexpr double kTimelineDisplayLeadInSeconds = 0.5;
constexpr double kTimelineHeaderLineAnchorToleranceSeconds = 1e-6;
constexpr double kTimelineKeyHoldAccelerationPerSecond = 1.0;
constexpr int kTimelineKeyHoldTickIntervalMs = 16;
const std::array<QColor, 5> kTimelineFireworkBandColors = {
    QColor(232, 124, 72),
    QColor(208, 106, 182),
    QColor(102, 180, 236),
    QColor(178, 202, 84),
    QColor(226, 206, 104),
};

double timelineHeldKeyPlaybackRate(double heldSeconds, double maxPlaybackRate)
{
    if (heldSeconds <= 0.0) {
        return 1.0;
    }
    const double accelerated = 1.0 + heldSeconds * kTimelineKeyHoldAccelerationPerSecond;
    return accelerated > maxPlaybackRate
        ? maxPlaybackRate
        : accelerated;
}

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
    return {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
}

QVector<double> makeTimelineButtonZoomPresets()
{
    return {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
}

bool usesTriggerSecondPlacement(const MuriDiagnostic& diagnostic)
{
    return diagnostic.kind == MuriKind::SlideTooFast
        || diagnostic.kind == MuriKind::MultiTouch;
}

QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> muriMarkersByLocationForReport(
    const MuriAnalysisReport& report)
{
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> markersByLocation;
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        const quint64 locationId = timelineRenderLocationId(diagnostic.line, diagnostic.col);
        QVector<miacode::timeline::TimelineMuriMarkerPlacement>& placements = markersByLocation[locationId];
        const bool useTriggerSecond = usesTriggerSecondPlacement(diagnostic);
        const bool duplicate = std::any_of(
            placements.cbegin(),
            placements.cend(),
            [useTriggerSecond, &diagnostic](const miacode::timeline::TimelineMuriMarkerPlacement& placement) {
                if (placement.useTriggerSecond != useTriggerSecond) {
                    return false;
                }
                if (!useTriggerSecond) {
                    return true;
                }
                return qAbs(placement.second - diagnostic.second) <= 1e-6;
            });
        if (duplicate) {
            continue;
        }

        miacode::timeline::TimelineMuriMarkerPlacement placement;
        placement.second = diagnostic.second;
        placement.useTriggerSecond = useTriggerSecond;
        placements.append(placement);
    }
    return markersByLocation;
}

int preferredRenderedSubdivisionBeats(int sourceSubdivisionBeats)
{
    const int normalizedSource = qMax(1, sourceSubdivisionBeats);
    if (normalizedSource <= kTimelineMaxRenderedSubdivisionBeats) {
        return normalizedSource;
    }
    for (int candidate = kTimelineMaxRenderedSubdivisionBeats; candidate >= 1; --candidate) {
        if ((normalizedSource % candidate) == 0) {
            return candidate;
        }
    }
    return 1;
}

bool shouldPaintTimelineBeatMarker(const TimelineRenderBeat& beat)
{
    const int sourceSubdivisionBeats = qMax(1, beat.subdivisionBeats);
    const int renderedSubdivisionBeats = preferredRenderedSubdivisionBeats(sourceSubdivisionBeats);
    if (renderedSubdivisionBeats >= sourceSubdivisionBeats) {
        return true;
    }
    const int stride = qMax(1, sourceSubdivisionBeats / renderedSubdivisionBeats);
    return (beat.subdivisionIndex % stride) == 0;
}

QFont scaledTimelineHeaderFont(const QFont& sourceFont, qreal scale)
{
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
}

qreal timelineHeaderLabelScale(const QFont& baseFont, int digitCount)
{
    if (digitCount <= 1) {
        return kTimelineHeaderSingleDigitFontScale;
    }
    const qreal multiDigitWidthBudget = qMax<qreal>(
        8.0,
        static_cast<qreal>(kTimelineHeaderLineLabelMinSpacingPx - kTimelineHeaderMultiDigitLabelSideGapPx)
    );
    const QString widthSample(qMax(1, digitCount), QLatin1Char('8'));
    const qreal widthScale = multiDigitWidthBudget
        / qMax<qreal>(1.0, static_cast<qreal>(QFontMetricsF(baseFont).horizontalAdvance(widthSample)));
    return qMin(kTimelineHeaderMultiDigitBaseFontScale, widthScale);
}

int timelineHeaderLabelHalfWidthPx(const QFont& baseFont, const QString& labelText)
{
    if (labelText.isEmpty()) {
        return 0;
    }
    const QFont labelFont = scaledTimelineHeaderFont(baseFont, timelineHeaderLabelScale(baseFont, labelText.size()));
    return qCeil(QFontMetricsF(labelFont).horizontalAdvance(labelText) * 0.5) + 1;
}

void appendTimelineUiPerfLog(const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("timeline/ui_perf"),
        payload,
        true
    );
}
}  // namespace

TimelineView::TimelineView(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setLineWidth(0);
    setMidLineWidth(0);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    viewport()->setFocusPolicy(Qt::NoFocus);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAutoFillBackground(false);

    zoomButton_ = new QToolButton(this);
    zoomButton_->setAutoRaise(false);
    zoomButton_->setCursor(Qt::PointingHandCursor);
    connect(zoomButton_, &QToolButton::clicked, this, [this]() { cycleZoomPreset(); });
    viewportLockCheckBox_ = new QCheckBox(this);
    viewportLockCheckBox_->setCursor(Qt::PointingHandCursor);
    viewportLockCheckBox_->setText(
        UiText::isChineseUi()
            ? QStringLiteral("\u5149\u6807\u5c45\u4e2d")
            : QStringLiteral("View Lock")
    );
    viewportLockCheckBox_->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("\u5c06\u7f16\u8f91\u5668\u5149\u6807\u5c3d\u91cf\u4fdd\u6301\u5728\u4ee3\u7801\u533a\u4e2d\u592e")
            : QStringLiteral("Keep the editor cursor near the middle of the code area when possible")
    );
    connect(viewportLockCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (stateBridge_ != nullptr && !applyingBridgeState_) {
            stateBridge_->setViewportLockEnabled(enabled);
        }
        emit viewportLockToggled(enabled);
    });
    followPreviewCheckBox_ = new QCheckBox(this);
    followPreviewCheckBox_->setCursor(Qt::PointingHandCursor);
    followPreviewCheckBox_->setText(
        UiText::isChineseUi()
            ? QStringLiteral("\u4ee3\u7801\u8ddf\u968f")
            : QStringLiteral("Follow Code")
    );
    followPreviewCheckBox_->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("\u4ec5\u5728\u64ad\u653e\u4e2d\u5c06\u7f16\u8f91\u5668\u5149\u6807\u7ed1\u5b9a\u5230\u9884\u89c8\u65f6\u95f4\u524d\u6700\u8fd1\u7684\u9017\u53f7")
            : QStringLiteral("During playback, bind the editor cursor to the latest comma at or before preview time")
    );
    connect(followPreviewCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (stateBridge_ != nullptr && !applyingBridgeState_) {
            stateBridge_->setFollowPreviewEnabled(enabled);
        }
        emit followPreviewToggled(enabled);
    });
    followProgressCheckBox_ = new QCheckBox(this);
    followProgressCheckBox_->setCursor(Qt::PointingHandCursor);
    followProgressCheckBox_->setText(
        UiText::isChineseUi()
            ? QStringLiteral("\u8fdb\u5ea6\u8ddf\u968f")
            : QStringLiteral("Progress Follow")
    );
    followProgressCheckBox_->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("\u64ad\u653e\u4e2d\u8ba9\u65f6\u95f4\u8f74\u89c6\u56fe\u8ddf\u968f\u9884\u89c8\u8fdb\u5ea6\u7ebf")
            : QStringLiteral("During playback, keep the timeline view centered on the preview progress line")
    );
    followProgressCheckBox_->setChecked(true);
    connect(followProgressCheckBox_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (stateBridge_ != nullptr && !applyingBridgeState_) {
            stateBridge_->setFollowProgressEnabled(enabled);
        }
        emit followProgressToggled(enabled);
    });

    headerLineNumberFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    headerLineNumberFont_.setStyleHint(QFont::Monospace);
    headerLineNumberFont_.setFixedPitch(true);
    zoomPresets_ = makeTimelineZoomPresets();
    buttonZoomPresets_ = makeTimelineButtonZoomPresets();
    zoomPresetIndex_ = qMax(0, zoomPresets_.indexOf(0.5));
    pixelsPerSecond_ = 120.0 * zoomScale();

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
    heldHorizontalKeyScrollTimer_ = new QTimer(this);
    heldHorizontalKeyScrollTimer_->setSingleShot(false);
    heldHorizontalKeyScrollTimer_->setTimerType(Qt::PreciseTimer);
    heldHorizontalKeyScrollTimer_->setInterval(kTimelineKeyHoldTickIntervalMs);
    connect(heldHorizontalKeyScrollTimer_, &QTimer::timeout, this, &TimelineView::applyHeldHorizontalKeyScrollTick);
    updateHorizontalRange();
}

void TimelineView::setStateBridge(TimelineQuickStateBridge* stateBridge)
{
    if (stateBridge_ == stateBridge) {
        return;
    }
    if (stateBridgeRenderStateConnection_) {
        QObject::disconnect(stateBridgeRenderStateConnection_);
        stateBridgeRenderStateConnection_ = {};
    }
    stateBridge_ = stateBridge;
    if (stateBridge_ != nullptr) {
        stateBridgeRenderStateConnection_ =
            connect(stateBridge_, &TimelineQuickStateBridge::renderStateChanged, this, &TimelineView::applyStateFromBridge);
        applyStateFromBridge();
    }
}

TimelineQuickStateBridge* TimelineView::stateBridge() const
{
    return stateBridge_;
}

void TimelineView::applyStateFromBridge()
{
    if (stateBridge_ == nullptr) {
        return;
    }

    applyingBridgeState_ = true;
    headerLineNumberFont_ = stateBridge_->headerLineNumberFont();
    snapshotCache_ = stateBridge_->renderSnapshot();
    lines_ = snapshotCache_.lines;
    measureLineSeconds_ = snapshotCache_.measureLineSeconds;
    noteVisualEndPrefixMaxWithSlideTracks_ = snapshotCache_.noteVisualEndPrefixMaxWithSlideTracks;
    noteVisualEndPrefixMaxWithoutSlideTracks_ = snapshotCache_.noteVisualEndPrefixMaxWithoutSlideTracks;
    trailingMeasureLineStartSecond_ = snapshotCache_.trailingMeasureLineStartSecond;
    trailingMeasureLineStepSeconds_ = snapshotCache_.trailingMeasureLineStepSeconds;
    durationSeconds_ = qMax(0.0, snapshotCache_.durationSeconds);
    minimumDataSecond_ = snapshotCache_.minimumSecond;
    maximumDataSecond_ = snapshotCache_.maximumSecond;
    waveformData_ = stateBridge_->waveformData();
    muriMarkerPlacementsByLocation_ = stateBridge_->muriMarkersByLocation();
    playbackEntrySeconds_ = stateBridge_->playbackEntrySeconds();
    playheadUpperLimitSeconds_ = stateBridge_->playheadUpperLimitSeconds();
    playheadSeconds_ = stateBridge_->playheadSeconds();
    cursorSeconds_ = stateBridge_->cursorSeconds();
    showSlideTracks_ = stateBridge_->showSlideTracks();
    playheadIndicatorSuppressed_ = stateBridge_->playheadIndicatorSuppressed();
    contentScale_ = stateBridge_->contentScale();
    waveformBrightness_ = stateBridge_->waveformBrightness();
    waveformPhaseCompensationSeconds_ = stateBridge_->waveformPhaseCompensationSeconds();
    pixelsPerSecond_ = 120.0 * stateBridge_->zoomScale();
    const int nextZoomIndex = qMax(0, zoomPresets_.indexOf(stateBridge_->zoomScale()));
    if (nextZoomIndex >= 0) {
        zoomPresetIndex_ = nextZoomIndex;
    }
    updateZoomButtonAppearance();
    if (followPreviewCheckBox_ != nullptr) {
        const QSignalBlocker blocker(followPreviewCheckBox_);
        followPreviewCheckBox_->setChecked(stateBridge_->followPreviewEnabled());
    }
    if (viewportLockCheckBox_ != nullptr) {
        const QSignalBlocker blocker(viewportLockCheckBox_);
        viewportLockCheckBox_->setChecked(stateBridge_->viewportLockEnabled());
    }
    if (followProgressCheckBox_ != nullptr) {
        const QSignalBlocker blocker(followProgressCheckBox_);
        followProgressCheckBox_->setChecked(stateBridge_->followProgressEnabled());
    }
    updateDisplayBounds();
    updateHorizontalRange();
    if (horizontalScrollBar() != nullptr) {
        horizontalScrollBar()->setValue(
            qBound(horizontalScrollBar()->minimum(), stateBridge_->horizontalScrollValue(), horizontalScrollBar()->maximum()));
    }
    layoutHeaderButtons();
    refreshMinimumHeightForCurrentDevice();
    viewport()->update();
    applyingBridgeState_ = false;
}

QSize TimelineView::minimumSizeHint() const
{
    return QSize(timelineLeft() + 240, minimumContentHeightForCurrentDevice());
}

QSize TimelineView::sizeHint() const
{
    return minimumSizeHint();
}

const QFont& TimelineView::headerLineNumberFont() const
{
    return headerLineNumberFont_;
}

void TimelineView::setHeaderLineNumberFont(const QFont& font)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setHeaderLineNumberFont(font);
        return;
    }
    if (headerLineNumberFont_ == font) {
        return;
    }
    headerLineNumberFont_ = font;
    refreshMinimumHeightForCurrentDevice();
    viewport()->update();
    emit renderStateChanged();
}

void TimelineView::refreshTheme()
{
    viewport()->setPalette(UiTheme::timelineViewportPalette());
    if (zoomButton_ != nullptr) {
        zoomButton_->setStyleSheet(UiTheme::timelineZoomButtonStyleSheet());
    }
    if (followPreviewCheckBox_ != nullptr) {
        followPreviewCheckBox_->setStyleSheet(
            UiTheme::timelineCheckBoxStyleSheet()
            + QStringLiteral(
                "QCheckBox { spacing: 4px; }"
                "QCheckBox::indicator { width: 14px; height: 14px; }"
            )
        );
    }
    if (viewportLockCheckBox_ != nullptr) {
        viewportLockCheckBox_->setStyleSheet(
            UiTheme::timelineCheckBoxStyleSheet()
            + QStringLiteral(
                "QCheckBox { spacing: 4px; }"
                "QCheckBox::indicator { width: 14px; height: 14px; }"
            )
        );
    }
    if (followProgressCheckBox_ != nullptr) {
        followProgressCheckBox_->setStyleSheet(
            UiTheme::timelineCheckBoxStyleSheet()
            + QStringLiteral(
                "QCheckBox { spacing: 4px; }"
                "QCheckBox::indicator { width: 14px; height: 14px; }"
            )
        );
    }
    refreshMinimumHeightForCurrentDevice();
    layoutHeaderButtons();
    viewport()->update();
    emit renderStateChanged();
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

void TimelineView::setTimelineData(const TimelineRenderSnapshot& snapshot)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setTimelineData(snapshot);
        return;
    }
    snapshotCache_ = snapshot;
    lines_ = snapshot.lines;
    measureLineSeconds_ = snapshot.measureLineSeconds;
    noteVisualEndPrefixMaxWithSlideTracks_ = snapshot.noteVisualEndPrefixMaxWithSlideTracks;
    noteVisualEndPrefixMaxWithoutSlideTracks_ = snapshot.noteVisualEndPrefixMaxWithoutSlideTracks;
    trailingMeasureLineStartSecond_ = snapshot.trailingMeasureLineStartSecond;
    trailingMeasureLineStepSeconds_ = snapshot.trailingMeasureLineStepSeconds;
    muriMarkerPlacementsByLocation_.clear();
    durationSeconds_ = qMax(0.0, snapshot.durationSeconds);
    minimumDataSecond_ = snapshot.minimumSecond;
    maximumDataSecond_ = snapshot.maximumSecond;
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
    emit renderStateChanged();
}

const TimelineRenderSnapshot& TimelineView::renderSnapshot() const
{
    return snapshotCache_;
}

void TimelineView::setWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setWaveformData(waveformData);
        return;
    }
    waveformData_ = waveformData;
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
    emit renderStateChanged();
}

std::shared_ptr<const miacode::waveform::WaveformData> TimelineView::waveformData() const
{
    return waveformData_;
}

void TimelineView::clear()
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->clear();
        return;
    }
    snapshotCache_ = TimelineRenderSnapshot();
    lines_.clear();
    measureLineSeconds_.clear();
    noteVisualEndPrefixMaxWithSlideTracks_.clear();
    noteVisualEndPrefixMaxWithoutSlideTracks_.clear();
    trailingMeasureLineStartSecond_ = 0.0;
    trailingMeasureLineStepSeconds_ = 0.0;
    muriMarkerPlacementsByLocation_.clear();
    durationSeconds_ = 0.0;
    playbackEntrySeconds_ = 0.0;
    playheadSeconds_ = 0.0;
    cursorSeconds_ = 0.0;
    focusTarget_ = FocusTarget::Playhead;
    playheadUpperLimitSeconds_ = -1.0;
    minimumDataSecond_ = 0.0;
    maximumDataSecond_ = 0.0;
    displayStartSeconds_ = -kTimelineDisplayLeadInSeconds;
    displayEndSeconds_ = 1.0;
    waveformData_.reset();
    updateHorizontalRange();
    viewport()->update();
    emit renderStateChanged();
}

void TimelineView::setPlaybackEntrySeconds(double second)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setPlaybackEntrySeconds(second);
        return;
    }
    const double clamped = qMax(0.0, second);
    if (qFuzzyCompare(playbackEntrySeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    playbackEntrySeconds_ = clamped;
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
    emit renderStateChanged();
}

double TimelineView::playbackEntrySeconds() const
{
    return playbackEntrySeconds_;
}

void TimelineView::setPlayheadUpperLimitSeconds(double second)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setPlayheadUpperLimitSeconds(second);
        return;
    }
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
    emit renderStateChanged();
}

double TimelineView::playheadUpperLimitSeconds() const
{
    return playheadUpperLimitSeconds_;
}

void TimelineView::setPlayheadSeconds(double second, bool centerView)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setPlayheadSeconds(second, centerView);
        return;
    }
    double clamped = qMax(0.0, second);
    if (playheadUpperLimitSeconds_ > 0.0) {
        clamped = qMin(clamped, playheadUpperLimitSeconds_);
    }
    if (qFuzzyCompare(playheadSeconds_, clamped)) {
        return;
    }

    const double previousSecond = playheadSeconds_;
    const int previousScrollValue = horizontalScrollBar()->value();
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
    if (horizontalScrollBar()->value() == previousScrollValue) {
        updateTimelineMarkerStrip(previousSecond, playheadSeconds_, 3);
    } else {
        viewport()->update();
    }
    emit playheadChanged(playheadSeconds_);
    emit renderStateChanged();
}

void TimelineView::setCursorSeconds(double second, bool centerView)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setCursorSeconds(second, centerView);
        return;
    }
    const double clamped = qIsFinite(second) ? second : 0.0;
    const bool changed = !qFuzzyCompare(cursorSeconds_ + 1.0, clamped + 1.0);
    const double previousSecond = cursorSeconds_;
    const int previousScrollValue = horizontalScrollBar()->value();
    if (changed) {
        cursorSeconds_ = clamped;
    }
    if (centerView) {
        const int cursorX = secondToX(cursorSeconds_);
        const int targetX = cursorX - (viewport()->width() / 2);
        horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
    }
    if (changed || centerView) {
        if (horizontalScrollBar()->value() == previousScrollValue) {
            updateTimelineMarkerStrip(previousSecond, cursorSeconds_, 3);
        } else {
            viewport()->update();
        }
        emit renderStateChanged();
    }
}

void TimelineView::focusPlayhead(bool centerView)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->focusPlayhead(centerView);
        return;
    }
    focusTarget_ = FocusTarget::Playhead;
    if (!centerView) {
        return;
    }
    const int previousValue = horizontalScrollBar()->value();
    const int playheadX = secondToX(playheadSeconds_);
    const int targetX = playheadX - (viewport()->width() / 2);
    horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
    if (horizontalScrollBar()->value() != previousValue) {
        viewport()->update();
        emit renderStateChanged();
    }
}

void TimelineView::focusCursor(bool centerView)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->focusCursor(centerView);
        return;
    }
    focusTarget_ = FocusTarget::Cursor;
    if (!centerView) {
        return;
    }
    const int previousValue = horizontalScrollBar()->value();
    const int cursorX = secondToX(cursorSeconds_);
    const int targetX = cursorX - (viewport()->width() / 2);
    horizontalScrollBar()->setValue(qBound(horizontalScrollBar()->minimum(), targetX, horizontalScrollBar()->maximum()));
    if (horizontalScrollBar()->value() != previousValue) {
        viewport()->update();
        emit renderStateChanged();
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
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setShowSlideTracks(show);
        return;
    }
    if (showSlideTracks_ == show) {
        return;
    }
    showSlideTracks_ = show;
    viewport()->update();
    emit renderStateChanged();
}

bool TimelineView::showSlideTracks() const
{
    return showSlideTracks_;
}

void TimelineView::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setMuriAnalysisReport(report);
        return;
    }
    const QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> nextMarkersByLocation =
        muriMarkersByLocationForReport(report);
    if (muriMarkerPlacementsByLocation_ == nextMarkersByLocation) {
        return;
    }
    muriMarkerPlacementsByLocation_ = nextMarkersByLocation;
    viewport()->update();
    emit renderStateChanged();
}

const QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>>&
TimelineView::muriMarkerPlacementsByLocation() const
{
    return muriMarkerPlacementsByLocation_;
}

int TimelineView::minimumContentHeightForCurrentDevice() const
{
    if (waveformOnlyPresentation()) {
        return timelineTop() + timelineHeight() + 8;
    }

    const int baseHeaderHeight = scaledTimelineHeaderMetric(kHeaderHeight + kTimelineTopMargin);
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
    if (viewportLockCheckBox_ != nullptr) {
        controlBandHeight = qMax(
            controlBandHeight,
            qMax(viewportLockCheckBox_->minimumSizeHint().height(), viewportLockCheckBox_->sizeHint().height())
        );
    }
    if (followProgressCheckBox_ != nullptr) {
        controlBandHeight = qMax(
            controlBandHeight,
            qMax(followProgressCheckBox_->minimumSizeHint().height(), followProgressCheckBox_->sizeHint().height())
        );
    }
    if (!headerLineNumberFont_.family().isEmpty()) {
        QFont scaledHeaderFont(headerLineNumberFont_);
        if (scaledHeaderFont.pointSizeF() > 0.0) {
            scaledHeaderFont.setPointSizeF(qMax(1.0, scaledHeaderFont.pointSizeF() * headerContentScale()));
        } else if (scaledHeaderFont.pointSize() > 0) {
            scaledHeaderFont.setPointSizeF(qMax(1.0, static_cast<qreal>(scaledHeaderFont.pointSize()) * headerContentScale()));
        } else if (scaledHeaderFont.pixelSize() > 0) {
            scaledHeaderFont.setPixelSize(qMax(1, qRound(static_cast<qreal>(scaledHeaderFont.pixelSize()) * headerContentScale())));
        }
        controlBandHeight = qMax(controlBandHeight, QFontMetrics(scaledHeaderFont).height());
    }
    const int headerHeight = qMax(baseHeaderHeight, controlBandHeight + 10);
    return headerHeight + timelineHeight();
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
    double minSecond = qMin(0.0, minimumDataSecond_);
    double maxSecond = qMax(
        maximumDataSecond_,
        qMax(durationSeconds_, qMax(playbackEntrySeconds_, qMax(playheadSeconds_, qMax(0.0, cursorSeconds_))))
    );
    if (playheadUpperLimitSeconds_ > 0.0) {
        maxSecond = qMax(maxSecond, playheadUpperLimitSeconds_);
    }
    if (waveformData_ && waveformData_->durationSeconds > 0.0) {
        maxSecond = qMax(maxSecond, waveformData_->durationSeconds);
    }
    displayStartSeconds_ = qMin(-kTimelineDisplayLeadInSeconds, minSecond - kTimelineDisplayLeadInSeconds);
    displayEndSeconds_ = qMax(displayStartSeconds_ + 1.0, maxSecond + 1.0);
}

double TimelineView::zoomScale() const
{
    return zoomPresets_.value(zoomPresetIndex_, 0.5);
}

double TimelineView::contentScale() const
{
    return contentScale_;
}

double TimelineView::waveformBrightness() const
{
    return waveformBrightness_;
}

void TimelineView::setWaveformBrightness(double brightness)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setWaveformBrightness(brightness);
        return;
    }
    const double clamped = miacode::timeline::normalizedTimelineWaveformBrightness(brightness);
    if (qFuzzyCompare(waveformBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    waveformBrightness_ = clamped;
    viewport()->update();
    emit renderStateChanged();
}

void TimelineView::setContentScale(double scale)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setContentScale(scale);
        return;
    }
    // Upper bound mirrors kBottomTabsContentScaleMax (MainWindow.WindowShell.cpp).
    const double clamped = qBound(0.5, scale, 4.0);
    if (qFuzzyCompare(contentScale_ + 1.0, clamped + 1.0)) {
        return;
    }
    contentScale_ = clamped;
    transformedIconCache_.clear();
    holdPixmapPartsCache_.clear();
    updateHorizontalRange();
    layoutHeaderButtons();
    refreshMinimumHeightForCurrentDevice();
    viewport()->update();
    emit renderStateChanged();
}

int TimelineView::horizontalScrollValue() const
{
    return horizontalScrollBar() != nullptr ? horizontalScrollBar()->value() : 0;
}

void TimelineView::setHorizontalScrollValue(int value)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setHorizontalScrollValue(value);
        return;
    }
    QScrollBar* hbar = horizontalScrollBar();
    if (hbar == nullptr) {
        return;
    }
    const int clamped = qBound(hbar->minimum(), value, hbar->maximum());
    if (hbar->value() == clamped) {
        return;
    }
    hbar->setValue(clamped);
    viewport()->update();
    emit renderStateChanged();
}

void TimelineView::stepZoomPresetForQuickSurface(int deltaSteps, double anchorSecond)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->stepZoomPreset(deltaSteps, anchorSecond);
        return;
    }
    const int previousIndex = zoomPresetIndex_;
    stepZoomPreset(deltaSteps, anchorSecond);
    if (zoomPresetIndex_ != previousIndex) {
        emit renderStateChanged();
    }
}

bool TimelineView::playheadIndicatorSuppressed() const
{
    return playheadIndicatorSuppressed_;
}

void TimelineView::suppressPlayheadIndicatorForQuickSurface()
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->suppressPlayheadIndicator();
        return;
    }
    suppressPlayheadIndicatorForInteraction();
    emit renderStateChanged();
}

void TimelineView::restorePlayheadIndicatorForQuickSurface(bool immediate)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->restorePlayheadIndicator(immediate);
        return;
    }
    restorePlayheadIndicatorAfterInteraction(immediate);
    emit renderStateChanged();
}

void TimelineView::setFollowPreviewEnabled(bool enabled)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setFollowPreviewEnabled(enabled);
        return;
    }
    if (followPreviewCheckBox_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(followPreviewCheckBox_);
    followPreviewCheckBox_->setChecked(enabled);
    emit renderStateChanged();
}

bool TimelineView::followPreviewEnabled() const
{
    return followPreviewCheckBox_ != nullptr && followPreviewCheckBox_->isChecked();
}

void TimelineView::setViewportLockEnabled(bool enabled)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setViewportLockEnabled(enabled);
        return;
    }
    if (viewportLockCheckBox_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(viewportLockCheckBox_);
    viewportLockCheckBox_->setChecked(enabled);
    emit renderStateChanged();
}

bool TimelineView::viewportLockEnabled() const
{
    return viewportLockCheckBox_ != nullptr && viewportLockCheckBox_->isChecked();
}

void TimelineView::setFollowProgressEnabled(bool enabled)
{
    if (stateBridge_ != nullptr && !applyingBridgeState_) {
        stateBridge_->setFollowProgressEnabled(enabled);
        return;
    }
    if (followProgressCheckBox_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(followProgressCheckBox_);
    followProgressCheckBox_->setChecked(enabled);
    emit renderStateChanged();
}

bool TimelineView::followProgressEnabled() const
{
    return followProgressCheckBox_ == nullptr || followProgressCheckBox_->isChecked();
}

void TimelineView::setPresentationMode(PresentationMode mode)
{
    if (presentationMode_ == mode) {
        return;
    }
    presentationMode_ = mode;
    refreshMinimumHeightForCurrentDevice();
    layoutHeaderButtons();
    updateDisplayBounds();
    updateHorizontalRange();
    viewport()->update();
    emit renderStateChanged();
}

TimelineView::PresentationMode TimelineView::presentationMode() const
{
    return presentationMode_;
}

int TimelineView::zoomPresetIndex() const
{
    return zoomPresetIndex_;
}

int TimelineView::zoomPresetCount() const
{
    return zoomPresets_.size();
}


#include "TimelineView.Paint.cpp"
#include "TimelineView.Interaction.cpp"
#include "TimelineView.Core.cpp"
