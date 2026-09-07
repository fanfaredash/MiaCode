#include "timeline/quick/TimelineQuickStateBridge.h"

#include <algorithm>

#include <QDir>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/InputShortcutGesture.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "common/TimelineThemeConfig.h"
#include "timeline/TimelineSceneStateBuilder.h"

#include <QVariant>

namespace {

QVector<double> makeTimelineZoomPresets()
{
    return {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
}

int zoomPresetIndexForScale(const QVector<double>& zoomPresets, double scale, int fallbackIndex = 1)
{
    if (zoomPresets.isEmpty()) {
        return 0;
    }
    if (!qIsFinite(scale) || scale <= 0.0) {
        return qBound(0, fallbackIndex, zoomPresets.size() - 1);
    }

    int closestIndex = qBound(0, fallbackIndex, zoomPresets.size() - 1);
    double closestDistance = qAbs(zoomPresets.at(closestIndex) - scale);
    for (int index = 0; index < zoomPresets.size(); ++index) {
        const double distance = qAbs(zoomPresets.at(index) - scale);
        if (distance + 1e-9 < closestDistance) {
            closestDistance = distance;
            closestIndex = index;
        }
    }
    return closestIndex;
}

SimaiNativeValidationLocale timelineUiValidationLocale()
{
    const QString token = UiText::resolvedLanguageToken();
    if (token.startsWith(QStringLiteral("zh"))) {
        return SimaiNativeValidationLocale::Chinese;
    }
    if (token.startsWith(QStringLiteral("ja"))) {
        return SimaiNativeValidationLocale::Japanese;
    }
    return SimaiNativeValidationLocale::English;
}

QString muriAlertLevelText(MuriAlertLevel level)
{
    switch (level) {
    case MuriAlertLevel::Muri:
        return UiText::text(QStringLiteral("validation.muri.alert.muri"));
    case MuriAlertLevel::Warning:
        return UiText::text(QStringLiteral("validation.muri.alert.warning"));
    }
    return UiText::text(QStringLiteral("validation.muri.alert.muri"));
}

QString muriKindText(MuriKind kind)
{
    switch (kind) {
    case MuriKind::SlideTooFast:
        return UiText::text(QStringLiteral("validation.muri.kind.slide_too_fast"));
    case MuriKind::SlideHeadTap:
        return UiText::text(QStringLiteral("validation.muri.kind.slide_head_tap"));
    case MuriKind::TapOnSlide:
        return UiText::text(QStringLiteral("validation.muri.kind.tap_on_slide"));
    case MuriKind::Overlap:
        return UiText::text(QStringLiteral("validation.muri.kind.overlap"));
    case MuriKind::MultiTouch:
        return UiText::text(QStringLiteral("validation.muri.kind.multi_touch"));
    }
    return UiText::text(QStringLiteral("validation.muri.alert.muri"));
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

QHash<quint64, QString> muriMarkerTooltipsForReport(const MuriAnalysisReport& report)
{
    QHash<quint64, QStringList> linesByLocation;
    const SimaiNativeValidationLocale locale = timelineUiValidationLocale();
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        const quint64 locationId = timelineRenderLocationId(diagnostic.line, diagnostic.col);
        QString text = QStringLiteral("%1 · %2")
                           .arg(muriAlertLevelText(diagnostic.alertLevel))
                           .arg(muriKindText(diagnostic.kind));
        const QString detail = renderMuriDiagnosticDetail(diagnostic, locale).trimmed();
        if (!detail.isEmpty()) {
            text += QStringLiteral("\n") + detail;
        }
        if (!text.isEmpty()) {
            linesByLocation[locationId].append(text);
        }
    }
    QHash<quint64, QString> tooltips;
    for (auto it = linesByLocation.cbegin(); it != linesByLocation.cend(); ++it) {
        tooltips.insert(it.key(), it.value().join(QStringLiteral("\n\n")));
    }
    return tooltips;
}

}  // namespace

TimelineQuickStateBridge::TimelineQuickStateBridge(QObject* parent, const QFont& font)
    : QObject(parent)
    , zoomPresets_(makeTimelineZoomPresets())
{
    headerLineNumberFont_ = font;
    headerLineNumberFont_.setPointSize(11);
}

void TimelineQuickStateBridge::bumpAllRevisions()
{
    ++gridRevision_;
    ++waveformRevision_;
    ++headerRevision_;
    ++notesRevision_;
    ++overlayRevision_;
    ++overlayDynamicRevision_;
}

void TimelineQuickStateBridge::bumpHeaderRevision()
{
    ++headerRevision_;
}

void TimelineQuickStateBridge::bumpNotesRevision()
{
    ++notesRevision_;
}

void TimelineQuickStateBridge::bumpOverlayRevision()
{
    ++overlayRevision_;
}

void TimelineQuickStateBridge::bumpOverlayDynamicRevision()
{
    ++overlayDynamicRevision_;
}

void TimelineQuickStateBridge::clear()
{
    snapshot_ = TimelineRenderSnapshot();
    waveformData_.reset();
    muriMarkersByLocation_.clear();
    muriMarkerTooltips_.clear();
    horizontalScrollValue_ = 0.0;
    playbackEntrySeconds_ = 0.0;
    playheadUpperLimitSeconds_ = -1.0;
    playheadSeconds_ = 0.0;
    cursorSeconds_ = 0.0;
    playheadIndicatorSuppressed_ = false;
    refreshLayoutMetrics();
    bumpAllRevisions();
    emit renderStateChanged();
}

void TimelineQuickStateBridge::setTimelineData(const TimelineRenderSnapshot& snapshot)
{
    snapshot_ = snapshot;
    refreshLayoutMetrics();
    bumpAllRevisions();
    emit renderStateChanged();
}

const TimelineRenderSnapshot& TimelineQuickStateBridge::renderSnapshot() const
{
    return snapshot_;
}

void TimelineQuickStateBridge::setWaveformData(
    const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData)
{
    waveformData_ = waveformData;
    refreshLayoutMetrics();
    bumpAllRevisions();
    emit renderStateChanged();
}

std::shared_ptr<const miacode::waveform::WaveformData> TimelineQuickStateBridge::waveformData() const
{
    return waveformData_;
}

void TimelineQuickStateBridge::setMuriAnalysisReport(const MuriAnalysisReport& report)
{
    QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>> nextMarkersByLocation =
        muriMarkersByLocationForReport(report);
    const QHash<quint64, QString> nextTooltips = muriMarkerTooltipsForReport(report);
    for (auto it = nextMarkersByLocation.begin(); it != nextMarkersByLocation.end(); ++it) {
        const QString tooltipText = nextTooltips.value(it.key());
        for (miacode::timeline::TimelineMuriMarkerPlacement& placement : it.value()) {
            placement.tooltipText = tooltipText;
        }
    }
    if (muriMarkersByLocation_ == nextMarkersByLocation && muriMarkerTooltips_ == nextTooltips) {
        return;
    }
    muriMarkersByLocation_ = nextMarkersByLocation;
    muriMarkerTooltips_ = nextTooltips;
    bumpOverlayRevision();
    emit renderStateChanged();
}

QHash<quint64, QVector<miacode::timeline::TimelineMuriMarkerPlacement>>
TimelineQuickStateBridge::muriMarkersByLocation() const
{
    return muriMarkersByLocation_;
}

QHash<quint64, QString> TimelineQuickStateBridge::muriMarkerTooltips() const
{
    return muriMarkerTooltips_;
}

QFont TimelineQuickStateBridge::headerLineNumberFont() const
{
    return headerLineNumberFont_;
}

QString TimelineQuickStateBridge::skinDirectory() const
{
    return skinDirectory_;
}

void TimelineQuickStateBridge::setSkinDirectory(const QString& skinDirectory)
{
    const QString trimmed = skinDirectory.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString() : QDir::cleanPath(trimmed);
    if (skinDirectory_ == normalized) {
        return;
    }
    skinDirectory_ = normalized;
    bumpNotesRevision();
    emit renderStateChanged();
}

void TimelineQuickStateBridge::setQuickViewportSize(const QSize& viewportSize)
{
    if (!viewportSize.isValid()) {
        return;
    }
    const QSize normalized(qMax(1, viewportSize.width()), qMax(1, viewportSize.height()));
    if (quickViewportSize_ == normalized) {
        return;
    }
    quickViewportSize_ = normalized;
    refreshLayoutMetrics();
    ++layoutRevision_;
    emit renderStateChanged();
}

int TimelineQuickStateBridge::timelineTop() const
{
    return layoutMetricsValid_ ? layoutMetrics_.timelineTop : 0;
}

QSize TimelineQuickStateBridge::effectiveViewportSize() const
{
    if (quickViewportSize_.isValid()) {
        return quickViewportSize_;
    }
    return QSize(1, 1);
}

void TimelineQuickStateBridge::refreshLayoutMetrics()
{
    miacode::timeline::TimelineSceneBuildRequest request;
    request.snapshot = snapshot_;
    request.waveformData = waveformData_;
    request.viewportSize = effectiveViewportSize();
    request.skinDirectory = skinDirectory_;
    request.zoomScale = zoomScale();
    request.contentScale = contentScale_;
    request.fitViewportHeight = true;
    request.waveformBrightness = waveformBrightness_;
    request.measureLineBrightness = measureLineBrightness_;
    request.waveformPhaseCompensationSeconds = waveformPhaseCompensationSeconds_;
    request.playbackEntrySeconds = playbackEntrySeconds_;
    request.playheadSeconds = playheadSeconds_;
    request.cursorSeconds = cursorSeconds_;
    request.playheadUpperLimitSeconds = playheadUpperLimitSeconds_;
    layoutMetrics_ = miacode::timeline::TimelineSceneStateBuilder::layoutMetrics(request);
    layoutMetricsValid_ = true;
    horizontalScrollValue_ =
        qBound(0.0, horizontalScrollValue_, static_cast<double>(maxHorizontalScrollValue()));
}

int TimelineQuickStateBridge::maxHorizontalScrollValue() const
{
    if (!layoutMetricsValid_) {
        return 0;
    }
    return miacode::timeline::TimelineSceneStateBuilder::maxHorizontalScrollValue(layoutMetrics_);
}

double TimelineQuickStateBridge::horizontalScrollValue() const
{
    return horizontalScrollValue_;
}

void TimelineQuickStateBridge::setHorizontalScrollValue(double value)
{
    if (!layoutMetricsValid_) {
        refreshLayoutMetrics();
    }
    const double clamped = qBound(0.0, value, static_cast<double>(maxHorizontalScrollValue()));
    if (qFuzzyCompare(horizontalScrollValue_ + 1.0, clamped + 1.0)) {
        return;
    }
    horizontalScrollValue_ = clamped;
    if (miacode::debug_options::timelineHotpathDiagnosticsEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/bridge"),
            QStringLiteral("action=set_horizontal_scroll_value value=%1 max=%2 viewport_width=%3")
                .arg(horizontalScrollValue_, 0, 'f', 3)
                .arg(maxHorizontalScrollValue())
                .arg(effectiveViewportSize().width())
        );
    }
    emit renderStateChanged();
}

double TimelineQuickStateBridge::zoomScale() const
{
    return zoomPresets_.value(zoomPresetIndex_, 0.5);
}

QVector<double> TimelineQuickStateBridge::zoomPresets() const
{
    return zoomPresets_;
}

QVariantList TimelineQuickStateBridge::zoomPresetValues() const
{
    QVariantList values;
    values.reserve(zoomPresets_.size());
    for (double scale : zoomPresets_) {
        values.append(scale);
    }
    return values;
}

void TimelineQuickStateBridge::applyZoomPreset(double scale)
{
    setZoomScaleAnchored(scale, viewportCenterSecond());
}

QStringList TimelineQuickStateBridge::zoomInWheelShortcuts() const
{
    return zoomInWheelShortcuts_;
}

QStringList TimelineQuickStateBridge::zoomOutWheelShortcuts() const
{
    return zoomOutWheelShortcuts_;
}

void TimelineQuickStateBridge::setZoomWheelShortcuts(
    const QStringList& zoomInShortcuts,
    const QStringList& zoomOutShortcuts)
{
    const QStringList normalizedZoomIn = miacode::input_shortcut::normalizeGestureTexts(zoomInShortcuts);
    const QStringList normalizedZoomOut = miacode::input_shortcut::normalizeGestureTexts(zoomOutShortcuts);
    zoomInWheelShortcuts_ = normalizedZoomIn.isEmpty()
        ? QStringList{QStringLiteral("Ctrl+WheelUp")}
        : normalizedZoomIn;
    zoomOutWheelShortcuts_ = normalizedZoomOut.isEmpty()
        ? QStringList{QStringLiteral("Ctrl+WheelDown")}
        : normalizedZoomOut;
}

double TimelineQuickStateBridge::viewportCenterSecond()
{
    if (!layoutMetricsValid_) {
        refreshLayoutMetrics();
    }
    const double pixelsPerSecond = qMax(1e-9, layoutMetrics_.pixelsPerSecond);
    const double sceneX = horizontalScrollValue_
        + (static_cast<double>(layoutMetrics_.viewportSize.width()) * 0.5);
    return qMax(
        0.0,
        layoutMetrics_.displayStartSeconds
            + ((sceneX
                - static_cast<double>(layoutMetrics_.leadingCenteringPadding)
                - static_cast<double>(layoutMetrics_.timelineLeft))
               / pixelsPerSecond));
}

QSize TimelineQuickStateBridge::viewportSize() const
{
    return effectiveViewportSize();
}

double TimelineQuickStateBridge::contentScale() const
{
    return contentScale_;
}

void TimelineQuickStateBridge::setContentScale(double scale)
{
    // Upper bound mirrors kBottomTabsContentScaleMax (MainWindow.WindowShell.cpp); above
    // 100% only the note grid grows, the header/素材 cap in the scene builder.
    const double clamped = qBound(0.5, scale, 4.0);
    if (qFuzzyCompare(contentScale_ + 1.0, clamped + 1.0)) {
        return;
    }
    contentScale_ = clamped;
    refreshLayoutMetrics();
    bumpAllRevisions();
    emit renderStateChanged();
}

double TimelineQuickStateBridge::waveformBrightness() const
{
    return waveformBrightness_;
}

void TimelineQuickStateBridge::setWaveformBrightness(double brightness)
{
    const double clamped = miacode::timeline::normalizedTimelineWaveformBrightness(brightness);
    if (qFuzzyCompare(waveformBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    waveformBrightness_ = clamped;
    ++waveformRevision_;
    emit waveformBrightnessChanged(waveformBrightness_);
    emit renderStateChanged();
}

double TimelineQuickStateBridge::measureLineBrightness() const
{
    return measureLineBrightness_;
}

void TimelineQuickStateBridge::setMeasureLineBrightness(double brightness)
{
    const double clamped = miacode::timeline::normalizedTimelineMeasureLineBrightness(brightness);
    if (qFuzzyCompare(measureLineBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    measureLineBrightness_ = clamped;
    ++gridRevision_;
    emit measureLineBrightnessChanged(measureLineBrightness_);
    emit renderStateChanged();
}

double TimelineQuickStateBridge::waveformPhaseCompensationSeconds() const
{
    return waveformPhaseCompensationSeconds_;
}

void TimelineQuickStateBridge::setWaveformPhaseCompensationSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? qMax(0.0, seconds) : 0.0;
    if (qFuzzyCompare(waveformPhaseCompensationSeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    waveformPhaseCompensationSeconds_ = clamped;
    ++waveformRevision_;
    emit renderStateChanged();
}

void TimelineQuickStateBridge::setZoomScale(double scale)
{
    if (zoomPresets_.isEmpty()) {
        return;
    }
    const int nextIndex = zoomPresetIndexForScale(zoomPresets_, scale, zoomPresetIndex_);
    if (nextIndex == zoomPresetIndex_) {
        return;
    }
    zoomPresetIndex_ = nextIndex;
    refreshLayoutMetrics();
    bumpAllRevisions();
    emit zoomScaleChanged(zoomScale());
    emit renderStateChanged();
}

void TimelineQuickStateBridge::setZoomScaleAnchored(double scale, double anchorSecond)
{
    if (zoomPresets_.isEmpty()) {
        return;
    }
    const int nextIndex = zoomPresetIndexForScale(zoomPresets_, scale, zoomPresetIndex_);
    if (nextIndex == zoomPresetIndex_) {
        return;
    }
    zoomPresetIndex_ = nextIndex;
    refreshLayoutMetrics();
    centerOnSecond(anchorSecond);
    bumpAllRevisions();
    emit zoomScaleChanged(zoomScale());
    emit renderStateChanged();
}

void TimelineQuickStateBridge::cycleZoomPreset(double anchorSecond)
{
    if (zoomPresets_.isEmpty()) {
        return;
    }
    const double currentScale = zoomScale();
    int nextIndex = 0;
    bool foundHigherPreset = false;
    for (int index = 0; index < zoomPresets_.size(); ++index) {
        if (zoomPresets_.at(index) > currentScale + 1e-6) {
            nextIndex = index;
            foundHigherPreset = true;
            break;
        }
    }
    if (!foundHigherPreset) {
        nextIndex = 0;
    }
    if (nextIndex == zoomPresetIndex_) {
        return;
    }
    zoomPresetIndex_ = nextIndex;
    refreshLayoutMetrics();
    centerOnSecond(anchorSecond);
    bumpAllRevisions();
    emit zoomScaleChanged(zoomScale());
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::centerOnSecond(double second)
{
    if (!layoutMetricsValid_) {
        refreshLayoutMetrics();
    }
    // Exact, not secondToSceneX: this is the follow-playback scroll target, and rounding it
    // to whole pixels is what quantised the scroll velocity into the 3/4/5px stepping that
    // reads as judder. The viewport half-width is halved in floating point for the same
    // reason — integer division here reintroduced a half-pixel bias on odd widths.
    const qreal targetX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneXExact(layoutMetrics_, second)
        - (layoutMetrics_.viewportSize.width() / 2.0);
    const double clamped = qBound(0.0, static_cast<double>(targetX),
                                  static_cast<double>(maxHorizontalScrollValue()));
    if (qFuzzyCompare(horizontalScrollValue_ + 1.0, clamped + 1.0)) {
        return false;
    }
    horizontalScrollValue_ = clamped;
    return true;
}

void TimelineQuickStateBridge::stepZoomPreset(int deltaSteps, double anchorSecond)
{
    if (zoomPresets_.isEmpty() || deltaSteps == 0) {
        return;
    }
    const int nextIndex = qBound(0, zoomPresetIndex_ + deltaSteps, zoomPresets_.size() - 1);
    if (nextIndex == zoomPresetIndex_) {
        return;
    }
    zoomPresetIndex_ = nextIndex;
    refreshLayoutMetrics();
    centerOnSecond(anchorSecond);
    bumpAllRevisions();
    emit zoomScaleChanged(zoomScale());
    emit renderStateChanged();
}

void TimelineQuickStateBridge::setPlaybackEntrySeconds(double second)
{
    const double clamped = qMax(0.0, second);
    if (qFuzzyCompare(playbackEntrySeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    playbackEntrySeconds_ = clamped;
    bumpHeaderRevision();
    emit renderStateChanged();
}

double TimelineQuickStateBridge::playbackEntrySeconds() const
{
    return playbackEntrySeconds_;
}

void TimelineQuickStateBridge::setPlayheadUpperLimitSeconds(double second)
{
    const double clampedUpperLimit = second > 0.0 ? second : -1.0;
    const bool limitChanged = !qFuzzyCompare(playheadUpperLimitSeconds_ + 2.0, clampedUpperLimit + 2.0);
    playheadUpperLimitSeconds_ = clampedUpperLimit;
    bool playheadValueChanged = false;
    if (playheadUpperLimitSeconds_ > 0.0 && playheadSeconds_ > playheadUpperLimitSeconds_) {
        playheadValueChanged = !qFuzzyCompare(playheadSeconds_ + 1.0, playheadUpperLimitSeconds_ + 1.0);
        playheadSeconds_ = playheadUpperLimitSeconds_;
    }
    if (!limitChanged && !playheadValueChanged) {
        return;
    }
    bumpOverlayDynamicRevision();
    if (playheadValueChanged) {
        emit playheadChanged(playheadSeconds_);
    }
    emit renderStateChanged();
}

double TimelineQuickStateBridge::playheadUpperLimitSeconds() const
{
    return playheadUpperLimitSeconds_;
}

double TimelineQuickStateBridge::durationSeconds() const
{
    return qMax(0.0, snapshot_.durationSeconds);
}

double TimelineQuickStateBridge::playheadSeconds() const
{
    return playheadSeconds_;
}

void TimelineQuickStateBridge::setPlayheadSeconds(double second, bool centerView)
{
    double clamped = qMax(0.0, second);
    if (playheadUpperLimitSeconds_ > 0.0) {
        clamped = qMin(clamped, playheadUpperLimitSeconds_);
    }
    const bool changed = !qFuzzyCompare(playheadSeconds_ + 1.0, clamped + 1.0);
    playheadSeconds_ = clamped;
    const bool scrollChanged = centerView ? centerOnSecond(playheadSeconds_) : false;
    if (!changed && !scrollChanged) {
        return;
    }
    bumpOverlayDynamicRevision();
    if (changed) {
        emit playheadChanged(playheadSeconds_);
    }
    emit renderStateChanged();
}

double TimelineQuickStateBridge::cursorSeconds() const
{
    return cursorSeconds_;
}

void TimelineQuickStateBridge::setCursorSeconds(double second, bool centerView)
{
    const double clamped = qIsFinite(second) ? second : 0.0;
    const bool changed = !qFuzzyCompare(cursorSeconds_ + 1.0, clamped + 1.0);
    if (!changed && !centerView) {
        return;
    }
    cursorSeconds_ = clamped;
    const bool scrollChanged = centerView ? centerOnSecond(cursorSeconds_) : false;
    if (!changed && !scrollChanged) {
        return;
    }
    bumpOverlayDynamicRevision();
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/bridge"),
            QStringLiteral("action=set_cursor_seconds second=%1 center=%2 quick_viewport=%3x%4")
                .arg(cursorSeconds_, 0, 'f', 6)
                .arg(centerView ? 1 : 0)
                .arg(quickViewportSize_.width())
                .arg(quickViewportSize_.height())
        );
    }
    emit renderStateChanged();
}

void TimelineQuickStateBridge::notifyRenderCadenceTick()
{
    emit renderCadenceTick();
}

void TimelineQuickStateBridge::setPlaybackCadenceActive(bool active)
{
    playbackCadenceActive_ = active;
}

bool TimelineQuickStateBridge::playbackCadenceActive() const
{
    return playbackCadenceActive_;
}

void TimelineQuickStateBridge::focusPlayhead(bool centerView)
{
    if (!centerView) {
        return;
    }
    if (!centerOnSecond(playheadSeconds_)) {
        return;
    }
    bumpOverlayDynamicRevision();
    emit renderStateChanged();
}

void TimelineQuickStateBridge::focusCursor(bool centerView)
{
    if (!centerView) {
        return;
    }
    if (!centerOnSecond(cursorSeconds_)) {
        return;
    }
    bumpOverlayDynamicRevision();
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::showSlideTracks() const
{
    return showSlideTracks_;
}

void TimelineQuickStateBridge::setShowSlideTracks(bool show)
{
    if (showSlideTracks_ == show) {
        return;
    }
    showSlideTracks_ = show;
    bumpNotesRevision();
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::followPreviewEnabled() const
{
    return followPreviewEnabled_;
}

void TimelineQuickStateBridge::setFollowPreviewEnabled(bool enabled)
{
    if (followPreviewEnabled_ == enabled) {
        return;
    }
    followPreviewEnabled_ = enabled;
    emit followPreviewEnabledChanged(enabled);
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::viewportLockEnabled() const
{
    return viewportLockEnabled_;
}

void TimelineQuickStateBridge::setViewportLockEnabled(bool enabled)
{
    if (viewportLockEnabled_ == enabled) {
        return;
    }
    viewportLockEnabled_ = enabled;
    emit viewportLockEnabledChanged(enabled);
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::followProgressEnabled() const
{
    return followProgressEnabled_;
}

void TimelineQuickStateBridge::setFollowProgressEnabled(bool enabled)
{
    if (followProgressEnabled_ == enabled) {
        return;
    }
    followProgressEnabled_ = enabled;
    emit followProgressEnabledChanged(enabled);
    emit renderStateChanged();
}

bool TimelineQuickStateBridge::timelineSyncEnabled() const
{
    return timelineSyncEnabled_;
}

void TimelineQuickStateBridge::setTimelineSyncEnabled(bool enabled)
{
    if (timelineSyncEnabled_ == enabled) {
        return;
    }
    timelineSyncEnabled_ = enabled;
    emit timelineSyncEnabledChanged(enabled);
}

bool TimelineQuickStateBridge::playheadIndicatorSuppressed() const
{
    return playheadIndicatorSuppressed_;
}

void TimelineQuickStateBridge::suppressPlayheadIndicator()
{
    if (playheadIndicatorSuppressed_) {
        return;
    }
    playheadIndicatorSuppressed_ = true;
    bumpOverlayDynamicRevision();
    emit renderStateChanged();
}

void TimelineQuickStateBridge::restorePlayheadIndicator(bool immediate)
{
    if (!playheadIndicatorSuppressed_ && !immediate) {
        return;
    }
    playheadIndicatorSuppressed_ = false;
    bumpOverlayDynamicRevision();
    emit renderStateChanged();
}
