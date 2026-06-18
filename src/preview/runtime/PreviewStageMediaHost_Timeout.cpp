#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "preview/runtime/PreviewSharedD3D11Device.h"  // H2: single_device= log field

#include <cstdio>  // G2 Diag: std::snprintf for sync rate-change beacon lines

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>          // F1: DXGI adapter probe to auto-detect integrated GPUs
#include <wrl/client.h>
#pragma comment(lib, "dxgi.lib")
#endif

#ifdef MIACODE_USE_QTAVPLAYER
// Windows preview decode backend: FFmpeg via QtAVPlayer. QT_AVPLAYER_MULTIMEDIA
// turns on the QAVVideoFrame -> QVideoFrame bridge (the conversion we feed to
// the QML VideoOutput sink). Still need QVideoFrame/QVideoSink for delivery.
#ifndef QT_AVPLAYER_MULTIMEDIA
#define QT_AVPLAYER_MULTIMEDIA
#endif
#include <QtAVPlayer/qavplayer.h>
#include <QtAVPlayer/qavvideoframe.h>
#include <QVideoFrame>
#include <QVideoSink>
#if defined(Q_OS_WIN)
#include <QtAVPlayer/qavd3d11sharedcontext_p.h>  // HW-decode diag counters / seek catch-up
#endif
#elif defined(HAVE_QT_MULTIMEDIA)
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QVariant>
#include <QtMath>

#include "preview/runtime/PreviewStageMediaHostInternal.h"

using namespace miacode::preview::psmh_detail;

namespace {

constexpr int kMediaSeekPrepareTimeoutMs = 800;
constexpr int kVideoPlaybackWatchdogMs = 600;
constexpr int kVideoPlaybackSoftRecoveryMaxConsecutive = 2;

}  // namespace

void PreviewStageMediaHost::schedulePreparedPlaybackTimeout(quint64 transactionId, qint64 targetMs)
{
#ifdef MIACODE_USE_QTAVPLAYER
    const quint64 serial = ++preparedPlaybackTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, transactionId, targetMs]() {
        if (serial != preparedPlaybackTimeoutSerial_
            || !preparedPlaybackPending_
            || preparedPlaybackTransaction_ != transactionId
            || preparedPlaybackTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = preparedPlaybackTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_timeout"),
            QString("txn=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5")
                .arg(transactionId)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_));
        // Frame-accurate backend: if the ack frame simply hasn't been observed
        // yet, settle the handshake so the timeline start isn't wedged. No
        // backend rebuild (unlike the QMediaPlayer path).
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = true;
        ++preparedPlaybackTimeoutSerial_;
        emit playbackStartPrepared(targetSecond, transactionId);
    });
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(transactionId);
    Q_UNUSED(targetMs);
#else
    const quint64 serial = ++preparedPlaybackTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, transactionId, targetMs]() {
        if (serial != preparedPlaybackTimeoutSerial_
            || !preparedPlaybackPending_
            || preparedPlaybackTransaction_ != transactionId
            || preparedPlaybackTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = preparedPlaybackTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_timeout"),
            QString("txn=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5 age_ms=%6 playback_state=%7")
                .arg(transactionId)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_)
                .arg(currentVideoFrameAgeForDiagnosticsMs())
                .arg(playbackStateName(playerPlaybackState(player_)))
        );
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = true;
        ++preparedPlaybackTimeoutSerial_;
        recoverVideoBackend(QStringLiteral("prepare_playback_timeout"), targetSecond, false);
        if (preparedPlaybackReady_ && preparedPlaybackTransaction_ == transactionId) {
            emit playbackStartPrepared(targetSecond, transactionId);
        }
    });
#endif
}


void PreviewStageMediaHost::schedulePausedSeekTimeout(quint64 generation, qint64 targetMs)
{
#ifdef MIACODE_USE_QTAVPLAYER
    const quint64 serial = ++pausedSeekTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, generation, targetMs]() {
        if (serial != pausedSeekTimeoutSerial_
            || !pausedSeekCompletionPending_
            || pausedSeekGeneration_ != generation
            || pausedSeekTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = pausedSeekTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_timeout"),
            QString("generation=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5")
                .arg(generation)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_));
        pausedSeekCompletionPending_ = false;
        ++pausedSeekTimeoutSerial_;
        emit pausedSeekCompleted(targetSecond, generation);
    });
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(generation);
    Q_UNUSED(targetMs);
#else
    const quint64 serial = ++pausedSeekTimeoutSerial_;
    QTimer::singleShot(kMediaSeekPrepareTimeoutMs, this, [this, serial, generation, targetMs]() {
        if (serial != pausedSeekTimeoutSerial_
            || !pausedSeekCompletionPending_
            || pausedSeekGeneration_ != generation
            || pausedSeekTargetMs_ != targetMs) {
            return;
        }
        const double targetSecond = pausedSeekTargetSecond_;
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_timeout"),
            QString("generation=%1 second=%2 target_ms=%3 position_ms=%4 frame_count=%5 age_ms=%6 playback_state=%7")
                .arg(generation)
                .arg(targetSecond, 0, 'f', 6)
                .arg(targetMs)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(videoFrameCountTotal_)
                .arg(currentVideoFrameAgeForDiagnosticsMs())
                .arg(playbackStateName(playerPlaybackState(player_)))
        );
        const bool recovered = recoverVideoBackend(QStringLiteral("paused_seek_timeout"), targetSecond, false);
        if (!recovered
            && pausedSeekCompletionPending_
            && pausedSeekGeneration_ == generation
            && pausedSeekTargetMs_ == targetMs) {
            pausedSeekCompletionPending_ = false;
            ++pausedSeekTimeoutSerial_;
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 target_ms=%3 source=timeout_no_recover")
                    .arg(generation)
                    .arg(targetSecond, 0, 'f', 6)
                    .arg(targetMs)
            );
            emit pausedSeekCompleted(targetSecond, generation);
        }
    });
#endif
}


void PreviewStageMediaHost::scheduleVideoPlaybackWatchdog(const QString& reason)
{
// QMediaPlayer-only: QtAVPlayer's decode loop doesn't stall the way the Qt
// Multimedia backend did, so the no-new-frame watchdog is unused there.
#if !defined(HAVE_QT_MULTIMEDIA) || defined(MIACODE_USE_QTAVPLAYER)
    Q_UNUSED(reason);
#else
    if (mediaKind_ != MediaKind::Video || !videoPlaybackActive_) {
        return;
    }
    const quint64 serial = ++videoPlaybackWatchdogSerial_;
    const qint64 frameCount = videoFrameCountTotal_;
    const double targetSecond = observedPlayheadSecond_;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/watchdog/schedule tid=%lu serial=%llu reason=%s frame_count=%lld age_ms=%lld rate=%.3f",
            currentBeaconTid(),
            static_cast<unsigned long long>(serial),
            reason.toUtf8().constData(),
            static_cast<long long>(frameCount),
            static_cast<long long>(currentVideoFrameAgeForDiagnosticsMs()),
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    QTimer::singleShot(kVideoPlaybackWatchdogMs, this, [this, serial, frameCount, targetSecond, reason]() {
        if (serial != videoPlaybackWatchdogSerial_
            || mediaKind_ != MediaKind::Video
            || !videoPlaybackActive_) {
            miacode::oplog::appendStartupBeaconLine("preview/watchdog/skip_stale_or_inactive");
            return;
        }
        const qint64 ageMs = currentVideoFrameAgeForDiagnosticsMs();
        const bool noNewFrame = videoFrameCountTotal_ <= frameCount;
        const bool staleFrame = ageMs >= kVideoPlaybackWatchdogMs;
        const bool notPlaying = playerPlaybackState(player_) != QMediaPlayer::PlayingState;
        if (!noNewFrame && !staleFrame && !notPlaying) {
            consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
            miacode::oplog::appendStartupBeaconLine("preview/watchdog/healthy");
            return;
        }
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_watchdog_timeout"),
            QString("reason=%1 target_second=%2 observed_second=%3 no_new_frame=%4 stale_frame=%5 not_playing=%6 frame_count=%7 initial_frame_count=%8 age_ms=%9 playback_state=%10 soft_recovery_count=%11")
                .arg(reason)
                .arg(targetSecond, 0, 'f', 6)
                .arg(observedPlayheadSecond_, 0, 'f', 6)
                .arg(noNewFrame ? 1 : 0)
                .arg(staleFrame ? 1 : 0)
                .arg(notPlaying ? 1 : 0)
                .arg(videoFrameCountTotal_)
                .arg(frameCount)
                .arg(ageMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(consecutiveVideoPlaybackSoftRecoveryCount_)
        );
        trySoftRecoverVideoPlayback(
            QStringLiteral("playback_watchdog_%1").arg(reason),
            observedPlayheadSecond_,
            frameCount,
            ageMs);
    });
#endif
}


bool PreviewStageMediaHost::trySoftRecoverVideoPlayback(const QString& reason,
                                                        double targetSecond,
                                                        qint64 initialFrameCount,
                                                        qint64 ageMs)
{
// QMediaPlayer-only soft-recovery (seek-flush / rebind). QtAVPlayer path no-op.
#if !defined(HAVE_QT_MULTIMEDIA) || defined(MIACODE_USE_QTAVPLAYER)
    Q_UNUSED(reason);
    Q_UNUSED(targetSecond);
    Q_UNUSED(initialFrameCount);
    Q_UNUSED(ageMs);
    return false;
#else
    if (shuttingDown_
        || mediaKind_ != MediaKind::Video
        || player_ == nullptr
        || !videoPlaybackActive_) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_skip"),
            QString("reason=%1 shutting_down=%2 kind=%3 has_player=%4 active=%5")
                .arg(reason)
                .arg(shuttingDown_ ? 1 : 0)
                .arg(debugMediaTypeName())
                .arg(player_ != nullptr ? 1 : 0)
                .arg(videoPlaybackActive_ ? 1 : 0));
        return false;
    }
    if (consecutiveVideoPlaybackSoftRecoveryCount_ >= kVideoPlaybackSoftRecoveryMaxConsecutive) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_exhausted"),
            QString("reason=%1 count=%2 limit=%3 frame_count=%4 initial_frame_count=%5 age_ms=%6 playback_state=%7 position_ms=%8")
                .arg(reason)
                .arg(consecutiveVideoPlaybackSoftRecoveryCount_)
                .arg(kVideoPlaybackSoftRecoveryMaxConsecutive)
                .arg(videoFrameCountTotal_)
                .arg(initialFrameCount)
                .arg(ageMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_->position()));
        miacode::oplog::appendStartupBeaconLine("preview/watchdog/soft_recover_exhausted");
        return false;
    }

    const int step = consecutiveVideoPlaybackSoftRecoveryCount_++;
    const QString strategy = step == 0
        ? QStringLiteral("seek_flush")
        : QStringLiteral("rebind_sink_seek_flush");
    const double clampedTargetSecond = qMax(0.0, qIsFinite(targetSecond) ? targetSecond : currentPlaybackSecond());
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedTargetSecond + timelineOffsetSeconds_) * 1000.0));
    const qint64 flushMs = targetMs + 1;
    ++videoPlaybackWatchdogSerial_;
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 24);
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 16);

    {
        char buf[300];
        std::snprintf(buf, sizeof(buf),
            "preview/watchdog/soft_recover_begin tid=%lu step=%d strategy=%s target_ms=%lld pos=%lld frame_count=%lld age_ms=%lld rate=%.3f",
            currentBeaconTid(),
            step,
            strategy.toUtf8().constData(),
            static_cast<long long>(targetMs),
            static_cast<long long>(player_->position()),
            static_cast<long long>(videoFrameCountTotal_),
            static_cast<long long>(ageMs),
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    appendPreviewStageMediaLog(
        QStringLiteral("video_playback_soft_recover_begin"),
        QString("reason=%1 step=%2 strategy=%3 target_second=%4 target_ms=%5 flush_ms=%6 frame_count=%7 initial_frame_count=%8 age_ms=%9 playback_state=%10 position_ms=%11")
            .arg(reason)
            .arg(step)
            .arg(strategy)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(flushMs)
            .arg(videoFrameCountTotal_)
            .arg(initialFrameCount)
            .arg(ageMs)
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(player_->position()));

    if (step == 1) {
        if (videoSinkFrameConnection_) {
            QObject::disconnect(videoSinkFrameConnection_);
            videoSinkFrameConnection_ = QMetaObject::Connection();
        }
        videoSink_.clear();
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        bindVideoOutput();
    }

    player_->pause();
    player_->setPosition(flushMs);
    videoPlaybackActive_ = true;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.restart();
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }

    QTimer::singleShot(0, this, [this, targetMs, reason, strategy]() {
        if (shuttingDown_
            || mediaKind_ != MediaKind::Video
            || player_ == nullptr
            || !videoPlaybackActive_) {
            return;
        }
        player_->setPosition(targetMs);
        player_->play();
        appendPreviewStageMediaLog(
            QStringLiteral("video_playback_soft_recover_play"),
            QString("reason=%1 strategy=%2 target_ms=%3 playback_state=%4 position_ms=%5")
                .arg(reason)
                .arg(strategy)
                .arg(targetMs)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_->position()));
        miacode::oplog::appendStartupBeaconLine("preview/watchdog/soft_recover_play");
        scheduleVideoPlaybackWatchdog(QStringLiteral("soft_recover_%1").arg(strategy));
    });
    return true;
#endif
}


#ifdef MIACODE_USE_QTAVPLAYER
void PreviewStageMediaHost::settlePendingSeekAcks(double mediaSecondStart, double mediaSecondEnd)
{
    const qint64 startMs = qRound64(mediaSecondStart * 1000.0);
    const qint64 endMs = qRound64(qMax(mediaSecondStart, mediaSecondEnd) * 1000.0);
    const auto reaches = [&](qint64 targetMs) {
        return targetMs >= startMs - kPausedSeekAckToleranceMs
            && targetMs <= endMs + kPausedSeekAckToleranceMs;
    };
    if (pausedSeekCompletionPending_ && pausedSeekTargetMs_ >= 0 && reaches(pausedSeekTargetMs_)) {
        pausedSeekCompletionPending_ = false;
        ++pausedSeekTimeoutSerial_;
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_ack"),
            QString("generation=%1 second=%2 target_ms=%3 frame_start_ms=%4 frame_end_ms=%5 source=frame")
                .arg(pausedSeekGeneration_)
                .arg(pausedSeekTargetSecond_, 0, 'f', 6)
                .arg(pausedSeekTargetMs_)
                .arg(startMs)
                .arg(endMs));
        emit pausedSeekCompleted(pausedSeekTargetSecond_, pausedSeekGeneration_);
    }
    if (preparedPlaybackPending_ && preparedPlaybackTargetMs_ >= 0 && reaches(preparedPlaybackTargetMs_)) {
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = true;
        ++preparedPlaybackTimeoutSerial_;
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_ready"),
            QString("txn=%1 second=%2 target_ms=%3 frame_start_ms=%4 frame_end_ms=%5 source=frame")
                .arg(preparedPlaybackTransaction_)
                .arg(preparedPlaybackTargetSecond_, 0, 'f', 6)
                .arg(preparedPlaybackTargetMs_)
                .arg(startMs)
                .arg(endMs));
        emit playbackStartPrepared(preparedPlaybackTargetSecond_, preparedPlaybackTransaction_);
    }
}
#endif  // MIACODE_USE_QTAVPLAYER
