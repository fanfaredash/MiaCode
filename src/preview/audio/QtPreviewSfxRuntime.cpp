#include "QtPreviewSfxRuntime.h"

#include "BassPreviewAudioBackend.h"
#include "MiniaudioPreviewAudioBackend.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"

namespace {

bool runtimeAudioDebugEnabled()
{
    return miacode::debug_options::audioDebugOutputEnabled();
}

void appendAudioDebugLog(const QString& message)
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Audio, QString(), message);
}

}  // namespace

QtPreviewSfxRuntime::QtPreviewSfxRuntime(QObject* parent)
    : QObject(parent)
    , backend_(createBackend())
{
    appendAudioDebugLog(QString("QtPreviewSfxRuntime created backend=%1").arg(backend_->backendId()));
}

QtPreviewSfxRuntime::~QtPreviewSfxRuntime()
{
    appendAudioDebugLog(QString("QtPreviewSfxRuntime destroying backend=%1").arg(backend_->backendId()));
}

std::unique_ptr<miacode::preview_audio::PreviewAudioBackend> QtPreviewSfxRuntime::createBackend() const
{
#ifdef Q_OS_WIN
    auto bassBackend = std::make_unique<BassPreviewAudioBackend>();
    QString bassReason;
    const bool bassReady = bassBackend->canBePrimary(&bassReason);
    appendAudioDebugLog(
        QString("preview_audio_backend selected=bass ready=%1 reason=%2")
            .arg(bassReady ? 1 : 0)
            .arg(bassReason));
    return bassBackend;
#endif
    return std::make_unique<MiniaudioPreviewAudioBackend>();
}

void QtPreviewSfxRuntime::setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir)
{
    backend_->setWarmupResolvedPaths(chartPath, trackPath, sfxDir);
}

void QtPreviewSfxRuntime::reloadAssets(const PreviewAudioSettings& settings)
{
    backend_->reloadAssets(settings);
}

bool QtPreviewSfxRuntime::audioEngineInitialized() const
{
    return backend_->audioEngineInitialized();
}

void QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    backend_->setChartPath(chartPath);
}

void QtPreviewSfxRuntime::setBackgroundTrackOffsetSeconds(double seconds)
{
    backend_->setBackgroundTrackOffsetSeconds(seconds);
}

void QtPreviewSfxRuntime::setBackgroundTrackPlaybackRate(double rate)
{
    backend_->setBackgroundTrackPlaybackRate(rate);
}

void QtPreviewSfxRuntime::applyLevels(const PreviewAudioSettings& settings)
{
    backend_->applyLevels(settings);
}

void QtPreviewSfxRuntime::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    backend_->configureTimeline(noteMarkers, playbackRate, timingSettings);
}

void QtPreviewSfxRuntime::clearTimeline()
{
    backend_->clearTimeline();
}

void QtPreviewSfxRuntime::setPlaybackTransactionId(quint64 transactionId)
{
    backend_->setPlaybackTransactionId(transactionId);
}

double QtPreviewSfxRuntime::preparePreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate)
{
    return backend_->preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
}

void QtPreviewSfxRuntime::commitPreparedPreviewPlayback()
{
    backend_->commitPreparedPreviewPlayback();
}

void QtPreviewSfxRuntime::cancelPreparedPreviewPlayback()
{
    backend_->cancelPreparedPreviewPlayback();
}

double QtPreviewSfxRuntime::preparedStartSecond() const
{
    return backend_->preparedStartSecond();
}

void QtPreviewSfxRuntime::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    backend_->applyPausedPreviewState(noteMarkers, noteMarkersChanged, pauseSecond, playbackRate, timingSettings);
}

double QtPreviewSfxRuntime::startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate)
{
    return backend_->startPreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::capturePausedPreviewTransaction()
{
    return backend_->capturePausedPreviewTransaction();
}

QtPreviewSfxRuntime::PausePreviewResult QtPreviewSfxRuntime::pausePreviewPlaybackTransaction()
{
    return backend_->pausePreviewPlaybackTransaction();
}

double QtPreviewSfxRuntime::resumeRetainedPreviewPlaybackTransaction()
{
    return backend_->resumeRetainedPreviewPlaybackTransaction();
}

double QtPreviewSfxRuntime::seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying)
{
    return backend_->seekRetainedPreviewPlaybackTransaction(targetSecond, continuePlaying);
}

void QtPreviewSfxRuntime::resetRetainedPreviewPlaybackTransaction(double targetSecond)
{
    backend_->resetRetainedPreviewPlaybackTransaction(targetSecond);
}

void QtPreviewSfxRuntime::clearRetainedPreviewPlaybackTransaction()
{
    backend_->clearRetainedPreviewPlaybackTransaction();
}

QtPreviewSfxRuntime::RetainedPlaybackMode QtPreviewSfxRuntime::retainedPlaybackMode() const
{
    return backend_->retainedPlaybackMode();
}

QtPreviewSfxRuntime::RetainedBgmState QtPreviewSfxRuntime::retainedBgmState() const
{
    return backend_->retainedBgmState();
}

double QtPreviewSfxRuntime::authoritativePlaybackSecond() const
{
    return backend_->authoritativePlaybackSecond();
}

double QtPreviewSfxRuntime::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    return backend_->syncPreviewPlaybackClockTransaction(fallbackSecond);
}

void QtPreviewSfxRuntime::resetCursor(double second, bool includeCurrentSecond)
{
    backend_->resetCursor(second, includeCurrentSecond);
}

void QtPreviewSfxRuntime::drainEvents(double second)
{
    backend_->drainEvents(second);
}

void QtPreviewSfxRuntime::pauseTouchholdVoices()
{
    backend_->pauseTouchholdVoices();
}

void QtPreviewSfxRuntime::restoreTouchholdVoices(double second)
{
    backend_->restoreTouchholdVoices(second);
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    backend_->syncBackgroundTrack(timelineSecond);
}

bool QtPreviewSfxRuntime::hasBackgroundTrack() const
{
    return backend_->hasBackgroundTrack();
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return backend_->isBackgroundTrackRunning();
}

void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    backend_->startBackgroundTrack(second);
}

void QtPreviewSfxRuntime::seekBackgroundTrack(double second)
{
    backend_->seekBackgroundTrack(second);
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    backend_->pauseBackgroundTrack();
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    return backend_->backgroundPlaybackSecond();
}

bool QtPreviewSfxRuntime::audition(const QString& kind, double gain)
{
    return backend_->audition(kind, gain);
}

void QtPreviewSfxRuntime::stopAll()
{
    backend_->stopAll();
}
