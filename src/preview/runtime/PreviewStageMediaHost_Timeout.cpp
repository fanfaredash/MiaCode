#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "preview/runtime/PreviewSharedD3D11Device.h"  // H2: single_device= log field
#include "core/video/PreviewEndOfMediaPolicy.h"  // §5.2 EndOfMedia classification

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
#include <QtAVPlayer/qavpreviewdemuxdiag_p.h>  // §5.2 demuxer end-of-file provenance
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

// Stale-EndOfMedia recovery budget (audit §5.2), counted per loaded media.
// The first attempts re-seek the live decoder — cheap, and enough to clear the
// AVIO end-of-file latch that turns one failed byte read into "the file ended".
// The last attempt re-opens the source. After that the PV keeps its last frame
// rather than looping, so a genuinely unreadable file degrades instead of
// thrashing the decoder for the rest of the chart.
constexpr int kStaleEndOfMediaMaxRecoveries = 3;
constexpr int kStaleEndOfMediaSeekAttempts = 2;
// How long the seek-based recovery waits for its `seeked` acknowledgement before
// escalating to a reload. Sized well above the observed seek-landing latency and
// well below anything a user would read as "the PV is stuck".
constexpr int kStaleEndOfMediaResumeTimeoutMs = 900;

// Audit §5.2 — the decisive fact about an EndOfMedia is WHY the demuxer decided the
// file ended. `avio_feof` latches after any failed byte read, so an eof that came from
// there with a non-zero AVIO error is an I/O failure wearing an end-of-stream costume.
// Returns a leading-space `key=val …` fragment, or an empty string off the FFmpeg path.
QString demuxEndOfFileDiagnosticFields()
{
#ifdef MIACODE_USE_QTAVPLAYER
    QAVPreviewDemuxEofDiag diag{};
    qavGetPreviewDemuxEofDiag(&diag);
    return QStringLiteral(" demux_eof_events=%1 demux_eof_averror=%2 demux_eof_avio=%3 "
                          "demux_read_failures=%4 demux_last_read_result=%5 "
                          "demux_last_avio_error=%6 demux_last_eof_byte_pos=%7 "
                          "demux_seek_resets=%8")
        .arg(diag.eofEvents)
        .arg(diag.eofFromAvErrorEof)
        .arg(diag.eofFromAvioFeof)
        .arg(diag.readFailures)
        .arg(diag.lastReadResult)
        .arg(diag.lastAvioError)
        .arg(diag.lastEofBytePos)
        .arg(diag.seekResets);
#else
    return QString();
#endif
}

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
        preparedPlaybackLandingConfirmed_ = false;
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
        preparedPlaybackLandingConfirmed_ = false;
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
        preparedPlaybackLandingConfirmed_ = true;
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

// ---------------------------------------------------------------------------
// Stale EndOfMedia (audit §5.2)
// ---------------------------------------------------------------------------
// `EndOfMedia` is reported for two very different situations and the enum alone
// cannot tell them apart, so every decision here goes through
// core/video/PreviewEndOfMediaPolicy.h, which is covered by
// preview_end_of_media_policy_spec.
//
// Neither branch is ever allowed to end the main preview transport. That
// coupling was the root cause in
// docs/audit/PREVIEW_AUTO_PAUSE_INITIAL_DIAGNOSIS_ZH.md and it stays removed:
// the PV is subordinate visual media, and a natural end simply keeps its last
// frame while BGM / chart / timeline continue to the unified content endpoint.
void PreviewStageMediaHost::handleVideoEndOfMedia(bool wasPlaybackActive)
{
    using miacode::preview::video::classifyEndOfMedia;
    using miacode::preview::video::EndOfMediaClass;
    using miacode::preview::video::EndOfMediaFacts;
    using miacode::preview::video::endOfMediaClassName;
    using miacode::preview::video::endOfMediaShouldRecover;

    staleEndOfMediaResumePending_ = false;
    ++staleEndOfMediaResumeSerial_;

    EndOfMediaFacts facts;
#ifdef MIACODE_USE_QTAVPLAYER
    facts.durationSeconds = player_ != nullptr
        ? static_cast<double>(player_->duration()) / 1000.0
        : 0.0;
    // Decode progress, taken from the last frame the decoder actually produced.
    // NOT player_->position(): QAVPlayer::position() returns duration() outright
    // once mediaStatus() is EndOfMedia, so it reads "at the end" for a stale event
    // and a real one alike. (The handoff audit cites position_ms=121066 as evidence;
    // it is true by construction, and the pts below is the real evidence.)
    facts.decodedSeconds = lastFramePtsSeconds_;
#elif defined(HAVE_QT_MULTIMEDIA)
    facts.durationSeconds = player_ != nullptr
        ? static_cast<double>(player_->duration()) / 1000.0
        : 0.0;
    // The compat backend exposes no per-frame pts, and its position is likewise
    // duration-clamped at end-of-media, so this path classifies `natural` in
    // practice and keeps its existing behaviour. The line is still logged: the
    // numbers are what would reveal a stale event here if one ever showed up.
    facts.decodedSeconds = player_ != nullptr
        ? static_cast<double>(player_->position()) / 1000.0
        : -1.0;
#else
    facts.durationSeconds = 0.0;
    facts.decodedSeconds = -1.0;
#endif
    const double intervalMs = videoFrameIntervalAvgMs();
    facts.frameIntervalSeconds = intervalMs > 0.0 ? intervalMs / 1000.0 : 0.0;
    facts.expectedSeconds = qMax(0.0, observedPlayheadSecond_) + timelineOffsetSeconds_;

    const EndOfMediaClass classification = classifyEndOfMedia(facts);
    const bool recoverable = wasPlaybackActive
        && !shuttingDown_
        && mediaKind_ == MediaKind::Video
        && endOfMediaShouldRecover(facts);
    const bool budgetLeft = staleEndOfMediaRecoveries_ < kStaleEndOfMediaMaxRecoveries;

    appendPreviewStageMediaLog(
        QStringLiteral("end_of_media_classified"),
        QString("eom_class=%1 duration_ms=%2 decoded_ms=%3 expected_ms=%4 shortfall_ms=%5 "
                "frame_interval_ms=%6 frames=%7 was_active=%8 recoverable=%9 "
                "recoveries_used=%10 recovery_budget=%11 txn=%12 last_seek_ms=%13%14")
            .arg(QString::fromLatin1(endOfMediaClassName(classification)))
            .arg(qRound64(facts.durationSeconds * 1000.0))
            .arg(facts.decodedSeconds >= 0.0 ? qRound64(facts.decodedSeconds * 1000.0) : -1)
            .arg(qRound64(facts.expectedSeconds * 1000.0))
            .arg(facts.decodedSeconds >= 0.0
                     ? qRound64((facts.durationSeconds - facts.decodedSeconds) * 1000.0)
                     : -1)
            .arg(intervalMs, 0, 'f', 3)
            .arg(videoFrameCountTotal_)
            .arg(wasPlaybackActive ? 1 : 0)
            .arg(recoverable ? 1 : 0)
            .arg(staleEndOfMediaRecoveries_)
            .arg(kStaleEndOfMediaMaxRecoveries)
            .arg(playbackTransactionId_)
            .arg(lastSeekMs_)
            .arg(demuxEndOfFileDiagnosticFields()));

    if (classification != EndOfMediaClass::Stale || !recoverable) {
        return;
    }
    if (!budgetLeft) {
        appendPreviewStageMediaLog(
            QStringLiteral("stale_end_of_media_exhausted"),
            QString("recoveries=%1 limit=%2 expected_ms=%3 duration_ms=%4")
                .arg(staleEndOfMediaRecoveries_)
                .arg(kStaleEndOfMediaMaxRecoveries)
                .arg(qRound64(facts.expectedSeconds * 1000.0))
                .arg(qRound64(facts.durationSeconds * 1000.0)));
        return;
    }
    tryRecoverFromStaleEndOfMedia(qMax(0.0, observedPlayheadSecond_));
}

void PreviewStageMediaHost::resetStaleEndOfMediaRecovery()
{
    staleEndOfMediaRecoveries_ = 0;
    staleEndOfMediaResumePending_ = false;
    staleEndOfMediaResumeSecond_ = 0.0;
    ++staleEndOfMediaResumeSerial_;
}

bool PreviewStageMediaHost::tryRecoverFromStaleEndOfMedia(double targetSecond)
{
    if (shuttingDown_ || player_ == nullptr || mediaKind_ != MediaKind::Video
        || mediaPath_.isEmpty()) {
        return false;
    }
    if (staleEndOfMediaRecoveries_ >= kStaleEndOfMediaMaxRecoveries) {
        return false;
    }
    const double clampedSecond = qMax(0.0, qIsFinite(targetSecond) ? targetSecond : 0.0);
    const qint64 targetMs =
        qMax<qint64>(0, qRound64((clampedSecond + timelineOffsetSeconds_) * 1000.0));
    const int attempt = staleEndOfMediaRecoveries_++;
    const bool reload = attempt >= kStaleEndOfMediaSeekAttempts;

    appendPreviewStageMediaLog(
        QStringLiteral("stale_end_of_media_recover"),
        QString("attempt=%1 strategy=%2 target_second=%3 target_ms=%4 frames=%5 path=%6")
            .arg(attempt)
            .arg(reload ? QStringLiteral("reload") : QStringLiteral("seek_resume"))
            .arg(clampedSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(videoFrameCountTotal_)
            .arg(mediaPath_));

#ifdef MIACODE_USE_QTAVPLAYER
    if (reload) {
        // Re-opening runs QAVPlayer's terminate(), which clears the end-of-file
        // latch, so the play() below cannot be re-interpreted as "restart from the
        // beginning". Same in-place reload shape as the software-decode fallback.
        player_->stop();
        player_->setSource(QString());
        player_->setSource(mediaPath_);
        player_->setSpeed(static_cast<qreal>(playbackRate_));
        lastSeekMs_ = targetMs;
        player_->seek(targetMs);
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackPendingStart_ = false;
        videoPlaybackActiveElapsed_.restart();
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        updateClockDelta();
        emit diagnosticsChanged();
        return true;
    }

    // Seek first, resume on the acknowledgement. QAVPlayer::play() re-seeks to 0
    // whenever its end-of-file latch is still set, so playing before the seek lands
    // would restart the PV from the beginning — the very failure this recovers from.
    staleEndOfMediaResumePending_ = true;
    staleEndOfMediaResumeSecond_ = clampedSecond;
    const quint64 serial = ++staleEndOfMediaResumeSerial_;
    lastSeekMs_ = targetMs;
    player_->seek(targetMs);
    QTimer::singleShot(kStaleEndOfMediaResumeTimeoutMs, this, [this, serial, clampedSecond]() {
        if (serial != staleEndOfMediaResumeSerial_ || !staleEndOfMediaResumePending_) {
            return;
        }
        staleEndOfMediaResumePending_ = false;
        appendPreviewStageMediaLog(
            QStringLiteral("stale_end_of_media_resume_timeout"),
            QString("target_second=%1 recoveries=%2")
                .arg(clampedSecond, 0, 'f', 6)
                .arg(staleEndOfMediaRecoveries_));
        // Escalate: the seek never acknowledged, so the decoder needs re-opening.
        staleEndOfMediaRecoveries_ =
            qMax(staleEndOfMediaRecoveries_, kStaleEndOfMediaSeekAttempts);
        tryRecoverFromStaleEndOfMedia(clampedSecond);
    });
    return true;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(reload);
    Q_UNUSED(targetMs);
    Q_UNUSED(clampedSecond);
    return false;
#else
    Q_UNUSED(reload);
    Q_UNUSED(targetMs);
    // The compat backend already owns a delete-and-rebuild recovery with the same
    // resume-at-second contract; reuse it rather than growing a second one.
    return recoverVideoBackend(QStringLiteral("stale_end_of_media"), clampedSecond, true);
#endif
}
