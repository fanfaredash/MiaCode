#include "timeline/quick/TimelineQuickStateBridge.h"

#include <algorithm>

#include <QFontDatabase>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "UiText.h"
#include "common/TimelineThemeConfig.h"
#include "timeline/TimelineSceneStateBuilder.h"
#include "timeline/TimelineView.h"

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

QString localizeMuriEntityText(QString text, bool chineseUi)
{
    text = text.trimmed();
    if (!chineseUi || text.isEmpty()) {
        return text;
    }
    static const QString kProtectedPrefix = QStringLiteral("protected ");
    if (text.startsWith(kProtectedPrefix, Qt::CaseInsensitive)) {
        text = QStringLiteral("保护 %1").arg(text.mid(kProtectedPrefix.size()).trimmed());
    }
    return text;
}

QString localizeMuriDetail(QString rawDetail, bool chineseUi)
{
    rawDetail = rawDetail.trimmed();
    if (!chineseUi || rawDetail.isEmpty()) {
        return rawDetail;
    }

    const auto unwrap = [](const QString& text, const QString& prefix, const QString& suffix, QString* middle) {
        if (middle == nullptr || !text.startsWith(prefix) || !text.endsWith(suffix)) {
            return false;
        }
        *middle = text.mid(prefix.size(), text.size() - prefix.size() - suffix.size()).trimmed();
        return true;
    };
    const auto trimTrailingPeriod = [](QString text) {
        text = text.trimmed();
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
        return text.trimmed();
    };
    const auto splitText = [](const QString& text, const QString& separator, QString* left, QString* right) {
        if (left == nullptr || right == nullptr) {
            return false;
        }
        const int splitIndex = text.indexOf(separator);
        if (splitIndex < 0) {
            return false;
        }
        *left = text.left(splitIndex).trimmed();
        *right = text.mid(splitIndex + separator.size()).trimmed();
        return !left->isEmpty() && !right->isEmpty();
    };
    const auto splitGapText =
        [&trimTrailingPeriod](const QString& text,
                              const QString& separator,
                              QString* left,
                              QString* right,
                              QString* gapText) {
            if (left == nullptr || right == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            const int gapIndex = text.lastIndexOf(QStringLiteral(", gap "));
            if (splitIndex < 0 || gapIndex < 0 || gapIndex <= splitIndex) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *right = text.mid(splitIndex + separator.size(), gapIndex - splitIndex - separator.size()).trimmed();
            *gapText = trimTrailingPeriod(text.mid(gapIndex + QStringLiteral(", gap ").size()));
            return !left->isEmpty() && !right->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };
    const auto splitSingleGapText =
        [&trimTrailingPeriod](const QString& text, const QString& separator, QString* left, QString* gapText) {
            if (left == nullptr || gapText == nullptr) {
                return false;
            }
            const int splitIndex = text.indexOf(separator);
            if (splitIndex < 0) {
                return false;
            }
            *left = text.left(splitIndex).trimmed();
            *gapText = trimTrailingPeriod(text.mid(splitIndex + separator.size()));
            return !left->isEmpty() && gapText->endsWith(QStringLiteral(" ms"));
        };

    QString middle;
    QString left;
    QString right;
    QString gapText;
    if (splitSingleGapText(rawDetail, QStringLiteral(" start will early-judge a following tap, gap "), &left, &gapText)) {
        return QStringLiteral("%1 启动，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitSingleGapText(rawDetail, QStringLiteral(" jump-start will early-judge a following tap, gap "), &left, &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定后续 tap，间隔 %2。").arg(left, gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start will early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start may early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，可能会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" start early-judges "), &left, &right, &gapText)) {
        return QStringLiteral("%1 启动，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start will early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start may early-judge "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，可能会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" jump-start early-judges "), &left, &right, &gapText)) {
        return QStringLiteral("%1 偷跑，会提前判定 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory may collide with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹可能会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory will collide with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" trajectory collides with "), &left, &right, &gapText)) {
        return QStringLiteral("%1 运行轨迹会撞到 %2，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (splitGapText(rawDetail, QStringLiteral(" was early-judged by "), &left, &right, &gapText)) {
        return QStringLiteral("%1 被 %2 提前判定，间隔 %3。")
            .arg(localizeMuriEntityText(left, chineseUi), localizeMuriEntityText(right, chineseUi), gapText);
    }
    if (rawDetail.endsWith(QStringLiteral(" resolved outside its critical window."))
        || rawDetail.contains(QStringLiteral(" resolved outside its critical window, gap "))) {
        const int splitIndex = rawDetail.indexOf(QStringLiteral(" resolved outside its critical window"));
        if (splitIndex > 0) {
            const QString target = rawDetail.left(splitIndex).trimmed();
            const int gapIndex = rawDetail.lastIndexOf(QStringLiteral(", gap "));
            if (gapIndex > splitIndex) {
                gapText = trimTrailingPeriod(rawDetail.mid(gapIndex + QStringLiteral(", gap ").size()));
                return QStringLiteral("%1 提前完成，间隔 %2。").arg(target, gapText);
            }
            return QStringLiteral("%1 的判定落在临界窗之外。").arg(target);
        }
    }
    if (unwrap(rawDetail, QStringLiteral("Multi-touch formed by "), QStringLiteral("."), &middle)) {
        return QStringLiteral("%1 构成多押。").arg(middle);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap."))
        && splitText(
            trimTrailingPeriod(rawDetail.left(rawDetail.size() - QStringLiteral(" formed overlap.").size())),
            QStringLiteral(" and same-position "),
            &left,
            &right)) {
        return QStringLiteral("%1 与相同位置的 %2 构成叠键。").arg(left, right);
    }
    if (rawDetail.endsWith(QStringLiteral(" formed overlap at the same position."))) {
        const QString target = rawDetail.left(
            rawDetail.size() - QStringLiteral(" formed overlap at the same position.").size()).trimmed();
        if (!target.isEmpty()) {
            return QStringLiteral("%1 在相同位置构成叠键。").arg(target);
        }
    }
    if (unwrap(rawDetail, QStringLiteral("Slide-head trigger from "), QStringLiteral(" early-judged this note."), &middle)) {
        return QStringLiteral("来自 %1 的滑键头触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(rawDetail, QStringLiteral("Pad trigger from "), QStringLiteral(" early-judged this note."), &middle)) {
        return QStringLiteral("来自 %1 的按下触发提前判定了此物件。").arg(middle);
    }
    if (unwrap(
            rawDetail,
            QStringLiteral("Runtime simple-note judge for "),
            QStringLiteral(" missed the critical window and resolved as overlap."),
            &middle)) {
        return QStringLiteral("运行时普通物件判定 %1 错过了判定临界窗，并最终判为叠键。").arg(middle);
    }
    if (unwrap(rawDetail, QStringLiteral("Slide runtime judge for "), QStringLiteral(" resolved outside its critical window."), &middle)) {
        return QStringLiteral("Slide 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(rawDetail, QStringLiteral("Slide "), QStringLiteral(" was cleared earlier than its normal judge timing."), &middle)) {
        return QStringLiteral("Slide %1 的完成时间早于正常判定时机。").arg(middle);
    }
    if (unwrap(rawDetail, QStringLiteral("Wifi runtime judge for "), QStringLiteral(" resolved outside its critical window."), &middle)) {
        return QStringLiteral("Wifi 运行时判定 %1 落在判定临界窗之外。").arg(middle);
    }
    if (unwrap(rawDetail, QStringLiteral("Wifi "), QStringLiteral(" was cleared earlier than its normal judge timing."), &middle)) {
        return QStringLiteral("Wifi %1 的完成时间早于正常判定时机。").arg(middle);
    }
    if (rawDetail.startsWith(QStringLiteral("Static reference from "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Static reference from ").size()).trimmed();
        const int deltaIndex = tail.lastIndexOf(QStringLiteral(", Δ "));
        if (deltaIndex >= 0 && tail.endsWith(QStringLiteral(" ms"))) {
            const QString source = tail.left(deltaIndex).trimmed();
            const QString deltaText = tail.mid(deltaIndex + QStringLiteral(", Δ ").size());
            return QStringLiteral("静态参考：%1，Δ %2").arg(source, deltaText);
        }
        return QStringLiteral("静态参考：%1").arg(tail);
    }
    if (rawDetail.startsWith(QStringLiteral("Runtime hand actions formed "))) {
        const QString tail = rawDetail.mid(QStringLiteral("Runtime hand actions formed ").size()).trimmed();
        const QString marker = QStringLiteral("-hand multi-touch: ");
        const int split = tail.indexOf(marker);
        if (split > 0) {
            const QString handCount = tail.left(split).trimmed();
            const QString actions = tail.mid(split + marker.size()).trimmed();
            return QStringLiteral("运行时手部动作形成了 %1 手多押：%2").arg(handCount, actions);
        }
    }
    return rawDetail;
}

QHash<quint64, QString> muriMarkerTooltipsForReport(const MuriAnalysisReport& report)
{
    QHash<quint64, QStringList> linesByLocation;
    const bool chineseUi = UiText::isChineseUi();
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        const quint64 locationId = timelineRenderLocationId(diagnostic.line, diagnostic.col);
        QString text = QStringLiteral("%1 · %2")
                           .arg(muriAlertLevelDisplayName(diagnostic.alertLevel, chineseUi))
                           .arg(muriKindDisplayName(diagnostic.kind, chineseUi));
        const QString detail = localizeMuriDetail(diagnostic.detail, chineseUi).trimmed();
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

TimelineQuickStateBridge::TimelineQuickStateBridge(QObject* parent)
    : QObject(parent)
    , zoomPresets_(makeTimelineZoomPresets())
{
    headerLineNumberFont_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    headerLineNumberFont_.setStyleHint(QFont::Monospace);
    headerLineNumberFont_.setFixedPitch(true);
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

void TimelineQuickStateBridge::attachReferenceView(TimelineView* referenceView)
{
    if (referenceView_ == referenceView) {
        return;
    }
    if (referenceView_ != nullptr && referenceView_->stateBridge() == this) {
        referenceView_->setStateBridge(nullptr);
    }
    referenceView_ = referenceView;
    if (referenceView_ != nullptr) {
        referenceView_->setStateBridge(this);
    }
    emit renderStateChanged();
}

void TimelineQuickStateBridge::clear()
{
    snapshot_ = TimelineRenderSnapshot();
    waveformData_.reset();
    muriMarkersByLocation_.clear();
    muriMarkerTooltips_.clear();
    horizontalScrollValue_ = 0;
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

void TimelineQuickStateBridge::setHeaderLineNumberFont(const QFont& font)
{
    headerLineNumberFont_ = font;
    ++gridRevision_;
    bumpHeaderRevision();
    emit renderStateChanged();
}

QFont TimelineQuickStateBridge::headerLineNumberFont() const
{
    return headerLineNumberFont_;
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
    bumpAllRevisions();
    emit renderStateChanged();
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
    request.zoomScale = zoomScale();
    request.contentScale = contentScale_;
    request.waveformBrightness = waveformBrightness_;
    request.playbackEntrySeconds = playbackEntrySeconds_;
    request.playheadSeconds = playheadSeconds_;
    request.cursorSeconds = cursorSeconds_;
    request.playheadUpperLimitSeconds = playheadUpperLimitSeconds_;
    layoutMetrics_ = miacode::timeline::TimelineSceneStateBuilder::layoutMetrics(request);
    layoutMetricsValid_ = true;
    horizontalScrollValue_ = qBound(0, horizontalScrollValue_, maxHorizontalScrollValue());
}

int TimelineQuickStateBridge::maxHorizontalScrollValue() const
{
    if (!layoutMetricsValid_) {
        return 0;
    }
    return miacode::timeline::TimelineSceneStateBuilder::maxHorizontalScrollValue(layoutMetrics_);
}

int TimelineQuickStateBridge::horizontalScrollValue() const
{
    return horizontalScrollValue_;
}

void TimelineQuickStateBridge::setHorizontalScrollValue(int value)
{
    if (!layoutMetricsValid_) {
        refreshLayoutMetrics();
    }
    const int clamped = qBound(0, value, maxHorizontalScrollValue());
    if (horizontalScrollValue_ == clamped) {
        return;
    }
    horizontalScrollValue_ = clamped;
    if (miacode::debug_options::timelineHotpathDiagnosticsEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("timeline/bridge"),
            QStringLiteral("action=set_horizontal_scroll_value value=%1 max=%2 viewport_width=%3")
                .arg(horizontalScrollValue_)
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
    const int targetX =
        miacode::timeline::TimelineSceneStateBuilder::secondToSceneX(layoutMetrics_, second)
        - (layoutMetrics_.viewportSize.width() / 2);
    const int clamped = qBound(0, targetX, maxHorizontalScrollValue());
    if (horizontalScrollValue_ == clamped) {
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
