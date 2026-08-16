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

void PreviewStageMediaHost::preparePlaybackStart(double seconds, quint64 transactionId)
{
#ifdef MIACODE_USE_QTAVPLAYER
    initializeBackendObjects();
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    preparedPlaybackTransaction_ = transactionId;
    preparedPlaybackTargetSecond_ = clampedSecond;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    appendPreviewStageMediaLog(
        QStringLiteral("prepare_playback_start"),
        QString("txn=%1 second=%2 rate=%3 offset=%4 has_video=%5")
            .arg(transactionId)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit playbackStartPrepared(clampedSecond, transactionId);
        emit diagnosticsChanged();
        return;
    }

    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    preparedPlaybackTargetMs_ = targetMs;
    preparedPlaybackPending_ = true;
    player_->pause();
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        // Already parked at (or within a frame of) the target — the current
        // decoded frame already shows it, so ack on the next event-loop turn
        // instead of forcing a redundant seek.
        QMetaObject::invokeMethod(this, [this, transactionId, clampedSecond]() {
            if (!preparedPlaybackPending_ || preparedPlaybackTransaction_ != transactionId) {
                return;
            }
            preparedPlaybackPending_ = false;
            preparedPlaybackReady_ = true;
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_ready"),
                QString("txn=%1 second=%2 source=queued").arg(transactionId).arg(clampedSecond, 0, 'f', 6));
            emit playbackStartPrepared(clampedSecond, transactionId);
        }, Qt::QueuedConnection);
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    schedulePreparedPlaybackTimeout(transactionId, targetMs);
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(seconds);
    Q_UNUSED(transactionId);
#else
    initializeBackendObjects();
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    preparedPlaybackTransaction_ = transactionId;
    preparedPlaybackTargetSecond_ = clampedSecond;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    appendPreviewStageMediaLog(
        QStringLiteral("prepare_playback_start"),
        QString("txn=%1 second=%2 rate=%3 offset=%4 has_video=%5")
            .arg(transactionId)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit playbackStartPrepared(clampedSecond, transactionId);
        emit diagnosticsChanged();
        return;
    }

    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    preparedPlaybackTargetMs_ = targetMs;
    preparedPlaybackPending_ = true;
    player_->pause();
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        QMetaObject::invokeMethod(
            this,
            [this, transactionId, clampedSecond]() {
                if (!preparedPlaybackPending_ || preparedPlaybackTransaction_ != transactionId) {
                    appendPreviewStageMediaLog(
                        QStringLiteral("prepare_playback_drop"),
                        QString("txn=%1 current_txn=%2 reason=queued_ack_stale")
                            .arg(transactionId)
                            .arg(preparedPlaybackTransaction_)
                    );
                    return;
                }
                preparedPlaybackPending_ = false;
                preparedPlaybackReady_ = true;
                appendPreviewStageMediaLog(
                    QStringLiteral("prepare_playback_ready"),
                    QString("txn=%1 second=%2 position_ms=%3 target_ms=%4 source=queued")
                        .arg(transactionId)
                        .arg(clampedSecond, 0, 'f', 6)
                        .arg(lastSeekMs_)
                        .arg(preparedPlaybackTargetMs_)
                );
                emit playbackStartPrepared(clampedSecond, transactionId);
            },
            Qt::QueuedConnection
        );
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    schedulePreparedPlaybackTimeout(transactionId, targetMs);
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::commitPreparedPlaybackStart(double currentTimelineSecond)
{
#ifdef MIACODE_USE_QTAVPLAYER
    initializeBackendObjects();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }

    const double clampedSecond = qMax(0.0, currentTimelineSecond);
    observedPlayheadSecond_ = clampedSecond;
    lastTimelineSecond_ = clampedSecond;
    const double rawSecond = clampedSecond + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        // Timeline still ahead of the clip's in-point (negative offset): hold
        // paused until the playhead reaches second 0 of the media.
        player_->pause();
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("commit_prepared_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3")
                .arg(playbackTransactionId_)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6));
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = false;
        preparedPlaybackTargetMs_ = -1;
        preparedPlaybackTargetSecond_ = 0.0;
        preparedPlaybackTransaction_ = 0;
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    // This cache holds the last requested target, not a decoder-confirmed
    // position. A fresh player may take prepare's queued fast path, so commit
    // must establish this playback transaction with a physical seek.
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    appendPreviewStageMediaLog(
        QStringLiteral("commit_prepared_playback"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4 reseek=%5")
            .arg(playbackTransactionId_)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(1));
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(currentTimelineSecond);
#else
    initializeBackendObjects();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return;
    }

    const double clampedSecond = qMax(0.0, currentTimelineSecond);
    observedPlayheadSecond_ = clampedSecond;
    lastTimelineSecond_ = clampedSecond;
    const double rawSecond = clampedSecond + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        player_->pause();
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("commit_prepared_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3")
                .arg(playbackTransactionId_)
                .arg(clampedSecond, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6));
        preparedPlaybackPending_ = false;
        preparedPlaybackReady_ = false;
        preparedPlaybackTargetMs_ = -1;
        preparedPlaybackTargetSecond_ = 0.0;
        preparedPlaybackTransaction_ = 0;
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    // This cache records a requested target only. It cannot prove a freshly
    // created backend completed its prepare-side seek before commit.
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    if (playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
        QMetaObject::invokeMethod(player_, [this]() {
            if (mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
                player_->play();
            }
        }, Qt::QueuedConnection);
    }
    appendPreviewStageMediaLog(
        QStringLiteral("commit_prepared_playback"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4 late=%5 reseek=%6")
            .arg(playbackTransactionId_)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(qAbs(clampedSecond - preparedPlaybackTargetSecond_) > 0.0005 ? 1 : 0)
            .arg(1));
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    updateClockDelta();
    updateVideoFrameStallState(true);
    scheduleVideoPlaybackWatchdog(QStringLiteral("commit_prepared_playback"));
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::cancelPreparedPlaybackStart(quint64 transactionId)
{
    if (transactionId != 0 && preparedPlaybackTransaction_ != transactionId) {
        return;
    }
    if (!preparedPlaybackPending_ && !preparedPlaybackReady_ && preparedPlaybackTransaction_ == 0) {
        return;
    }
    appendPreviewStageMediaLog(
        QStringLiteral("prepare_playback_cancel"),
        QString("txn=%1 pending=%2 ready=%3")
            .arg(preparedPlaybackTransaction_)
            .arg(preparedPlaybackPending_ ? 1 : 0)
            .arg(preparedPlaybackReady_ ? 1 : 0));
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
}


bool PreviewStageMediaHost::hasPreparedPlaybackStart(quint64 transactionId) const
{
    return preparedPlaybackReady_ && preparedPlaybackTransaction_ == transactionId;
}


void PreviewStageMediaHost::setTimelineOffsetSeconds(double seconds)
{
    timelineOffsetSeconds_ = qIsFinite(seconds) ? seconds : 0.0;
    updateClockDelta();
    emit diagnosticsChanged();
}


void PreviewStageMediaHost::setPlayheadSeconds(double seconds)
{
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();

#ifdef MIACODE_USE_QTAVPLAYER
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    emit diagnosticsChanged();
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(seconds);
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::startPlayback(double seconds)
{
    MC_OP("PreviewStageMediaHost::startPlayback");
    recordPvMemoryBoundary(PvMemoryBoundary::Play);
#ifdef MIACODE_USE_QTAVPLAYER
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    appendPreviewStageMediaLog(
        QStringLiteral("start_playback"),
        QString("txn=%1 second=%2 raw_second=%3 rate=%4 offset=%5 has_video=%6")
            .arg(playbackTransactionId_)
            .arg(observedPlayheadSecond_, 0, 'f', 6)
            .arg(observedPlayheadSecond_ + timelineOffsetSeconds_, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = qMax(0.0, seconds);
    const double rawSecond = seconds + timelineOffsetSeconds_;
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    if (rawSecond < 0.0) {
        // Negative offset: timeline is ahead of the clip in-point — hold the
        // first media frame paused until the playhead reaches media second 0.
        player_->pause();
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
    } else {
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.restart();
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_started"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
    }
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(seconds);
#else
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    appendPreviewStageMediaLog(
        QStringLiteral("start_playback"),
        QString("txn=%1 second=%2 raw_second=%3 rate=%4 offset=%5 has_video=%6")
            .arg(playbackTransactionId_)
            .arg(observedPlayheadSecond_, 0, 'f', 6)
            .arg(observedPlayheadSecond_ + timelineOffsetSeconds_, 0, 'f', 6)
            .arg(playbackRate_, 0, 'f', 3)
            .arg(timelineOffsetSeconds_, 0, 'f', 6)
            .arg(mediaKind_ == MediaKind::Video && player_ != nullptr ? 1 : 0));
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 24);
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 16);

    lastTimelineSecond_ = qMax(0.0, seconds);
    const double rawSecond = seconds + timelineOffsetSeconds_;
    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/play/start_before_set_position tid=%lu txn=%llu target_ms=%lld rate=%.3f status=%d state=%d pos=%lld",
            currentBeaconTid(),
            static_cast<unsigned long long>(playbackTransactionId_),
            static_cast<long long>(targetMs),
            playbackRate_,
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setPosition(targetMs);
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/play/start_after_set_position tid=%lu txn=%llu pos=%lld status=%d state=%d",
            currentBeaconTid(),
            static_cast<unsigned long long>(playbackTransactionId_),
            static_cast<long long>(player_->position()),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (rawSecond < 0.0) {
        miacode::oplog::appendStartupBeaconLine("preview/play/start_before_pause_negative_raw");
        player_->pause();
        miacode::oplog::appendStartupBeaconLine("preview/play/start_after_pause_negative_raw");
        videoPlaybackPendingStart_ = true;
        videoPlaybackActive_ = false;
        videoPlaybackActiveElapsed_.invalidate();
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_pending"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
    } else {
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/play/start_before_play tid=%lu txn=%llu rate=%.3f status=%d state=%d",
                currentBeaconTid(),
                static_cast<unsigned long long>(playbackTransactionId_),
                playbackRate_,
                static_cast<int>(player_->mediaStatus()),
                static_cast<int>(playerPlaybackState(player_)));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        player_->play();
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/play/start_after_play tid=%lu txn=%llu status=%d state=%d pos=%lld",
                currentBeaconTid(),
                static_cast<unsigned long long>(playbackTransactionId_),
                static_cast<int>(player_->mediaStatus()),
                static_cast<int>(playerPlaybackState(player_)),
                static_cast<long long>(player_->position()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.restart();
        consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
        if (playerPlaybackState(player_) != QMediaPlayer::PlayingState) {
            QMetaObject::invokeMethod(player_, [this]() {
                if (mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
                    player_->play();
                }
            }, Qt::QueuedConnection);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("start_playback_started"),
            QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
                .arg(playbackTransactionId_)
                .arg(seconds, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(targetMs));
        scheduleVideoPlaybackWatchdog(QStringLiteral("start_playback"));
    }
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::submitPausedSeek(double seconds, quint64 generation)
{
#ifdef MIACODE_USE_QTAVPLAYER
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        emit pausedSeekCompleted(clampedSecond, generation);
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    pausedSeekGeneration_ = generation;
    pausedSeekTargetMs_ = targetMs;
    pausedSeekTargetSecond_ = clampedSecond;
    pausedSeekCompletionPending_ = true;
    appendPreviewStageMediaLog(
        QStringLiteral("paused_seek_media_submit"),
        QString("generation=%1 second=%2 target_ms=%3")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(targetMs));
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        // Already showing this frame — ack on the next event-loop turn.
        QMetaObject::invokeMethod(this, [this, generation, clampedSecond]() {
            if (!pausedSeekCompletionPending_ || pausedSeekGeneration_ != generation) {
                return;
            }
            pausedSeekCompletionPending_ = false;
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 source=queued").arg(generation).arg(clampedSecond, 0, 'f', 6));
            emit pausedSeekCompleted(clampedSecond, generation);
        }, Qt::QueuedConnection);
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    schedulePausedSeekTimeout(generation, targetMs);
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(seconds);
    Q_UNUSED(generation);
#else
    const double clampedSecond = qMax(0.0, seconds);
    observedPlayheadSecond_ = clampedSecond;
    updateClockDelta();
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        emit pausedSeekCompleted(clampedSecond, generation);
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = clampedSecond;
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    pausedSeekGeneration_ = generation;
    pausedSeekTargetMs_ = targetMs;
    pausedSeekTargetSecond_ = clampedSecond;
    pausedSeekCompletionPending_ = true;
    appendPreviewStageMediaLog(
        QStringLiteral("paused_seek_media_submit"),
        QString("generation=%1 second=%2 target_ms=%3")
            .arg(generation)
            .arg(clampedSecond, 0, 'f', 6)
            .arg(targetMs)
    );
    if (lastSeekMs_ >= 0 && qAbs(targetMs - lastSeekMs_) < kSeekCoalesceToleranceMs) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, clampedSecond]() {
                if (!pausedSeekCompletionPending_ || pausedSeekGeneration_ != generation) {
                    appendPreviewStageMediaLog(
                        QStringLiteral("paused_seek_media_drop"),
                        QString("generation=%1 current_generation=%2 reason=queued_ack_stale")
                            .arg(generation)
                            .arg(pausedSeekGeneration_)
                    );
                    return;
                }
                pausedSeekCompletionPending_ = false;
                appendPreviewStageMediaLog(
                    QStringLiteral("paused_seek_media_ack"),
                    QString("generation=%1 second=%2 position_ms=%3 target_ms=%4 source=queued")
                        .arg(generation)
                        .arg(clampedSecond, 0, 'f', 6)
                        .arg(lastSeekMs_)
                        .arg(pausedSeekTargetMs_)
                );
                emit pausedSeekCompleted(clampedSecond, generation);
            },
            Qt::QueuedConnection
        );
        emit diagnosticsChanged();
        return;
    }
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    schedulePausedSeekTimeout(generation, targetMs);
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::syncPlayback(double seconds)
{
#ifdef MIACODE_USE_QTAVPLAYER
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = qMax(0.0, seconds);
    if (!videoPlaybackPendingStart_) {
        const bool wasPlaying = player_->state() == QAVPlayer::PlayingState;
        bool transitioned = false;
        if (videoPlaybackActive_ && !wasPlaying) {
            player_->play();
            transitioned = true;
        }
        updateClockDelta();
        const bool stallStateChanged = updateVideoFrameStallState(true);
        // Steady-state syncPlayback runs every preview tick (~60/s); only emit
        // diagnosticsChanged on an actual transition to keep the per-tick
        // frameState re-push off the hot path.
        if (transitioned || stallStateChanged) {
            emit diagnosticsChanged();
        }
        return;
    }

    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    appendPreviewStageMediaLog(
        QStringLiteral("sync_playback_started"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
            .arg(playbackTransactionId_)
            .arg(seconds, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs));
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(seconds);
#else
    initializeBackendObjects();
    observedPlayheadSecond_ = qMax(0.0, seconds);
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    lastTimelineSecond_ = qMax(0.0, seconds);
    if (!videoPlaybackPendingStart_) {
        const bool wasPlaying = playerPlaybackState(player_) == QMediaPlayer::PlayingState;
        bool transitioned = false;
        if (videoPlaybackActive_ && !wasPlaying) {
            player_->play();
            transitioned = true;
        }
        updateClockDelta();
        const bool stallStateChanged = updateVideoFrameStallState(true);
        // Steady-state syncPlayback is called every preview tick (~60/s). Emitting
        // diagnosticsChanged unconditionally here fans out to refreshPreviewStageMediaRouteDebugState
        // which re-pushes media stats into frameState on every tick even though values seldom change.
        // Limit the emission to actual state transitions; other paths (noteVideoFrameArrived,
        // pause, recover, stall transitions) continue to emit when something genuinely changed.
        if (transitioned || stallStateChanged) {
            emit diagnosticsChanged();
        }
        return;
    }

    const double rawSecond = seconds + timelineOffsetSeconds_;
    if (rawSecond < 0.0) {
        videoPlaybackActiveElapsed_.invalidate();
        updateVideoFrameStallState(true);
        updateClockDelta();
        emit diagnosticsChanged();
        return;
    }

    const qint64 targetMs = qMax<qint64>(0, qRound64(rawSecond * 1000.0));
    lastSeekMs_ = targetMs;
    player_->setPosition(targetMs);
    if (videoFrameElapsed_.isValid()) {
        videoFrameElapsed_.restart();
    }
    player_->play();
    videoPlaybackPendingStart_ = false;
    videoPlaybackActive_ = true;
    videoPlaybackActiveElapsed_.restart();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    appendPreviewStageMediaLog(
        QStringLiteral("sync_playback_started"),
        QString("txn=%1 second=%2 raw_second=%3 target_ms=%4")
            .arg(playbackTransactionId_)
            .arg(seconds, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(targetMs));
    scheduleVideoPlaybackWatchdog(QStringLiteral("sync_playback_started"));
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::pausePlayback()
{
    observedPlayheadSecond_ = currentPlaybackSecond();
    recordPvMemoryBoundary(PvMemoryBoundary::Pause);
#ifndef HAVE_QT_MULTIMEDIA
    return;
#else
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        player_->pause();
    }
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    ++videoPlaybackWatchdogSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
#endif
}


double PreviewStageMediaHost::currentPlaybackSecond() const
{
#ifndef HAVE_QT_MULTIMEDIA
    return 0.0;
#else
    if (mediaKind_ != MediaKind::Video || player_ == nullptr) {
        return 0.0;
    }
    if (!videoPlaybackActive_) {
        return qMax(0.0, lastTimelineSecond_);
    }
    return qMax(0.0, static_cast<double>(player_->position()) / 1000.0 - timelineOffsetSeconds_);
#endif
}


bool PreviewStageMediaHost::videoPlaybackActive() const
{
    return mediaKind_ == MediaKind::Video && videoPlaybackActive_;
}
