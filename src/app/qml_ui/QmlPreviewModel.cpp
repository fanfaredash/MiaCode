#include "QmlPreviewModel.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MuriRenderOptions.h"

#include <array>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QVariantMap>

namespace {

struct StatisticDescriptor {
    const char* kind;
    const char* fallbackName;
};

constexpr std::array<StatisticDescriptor, 6> kStatisticDescriptors{{
    {"tap", "Tap"},
    {"hold", "Hold"},
    {"slide", "Slide"},
    {"touch", "Touch"},
    {"break", "Break"},
    {"total", "Total"},
}};

} // namespace

QmlPreviewModel::QmlPreviewModel(miacode::v2::ShellNotifications& notifications,
                                 miacode::v2::PreviewSurface*& surfaceSlot,
                                 miacode::v2::PlaybackControl*& controlSlot,
                                 QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , surfaceSlot_(&surfaceSlot)
    , controlSlot_(&controlSlot)
{
    v2UiProbeEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
    connect(notifications_, &miacode::v2::ShellNotifications::presentationChanged, this, [this]() {
        updateV2UiProbePlaybackState();
        refreshFromBackend();
    });
    connect(notifications_, &miacode::v2::ShellNotifications::previewPlayheadChanged, this, [this]() {
        updateV2UiProbePlaybackState();
        refreshFromBackend();
    });
    connect(notifications_, &miacode::v2::ShellNotifications::previewSkinDirectoryChanged, this, [this]() {
        refreshSkinDirectory();
        rebuildStatistics();
        emit statisticsChanged();
    });
    refreshSkinDirectory();
    refreshFromBackend(/*force=*/true);
}

void QmlPreviewModel::resetV2UiProbe()
{
    v2UiProbeStatisticsRebuildCount_ = 0;
    v2UiProbeStatisticsBuildNs_ = 0;
    v2UiProbeStatisticsBuildMaxNs_ = 0;
    v2UiProbeSkinResolveNs_ = 0;
    v2UiProbeSkinResolveMaxNs_ = 0;
    v2UiProbeShellStateChangeCount_ = 0;
}

void QmlPreviewModel::appendV2UiProbeSummary() const
{
    if (!v2UiProbeEnabled_) {
        return;
    }
    const double divisor = static_cast<double>(qMax<qint64>(1, v2UiProbeStatisticsRebuildCount_));
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("qml_ui/v2_preview_probe"),
        QStringLiteral(
            "action=playback_summary shell_state_changes=%1 statistics_rebuilds=%2 "
            "statistics_build_avg_ms=%3 statistics_build_max_ms=%4 skin_resolve_avg_ms=%5 "
            "skin_resolve_max_ms=%6"
        )
            .arg(v2UiProbeShellStateChangeCount_)
            .arg(v2UiProbeStatisticsRebuildCount_)
            .arg(v2UiProbeStatisticsBuildNs_ / divisor / 1000000.0, 0, 'f', 3)
            .arg(v2UiProbeStatisticsBuildMaxNs_ / 1000000.0, 0, 'f', 3)
            .arg(v2UiProbeSkinResolveNs_ / divisor / 1000000.0, 0, 'f', 3)
            .arg(v2UiProbeSkinResolveMaxNs_ / 1000000.0, 0, 'f', 3)
    );
}

void QmlPreviewModel::refreshSkinDirectory()
{
    const bool probeThisResolve = v2UiProbeEnabled_ && v2UiProbePlaybackActive_;
    QElapsedTimer skinTimer;
    if (probeThisResolve) {
        skinTimer.start();
    }
    skinDirectory_ = surface() != nullptr ? surface()->resolveSkinDir() : QString();
    if (probeThisResolve) {
        const qint64 skinElapsedNs = skinTimer.nsecsElapsed();
        v2UiProbeSkinResolveNs_ += skinElapsedNs;
        v2UiProbeSkinResolveMaxNs_ = qMax(v2UiProbeSkinResolveMaxNs_, skinElapsedNs);
    }
}

void QmlPreviewModel::rebuildStatistics()
{
    const bool probeThisBuild = v2UiProbeEnabled_ && v2UiProbePlaybackActive_;
    QElapsedTimer totalTimer;
    if (probeThisBuild) {
        totalTimer.start();
    }
    QVariantList nextStatistics;
    const QString skinRevision = QString::number(qHash(skinDirectory_));
    for (qsizetype index = 0; index < static_cast<qsizetype>(kStatisticDescriptors.size()); ++index) {
        const StatisticDescriptor& descriptor = kStatisticDescriptors.at(static_cast<std::size_t>(index));
        const QString text = statisticsTexts_.value(index);
        const int separator = text.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString name = separator > 0
            ? text.left(separator)
            : QString::fromLatin1(descriptor.fallbackName);
        const QString value = separator > 0 ? text.mid(separator + 1).trimmed() : QStringLiteral("0/0");
        const int valueSeparator = value.indexOf(QLatin1Char('/'));
        const int played = valueSeparator > 0 ? value.left(valueSeparator).toInt() : 0;
        const int total = valueSeparator > 0 ? value.mid(valueSeparator + 1).toInt() : 0;
        const QString kind = QString::fromLatin1(descriptor.kind);
        nextStatistics.append(QVariantMap{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},
            {QStringLiteral("played"), played},
            {QStringLiteral("total"), total},
            {QStringLiteral("value"), value},
            {QStringLiteral("iconSource"), kind == QStringLiteral("total")
                ? QString()
                : QStringLiteral("image://noteicon/%1?skin=%2").arg(kind, skinRevision)},
        });
    }
    statistics_ = std::move(nextStatistics);
    if (probeThisBuild) {
        const qint64 totalElapsedNs = totalTimer.nsecsElapsed();
        ++v2UiProbeStatisticsRebuildCount_;
        v2UiProbeStatisticsBuildNs_ += totalElapsedNs;
        v2UiProbeStatisticsBuildMaxNs_ = qMax(v2UiProbeStatisticsBuildMaxNs_, totalElapsedNs);
    }
}

void QmlPreviewModel::refreshFromBackend(bool force)
{
    if (surface() == nullptr) {
        return;
    }
    const double nextPosition = surface()->positionSeconds();
    const double nextDuration = surface()->durationSeconds();
    const double nextLowerBound = surface()->lowerBoundSeconds();
    QString nextRateLabel = surface()->playbackRateLabel().trimmed();
    nextRateLabel.remove(QLatin1Char('x'), Qt::CaseInsensitive);
    bool rateOk = false;
    const double nextRate = nextRateLabel.toDouble(&rateOk);
    const bool nextPlaying = surface()->playing();
    const RenderMode nextRenderModeValue = surface()->muriRenderMode();
    const QString nextRenderMode = muriRenderModeToken(nextRenderModeValue);
    if (nextRenderModeValue == RenderMode::Native
        || nextRenderModeValue == RenderMode::EraseByArea) {
        lastRegularMode_ = nextRenderModeValue;
    }
    const bool nextMuriCheckEnabled = nextRenderModeValue == RenderMode::MaimuriDxStyle;
    const bool nextSmoothStarErase = lastRegularMode_ != RenderMode::EraseByArea;
    const QString nextRenderModeLabel = nextMuriCheckEnabled
        ? tr("无理检测")
        : tr("常规渲染");
    const QStringList nextStatisticsTexts = surface()->statsTexts();

    const bool positionChangedValue = force || nextPosition != positionSeconds_;
    const bool transportChangedValue = force
        || nextDuration != durationSeconds_
        || nextLowerBound != lowerBoundSeconds_
        || (rateOk ? nextRate : 1.0) != rate_;
    const bool playingChangedValue = force || nextPlaying != playing_;
    const bool renderModeChangedValue = force
        || nextRenderMode != renderMode_
        || nextRenderModeLabel != renderModeLabel_
        || nextMuriCheckEnabled != muriCheckEnabled_
        || nextSmoothStarErase != smoothStarErase_;
    const bool statisticsChangedValue = force || nextStatisticsTexts != statisticsTexts_;

    positionSeconds_ = nextPosition;
    durationSeconds_ = nextDuration;
    lowerBoundSeconds_ = nextLowerBound;
    rate_ = rateOk ? nextRate : 1.0;
    playing_ = nextPlaying;
    renderMode_ = nextRenderMode;
    renderModeLabel_ = nextRenderModeLabel;
    muriCheckEnabled_ = nextMuriCheckEnabled;
    smoothStarErase_ = nextSmoothStarErase;
    if (statisticsChangedValue) {
        statisticsTexts_ = nextStatisticsTexts;
        rebuildStatistics();
    }

    if (positionChangedValue) {
        emit positionChanged();
    }
    if (transportChangedValue) {
        emit transportChanged();
    }
    if (playingChangedValue) {
        emit playingChanged();
    }
    if (renderModeChangedValue) {
        emit renderModeChanged();
    }
    if (statisticsChangedValue) {
        emit statisticsChanged();
    }
    emit presentationChanged();
}

void QmlPreviewModel::updateV2UiProbePlaybackState()
{
    if (!v2UiProbeEnabled_ || surface() == nullptr) {
        return;
    }
    const bool playingNow = surface()->playing();
    if (playingNow && !v2UiProbePlaybackActive_) {
        resetV2UiProbe();
    } else if (!playingNow && v2UiProbePlaybackActive_) {
        appendV2UiProbeSummary();
    }
    v2UiProbePlaybackActive_ = playingNow;
    if (playingNow) {
        ++v2UiProbeShellStateChangeCount_;
    }
}

double QmlPreviewModel::positionSeconds() const { return positionSeconds_; }
double QmlPreviewModel::durationSeconds() const { return durationSeconds_; }
double QmlPreviewModel::lowerBoundSeconds() const { return lowerBoundSeconds_; }
double QmlPreviewModel::rate() const { return rate_; }
bool QmlPreviewModel::playing() const { return playing_; }
QString QmlPreviewModel::renderMode() const { return renderMode_; }
QString QmlPreviewModel::renderModeLabel() const { return renderModeLabel_; }
bool QmlPreviewModel::muriCheckEnabled() const { return muriCheckEnabled_; }
bool QmlPreviewModel::smoothStarErase() const { return smoothStarErase_; }
QVariantList QmlPreviewModel::statistics() const { return statistics_; }

QString QmlPreviewModel::currentSkinDirectory() const
{
    return skinDirectory_;
}

QObject* QmlPreviewModel::runtime() const { return surface()->previewRuntimeObject(); }
QObject* QmlPreviewModel::mediaHost() const { return surface()->stageMediaHostObject(); }

double QmlPreviewModel::canvasAspectRatio() const
{
    return surface() != nullptr ? surface()->canvasAspectRatio() : 1.0;
}

void QmlPreviewModel::setPositionSeconds(double value) { playbackControl()->seek(value); }
void QmlPreviewModel::setRate(double value) { playbackControl()->setPlaybackRate(value); }
void QmlPreviewModel::setPlaying(bool value)
{
    if (value != playing()) playbackControl()->togglePlayback();
}

void QmlPreviewModel::toggleRenderMode()
{
    surface()->toggleMuriRenderMode();
    refreshFromBackend();
}

void QmlPreviewModel::setMuriCheckEnabled(bool enabled)
{
    if (surface() == nullptr) {
        return;
    }
    const RenderMode current = surface()->muriRenderMode();
    if (enabled) {
        if (current == RenderMode::MaimuriDxStyle) {
            return;
        }
        surface()->setMuriRenderMode(RenderMode::MaimuriDxStyle);
    } else {
        if (current != RenderMode::MaimuriDxStyle) {
            return;
        }
        surface()->setMuriRenderMode(lastRegularMode_);
    }
    refreshFromBackend();
}

void QmlPreviewModel::setSmoothStarErase(bool enabled)
{
    if (surface() == nullptr) {
        return;
    }
    lastRegularMode_ = enabled ? RenderMode::Native : RenderMode::EraseByArea;
    if (surface()->muriRenderMode() != RenderMode::MaimuriDxStyle) {
        surface()->setMuriRenderMode(lastRegularMode_);
    }
    refreshFromBackend();
}

void QmlPreviewModel::stop() { playbackControl()->stop(); }

void QmlPreviewModel::togglePlayback() { playbackControl()->togglePlayback(); }

void QmlPreviewModel::adjustRate(int direction) { playbackControl()->nudgePlaybackRate(direction); }

void QmlPreviewModel::logPreviewInteraction(const QString& action, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("qml_ui/preview_interaction"),
        action.trimmed().isEmpty()
            ? payload
            : (payload.isEmpty() ? action : QStringLiteral("%1 %2").arg(action, payload)),
        true);
}

void QmlPreviewModel::beginScrub() { playbackControl()->beginScrub(); }

void QmlPreviewModel::updateScrub(double second) { playbackControl()->updateScrub(second); }

void QmlPreviewModel::endScrub(double second) { playbackControl()->endScrub(second); }
