#include "QmlPreviewModel.h"

#include "app/quick_shell/QuickShellController.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MuriRenderOptions.h"
#include "mainwindow/MainWindow.h"

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

QmlPreviewModel::QmlPreviewModel(
    MainWindow& backend,
    QuickShellController& controller,
    QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , controller_(&controller)
{
    v2UiProbeEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
    connect(controller_, &QuickShellController::shellStateChanged, this, [this]() {
        updateV2UiProbePlaybackState();
        refreshFromController();
    });
    connect(backend_, &MainWindow::previewSkinDirectoryChanged, this, [this]() {
        refreshSkinDirectory();
        rebuildStatistics();
        emit statisticsChanged();
    });
    refreshSkinDirectory();
    refreshFromController(/*force=*/true);
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
    skinDirectory_ = backend_ != nullptr ? backend_->resolvePreviewSkinDir() : QString();
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

void QmlPreviewModel::refreshFromController(bool force)
{
    if (controller_ == nullptr || backend_ == nullptr) {
        return;
    }
    const double nextPosition = controller_->previewPositionSeconds();
    const double nextDuration = controller_->previewDurationSeconds();
    const double nextLowerBound = controller_->previewLowerBoundSeconds();
    QString nextRateLabel = controller_->previewSpeedLabel().trimmed();
    nextRateLabel.remove(QLatin1Char('x'), Qt::CaseInsensitive);
    bool rateOk = false;
    const double nextRate = nextRateLabel.toDouble(&rateOk);
    const bool nextPlaying = controller_->previewPlaying();
    const RenderMode nextRenderModeValue = backend_->muriRenderMode();
    const QString nextRenderMode = muriRenderModeToken(nextRenderModeValue);
    QString nextRenderModeLabel;
    switch (nextRenderModeValue) {
    case RenderMode::EraseByArea:
        nextRenderModeLabel = tr("按区消去");
        break;
    case RenderMode::MaimuriDxStyle:
        nextRenderModeLabel = tr("无理检测");
        break;
    case RenderMode::Native:
        nextRenderModeLabel = tr("常规模式");
        break;
    }
    const QStringList nextStatisticsTexts = controller_->previewStatsTexts();

    const bool positionChangedValue = force || nextPosition != positionSeconds_;
    const bool transportChangedValue = force
        || nextDuration != durationSeconds_
        || nextLowerBound != lowerBoundSeconds_
        || (rateOk ? nextRate : 1.0) != rate_;
    const bool playingChangedValue = force || nextPlaying != playing_;
    const bool renderModeChangedValue = force
        || nextRenderMode != renderMode_
        || nextRenderModeLabel != renderModeLabel_;
    const bool statisticsChangedValue = force || nextStatisticsTexts != statisticsTexts_;

    positionSeconds_ = nextPosition;
    durationSeconds_ = nextDuration;
    lowerBoundSeconds_ = nextLowerBound;
    rate_ = rateOk ? nextRate : 1.0;
    playing_ = nextPlaying;
    renderMode_ = nextRenderMode;
    renderModeLabel_ = nextRenderModeLabel;
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
}

void QmlPreviewModel::updateV2UiProbePlaybackState()
{
    if (!v2UiProbeEnabled_ || controller_ == nullptr) {
        return;
    }
    const bool playingNow = controller_->previewPlaying();
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
QVariantList QmlPreviewModel::statistics() const { return statistics_; }

QString QmlPreviewModel::currentSkinDirectory() const
{
    return skinDirectory_;
}

QObject* QmlPreviewModel::runtime() const { return controller_->previewRuntime(); }
QObject* QmlPreviewModel::mediaHost() const { return controller_->previewStageMediaHost(); }

void QmlPreviewModel::setPositionSeconds(double value) { controller_->seekPreview(value); }
void QmlPreviewModel::setRate(double value) { controller_->setPreviewRate(value); }
void QmlPreviewModel::setPlaying(bool value)
{
    if (value != playing()) controller_->togglePreviewPlayback();
}

void QmlPreviewModel::toggleRenderMode()
{
    controller_->toggleMuriRenderMode();
    refreshFromController();
}

void QmlPreviewModel::stop() { controller_->stopPreview(); }

void QmlPreviewModel::beginScrub() { controller_->beginPreviewScrub(); }

void QmlPreviewModel::updateScrub(double second) { controller_->updatePreviewScrub(second, true); }

void QmlPreviewModel::endScrub(double second) { controller_->endPreviewScrub(second, true); }
