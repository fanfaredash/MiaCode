#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/ProcessDiagnostics.h"
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

constexpr qint64 kVideoFrameStallMinMs = 120;
constexpr double kVideoFrameStallMultiplier = 3.5;
constexpr qint64 kPvMemoryPeriodicSampleMs = 5000;

double averageOrZero(double total, qint64 count)
{
    return count > 0 ? total / static_cast<double>(count) : 0.0;
}

double fpsFromAverageMs(double averageMs)
{
    return averageMs > 1e-6 ? 1000.0 / averageMs : 0.0;
}

quint64 counterDelta(quint64 current, quint64 baseline)
{
    // The QtAV counters are process-wide and monotonically increasing.  Be
    // defensive if a future backend changes that contract or a torn reset is
    // observed: a negative delta must never be printed as a giant unsigned value.
    return current >= baseline ? current - baseline : 0;
}

quint64 averageMicroseconds(quint64 totalUs, quint64 samples)
{
    return samples > 0 ? totalUs / samples : 0;
}

}  // namespace

miacode::preview::pv_memory::Observation PreviewStageMediaHost::pvMemoryObservation(
    bool includeProcess) const
{
    using namespace miacode::preview::pv_memory;
    Observation observation;
    observation.elapsedMs = pvMemoryElapsed_.isValid() ? pvMemoryElapsed_.elapsed() : 0;
    observation.media.mediaVisible = mediaVisible_;
    observation.media.playbackState = videoPlaybackActive_ ? QStringLiteral("playing")
                                                           : QStringLiteral("paused");
    observation.media.mediaStatus = mediaKind_ == MediaKind::Video
        ? QStringLiteral("video") : QStringLiteral("none");
    observation.media.positionMs = lastSeekMs_;
    observation.media.videoOutputAttached = videoOutputObject_ != nullptr;
    observation.media.videoSinkAttached = videoSink_ != nullptr;
    if (!includeProcess) {
        return observation;
    }
    const miacode::diag::CurrentProcessMemorySample sample =
        miacode::diag::currentProcessMemorySample();
    observation.process.residentBytes = sample.residentBytes;
    observation.process.footprintBytes = sample.physFootprintBytes;
    observation.process.internalBytes = sample.internalBytes;
    observation.process.compressedBytes = sample.compressedBytes;
    return observation;
}

void PreviewStageMediaHost::emitPvMemoryRecords(
    const QVector<miacode::preview::pv_memory::Record>& records)
{
    for (const auto& record : records) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::PvMemory,
            QStringLiteral("preview/pv_memory"),
            miacode::preview::pv_memory::Diagnostics::formatPayload(record));
    }
}

void PreviewStageMediaHost::beginPvMemorySource()
{
    if (!miacode::debug_options::debugModeEnabled() || mediaKind_ != MediaKind::Video) {
        return;
    }
    ++pvMemoryPeriodicTimerEpoch_;
    pvMemoryPeriodicTimerArmed_ = false;
    emitPvMemoryRecords(pvMemoryDiagnostics_.beginSource(
        miacode::preview::pv_memory::MediaKind::Video, pvMemoryObservation(true)));
    schedulePvMemoryPeriodicSample();
}

void PreviewStageMediaHost::observePvMemoryFrame(
    const QVideoFrame& frame,
    const miacode::preview::pv_memory::ImageConversionFact& conversion)
{
    if (!miacode::debug_options::debugModeEnabled()
        || !pvMemoryDiagnostics_.hasCurrentSource()) {
        return;
    }
    miacode::preview::pv_memory::FrameMetadata metadata;
    metadata.width = frame.size().width();
    metadata.height = frame.size().height();
    metadata.pixelFormat = QVideoFrameFormat::pixelFormatToString(
        frame.surfaceFormat().pixelFormat());
    emitPvMemoryRecords(pvMemoryDiagnostics_.observeFrame(
        pvMemoryObservation(false), metadata, conversion));
}

void PreviewStageMediaHost::recordPvMemoryBoundary(PvMemoryBoundary reason)
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    const auto record = pvMemoryDiagnostics_.boundary(reason, pvMemoryObservation(true));
    if (record) {
        emitPvMemoryRecords({*record});
    }
}

void PreviewStageMediaHost::clearPvMemorySource()
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    const QVector<miacode::preview::pv_memory::Record> records =
        pvMemoryDiagnostics_.clear(pvMemoryObservation(true));
    pvMemoryPeriodicTimerArmed_ = false;
    ++pvMemoryPeriodicTimerEpoch_;
    emitPvMemoryRecords(records);
    for (const auto& record : records) {
        if (record.reason != QStringLiteral("clear_after")) {
            continue;
        }
        const quint64 clearEpoch = record.clearEpoch;
        QTimer::singleShot(miacode::preview::pv_memory::kPostClear3SecondsMs, this,
            [this, clearEpoch]() {
                postClearPvMemoryCheckpoint(
                    clearEpoch, miacode::preview::pv_memory::kPostClear3SecondsMs);
            });
        QTimer::singleShot(miacode::preview::pv_memory::kPostClear15SecondsMs, this,
            [this, clearEpoch]() {
                postClearPvMemoryCheckpoint(
                    clearEpoch, miacode::preview::pv_memory::kPostClear15SecondsMs);
            });
        break;
    }
}

void PreviewStageMediaHost::latePvMemoryNoMedia()
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    emitPvMemoryRecords(pvMemoryDiagnostics_.lateNoMedia(pvMemoryObservation(true).process));
}

void PreviewStageMediaHost::postClearPvMemoryCheckpoint(quint64 clearEpoch, qint64 delayMs)
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    emitPvMemoryRecords(pvMemoryDiagnostics_.postClearCheckpoint(
        clearEpoch, delayMs, pvMemoryObservation(true).process));
}

void PreviewStageMediaHost::schedulePvMemoryPeriodicSample()
{
    if (!miacode::debug_options::debugModeEnabled()
        || !pvMemoryDiagnostics_.hasCurrentSource()
        || pvMemoryPeriodicTimerArmed_) {
        return;
    }
    pvMemoryPeriodicTimerArmed_ = true;
    const quint64 timerEpoch = pvMemoryPeriodicTimerEpoch_;
    QTimer::singleShot(kPvMemoryPeriodicSampleMs, this, [this, timerEpoch]() {
        if (timerEpoch != pvMemoryPeriodicTimerEpoch_) {
            return;
        }
        pvMemoryPeriodicTimerArmed_ = false;
        if (!miacode::debug_options::debugModeEnabled()
            || !pvMemoryDiagnostics_.hasCurrentSource()) {
            return;
        }
        const auto record = pvMemoryDiagnostics_.sample(pvMemoryObservation(true));
        if (record) {
            emitPvMemoryRecords({*record});
        }
        schedulePvMemoryPeriodicSample();
    });
}

void PreviewStageMediaHost::destroyPvMemorySource()
{
    pvMemoryPeriodicTimerArmed_ = false;
    ++pvMemoryPeriodicTimerEpoch_;
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    const auto record = pvMemoryDiagnostics_.destroy(pvMemoryObservation(true));
    if (record) {
        emitPvMemoryRecords({*record});
    }
}

bool PreviewStageMediaHost::hasVideoFrame() const
{
    return mediaKind_ == MediaKind::Video && videoFrameElapsed_.isValid();
}


double PreviewStageMediaHost::clockDeltaSeconds() const
{
    return clockDeltaSeconds_;
}


qint64 PreviewStageMediaHost::videoFrameAgeMs() const
{
    if (!hasVideoMedia()) {
        return -1;
    }
    return currentVideoFrameAgeForDiagnosticsMs();
}


qint64 PreviewStageMediaHost::videoFrameCountTotal() const
{
    return videoFrameCountTotal_;
}


double PreviewStageMediaHost::videoFrameRateEstimate() const
{
    return fpsFromAverageMs(videoFrameIntervalAvgMs());
}


double PreviewStageMediaHost::videoFrameIntervalAvgMs() const
{
    return averageOrZero(videoFrameIntervalSumMs_, videoFrameIntervalSampleCount_);
}


double PreviewStageMediaHost::videoFrameIntervalMaxMs() const
{
    return videoFrameIntervalMaxMs_;
}


qint64 PreviewStageMediaHost::videoFrameStallCount() const
{
    return videoFrameStallCount_;
}


bool PreviewStageMediaHost::videoFrameStalled() const
{
    return videoFrameStalled_;
}


void PreviewStageMediaHost::setVideoFrameToImageMaxFps(double fps)
{
    const double normalized = qBound(1.0, qIsFinite(fps) ? fps : 30.0, 1000.0);
    if (qAbs(videoFrameToImageMaxFps_ - normalized) <= 0.001) {
        return;
    }
    videoFrameToImageMaxFps_ = normalized;
    videoFrameToImageThrottle_.invalidate();
}


void PreviewStageMediaHost::setObservedPlayheadSecond(double second)
{
    observedPlayheadSecond_ = qMax(0.0, second);
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit diagnosticsChanged();
}


void PreviewStageMediaHost::updateClockDelta()
{
    if (!hasVideoMedia()) {
        clockDeltaSeconds_ = 0.0;
        return;
    }
    clockDeltaSeconds_ = currentPlaybackSecond() - observedPlayheadSecond_;
}


void PreviewStageMediaHost::noteVideoFrameArrived(const QVideoFrame& frame, quint64 sourceGeneration)
{
    MC_OP("PreviewStageMediaHost::noteVideoFrameArrived");
// QMediaPlayer sink-observe handler. The QtAVPlayer path pushes frames itself
// (handleDecodedVideoFrame) and never connects a sink observer, so this is a
// no-op there.
#if !defined(HAVE_QT_MULTIMEDIA) || defined(MIACODE_USE_QTAVPLAYER)
    Q_UNUSED(frame);
    Q_UNUSED(sourceGeneration);
#else
    const bool syncFrameBeacon = syncVideoFrameBeaconBudget_ > 0;
    if (syncFrameBeacon) {
        char buf[300];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/arrived_enter tid=%lu valid=%d source_gen=%llu current_gen=%llu count=%lld start_us=%lld end_us=%lld active=%d rate=%.3f",
            currentBeaconTid(),
            frame.isValid() ? 1 : 0,
            static_cast<unsigned long long>(sourceGeneration),
            static_cast<unsigned long long>(videoSourceGeneration_),
            static_cast<long long>(videoFrameCountTotal_),
            static_cast<long long>(frame.startTime()),
            static_cast<long long>(frame.endTime()),
            videoPlaybackActive_ ? 1 : 0,
            playbackRate_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (mediaKind_ != MediaKind::Video || sourceGeneration != videoSourceGeneration_) {
        if (syncFrameBeacon) {
            --syncVideoFrameBeaconBudget_;
            miacode::oplog::appendStartupBeaconLine("preview/frame/arrived_drop_stale");
        }
        appendPreviewStageMediaLog(
            QStringLiteral("video_frame_drop"),
            QString("reason=stale_source source_generation=%1 current_generation=%2 kind=%3")
                .arg(sourceGeneration)
                .arg(videoSourceGeneration_)
                .arg(debugMediaTypeName())
        );
        return;
    }
    if (!frame.isValid()) {
        if (syncFrameBeacon) {
            --syncVideoFrameBeaconBudget_;
            miacode::oplog::appendStartupBeaconLine("preview/frame/arrived_drop_invalid");
        }
        return;
    }
    // Phase 4c-9 — convert the QVideoFrame to a QImage and stash it in
    // loadedBackgroundImage_, exposed via currentBackgroundImage().
    // QVideoFrame::toImage() is GUI-thread safe (this slot runs on
    // GUI via the queued `videoFrameChanged` connection).
    //
    // Cost-mitigation gates (added because user reported the HUD's
    // stutter counter climbing to ~20 with video bg vs <5 with image):
    //   1. Skip when not visible — `mediaVisible_` gates the user's
    //      "Hide PV/BG while paused" setting; no point converting if
    //      the user has chosen to hide the bg.
    //   2. Throttle to ~30Hz max (33ms gap). For 30fps source video
    //      this means we capture roughly every other emission on
    //      avg; the rendered bg updates at ~15Hz, which is still
    //      visually fluid for a background and halves GUI thread
    //      cost. We always capture the FIRST frame after a
    //      visibility transition / chart switch (throttle not yet
    //      armed), so user actions don't get a stale frame.
    const qint64 videoFrameToImageThrottleNs =
        qMax<qint64>(1, qRound64(1000000000.0 / qMax(1.0, videoFrameToImageMaxFps_)));
    const bool throttledOut =
        videoFrameToImageThrottle_.isValid()
        && videoFrameToImageThrottle_.nsecsElapsed() < videoFrameToImageThrottleNs;
    miacode::preview::pv_memory::ImageConversionFact conversion;
    if (mediaVisible_ && !throttledOut) {
        if (syncFrameBeacon) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/frame/before_to_image tid=%lu count=%lld pixel_format=%d",
                currentBeaconTid(),
                static_cast<long long>(videoFrameCountTotal_),
                static_cast<int>(frame.surfaceFormat().pixelFormat()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        QElapsedTimer toImageTimer;
        toImageTimer.start();
        QImage decodedImage = frame.toImage();
        conversion.attempted = true;
        conversion.elapsedMs = toImageTimer.elapsed();
        conversion.succeeded = !decodedImage.isNull();
        conversion.resultBytes = conversion.succeeded ? decodedImage.sizeInBytes() : -1;
        if (syncFrameBeacon) {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "preview/frame/after_to_image tid=%lu null=%d size=%dx%d",
                currentBeaconTid(),
                decodedImage.isNull() ? 1 : 0,
                decodedImage.width(),
                decodedImage.height());
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        if (!decodedImage.isNull()) {
            loadedBackgroundImage_ = std::move(decodedImage);
            videoFrameToImageThrottle_.restart();
        }
    } else if (syncFrameBeacon) {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/skip_to_image tid=%lu visible=%d throttled=%d",
            currentBeaconTid(),
            mediaVisible_ ? 1 : 0,
            throttledOut ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    observePvMemoryFrame(frame, conversion);
    const bool firstFrameForSource = videoFrameCountTotal_ == 0;
    if (videoFrameElapsed_.isValid()) {
        const double intervalMs = static_cast<double>(videoFrameElapsed_.nsecsElapsed()) / 1000000.0;
        videoFrameIntervalSumMs_ += intervalMs;
        videoFrameIntervalMaxMs_ = qMax(videoFrameIntervalMaxMs_, intervalMs);
        videoFrameIntervalSampleCount_ += 1;
        if (!videoFrameIntervalsMs_.isEmpty()) {
            videoFrameIntervalsMs_[videoFrameIntervalWriteIndex_] = intervalMs;
            videoFrameIntervalWriteIndex_ = (videoFrameIntervalWriteIndex_ + 1) % videoFrameIntervalsMs_.size();
            videoFrameIntervalCount_ = qMin(videoFrameIntervalCount_ + 1, videoFrameIntervalsMs_.size());
        }
    }
    videoFrameElapsed_.restart();
    ++videoFrameCountTotal_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
    if (firstFrameForSource) {
        // Frame size + surface format diagnostics are the missing piece for
        // the user-machine PV-scaling bug: mediaTargetRect / VideoOutput
        // fillMode use the source's reported pixel aspect, and that aspect
        // can flip portrait↔landscape between QtMultimedia backends when a
        // phone-captured PV carries rotation metadata (FFmpeg applies the
        // rotation, native backends often don't). frame.size() shows the
        // post-rotation (or raw) pixel size; surfaceFormat.frameSize() and
        // viewport() expose the underlying buffer + cropped region; the
        // rotation/mirrored flags expose what the backend reports the
        // frame still needs.
        const QVideoFrameFormat fmt = frame.surfaceFormat();
        const QSize frameSize = frame.size();
        const QSize fmtFrameSize = fmt.frameSize();
        const QRect viewport = fmt.viewport();
        const auto rotationDegrees = [](QtVideo::Rotation rotation) -> int {
            switch (rotation) {
            case QtVideo::Rotation::None: return 0;
            case QtVideo::Rotation::Clockwise90: return 90;
            case QtVideo::Rotation::Clockwise180: return 180;
            case QtVideo::Rotation::Clockwise270: return 270;
            }
            return -1;
        };
        appendPreviewStageMediaLog(
            QStringLiteral("video_frame_first"),
            QString(
                "frame_ms=%1 frame_end_ms=%2 player_position_ms=%3 target_seek_ms=%4 "
                "playback_active=%5 frame_size=%6x%7 fmt_size=%8x%9 "
                "viewport=%10,%11+%12x%13 pixel_format=%14 rotation_deg=%15 "
                "mirrored=%16"
            )
                .arg(frame.startTime() >= 0 ? frame.startTime() / 1000 : -1)
                .arg(frame.endTime() >= 0 ? frame.endTime() / 1000 : -1)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(lastSeekMs_)
                .arg(videoPlaybackActive_ ? 1 : 0)
                .arg(frameSize.width()).arg(frameSize.height())
                .arg(fmtFrameSize.width()).arg(fmtFrameSize.height())
                .arg(viewport.x()).arg(viewport.y())
                .arg(viewport.width()).arg(viewport.height())
                .arg(QVideoFrameFormat::pixelFormatToString(fmt.pixelFormat()))
                .arg(rotationDegrees(frame.rotation()))
                .arg(frame.mirrored() ? 1 : 0)
        );
    }
    if (videoPlaybackActive_ && !videoPlaybackActiveElapsed_.isValid()) {
        videoPlaybackActiveElapsed_.restart();
    }
    if (pausedSeekCompletionPending_ && pausedSeekTargetMs_ >= 0) {
        qint64 candidatePositionMs = -1;
        const qint64 frameStartUs = frame.startTime();
        const qint64 frameEndUs = frame.endTime();
        if (frameStartUs >= 0) {
            candidatePositionMs = frameStartUs / 1000;
        } else if (player_ != nullptr) {
            candidatePositionMs = player_->position();
        }
        const bool frameCoversTarget =
            frameStartUs >= 0
            && frameEndUs >= 0
            && (frameStartUs / 1000) <= pausedSeekTargetMs_
            && pausedSeekTargetMs_ <= (frameEndUs / 1000);
        const bool closeEnough =
            candidatePositionMs >= 0
            && qAbs(candidatePositionMs - pausedSeekTargetMs_) <= kPausedSeekAckToleranceMs;
        if (frameCoversTarget || closeEnough) {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 target_ms=%3 frame_ms=%4 frame_end_ms=%5 source=frame")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetSecond_, 0, 'f', 6)
                    .arg(pausedSeekTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
            pausedSeekCompletionPending_ = false;
            emit pausedSeekCompleted(pausedSeekTargetSecond_, pausedSeekGeneration_);
        } else {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_wait_frame"),
                QString("generation=%1 target_ms=%2 frame_ms=%3 frame_end_ms=%4")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
        }
    }
    if (preparedPlaybackPending_ && preparedPlaybackTargetMs_ >= 0) {
        qint64 candidatePositionMs = -1;
        const qint64 frameStartUs = frame.startTime();
        const qint64 frameEndUs = frame.endTime();
        if (frameStartUs >= 0) {
            candidatePositionMs = frameStartUs / 1000;
        } else if (player_ != nullptr) {
            candidatePositionMs = player_->position();
        }
        const bool frameCoversTarget =
            frameStartUs >= 0
            && frameEndUs >= 0
            && (frameStartUs / 1000) <= preparedPlaybackTargetMs_
            && preparedPlaybackTargetMs_ <= (frameEndUs / 1000);
        const bool closeEnough =
            candidatePositionMs >= 0
            && qAbs(candidatePositionMs - preparedPlaybackTargetMs_) <= kPausedSeekAckToleranceMs;
        if (frameCoversTarget || closeEnough) {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_ready"),
                QString("txn=%1 second=%2 target_ms=%3 frame_ms=%4 frame_end_ms=%5 source=frame")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetSecond_, 0, 'f', 6)
                    .arg(preparedPlaybackTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
            preparedPlaybackPending_ = false;
            preparedPlaybackReady_ = true;
            preparedPlaybackLandingConfirmed_ = true;
            emit playbackStartPrepared(preparedPlaybackTargetSecond_, preparedPlaybackTransaction_);
        } else {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_wait_frame"),
                QString("txn=%1 target_ms=%2 frame_ms=%3 frame_end_ms=%4")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetMs_)
                    .arg(candidatePositionMs)
                    .arg(frameEndUs >= 0 ? (frameEndUs / 1000) : -1)
            );
        }
    }
    updateVideoFrameStallState(true);
    if (syncFrameBeacon) {
        --syncVideoFrameBeaconBudget_;
        char buf[280];
        std::snprintf(buf, sizeof(buf),
            "preview/frame/arrived_exit tid=%lu count=%lld player_pos=%lld clock_delta=%.6f status=%d state=%d remaining=%d",
            currentBeaconTid(),
            static_cast<long long>(videoFrameCountTotal_),
            player_ != nullptr ? static_cast<long long>(player_->position()) : -1ll,
            clockDeltaSeconds_,
            player_ != nullptr ? static_cast<int>(player_->mediaStatus()) : -1,
            player_ != nullptr ? static_cast<int>(playerPlaybackState(player_)) : -1,
            syncVideoFrameBeaconBudget_);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    emit diagnosticsChanged();
#endif
}

#ifdef MIACODE_USE_QTAVPLAYER
void PreviewStageMediaHost::handleDecodedVideoFrame(const QVideoFrame& frame,
                                                    double ptsSeconds,
                                                    double durationSeconds,
                                                    quint64 sourceGeneration)
{
    MC_OP("PreviewStageMediaHost::handleDecodedVideoFrame");
    if (mediaKind_ != MediaKind::Video || sourceGeneration != videoSourceGeneration_) {
        // Stale frame from a previous source (in-flight when the chart switched).
        return;
    }
    if (!frame.isValid()) {
        return;
    }

    // Visible path: push the decoded frame into the QML VideoOutput's sink.
    // A D3D11VA hardware frame stays a zero-copy RhiTexture handle here.
    lastVideoFrame_ = frame;
    observePvMemoryFrame(frame, {});
    if (ptsSeconds >= 0.0) {
        lastFramePtsSeconds_ = ptsSeconds;
    }
    if (videoSink_ != nullptr) {
        videoSink_->setVideoFrame(frame);
    }
    // The inner-circle sink only has a consumer in InnerCircleFitOuterFill; in
    // every other scale mode its VideoOutput is invisible. Pushing there anyway
    // made each decoded frame a second consumer that pins a decode-pool surface
    // for as long as the sink holds it — on the D3D11VA two-device bridge that
    // is exactly the resource the decoder needs back.
    // refreshInnerVideoSinkForScaleMode() re-primes it with lastVideoFrame_ when
    // the mode turns on, so switching into mode 3 mid-playback still shows the
    // current frame immediately.
    if (innerVideoSinkActive() && innerVideoSink_ != videoSink_) {
        innerVideoSink_->setVideoFrame(frame);
    }

    // NOTE — deliberately NO per-frame QVideoFrame::toImage() here. Calling it
    // on a QtAVPlayer D3D11VA *hardware* frame from the GUI thread maps a
    // decoder-pool surface while the decode thread keeps recycling it (the
    // D3D11 device context isn't shared safely across threads) → use-after-free
    // crash, observed on Intel iGPU. The QML VideoOutput renders the pushed
    // frame directly on the GPU, so no CPU copy is needed.

    // Frame-rate / stall diagnostics (drives the HUD; backend-agnostic math).
    const bool firstFrameForSource = videoFrameCountTotal_ == 0;
    if (videoFrameElapsed_.isValid()) {
        const double intervalMs = static_cast<double>(videoFrameElapsed_.nsecsElapsed()) / 1000000.0;
        videoFrameIntervalSumMs_ += intervalMs;
        videoFrameIntervalMaxMs_ = qMax(videoFrameIntervalMaxMs_, intervalMs);
        videoFrameIntervalSampleCount_ += 1;
        if (!videoFrameIntervalsMs_.isEmpty()) {
            videoFrameIntervalsMs_[videoFrameIntervalWriteIndex_] = intervalMs;
            videoFrameIntervalWriteIndex_ = (videoFrameIntervalWriteIndex_ + 1) % videoFrameIntervalsMs_.size();
            videoFrameIntervalCount_ = qMin(videoFrameIntervalCount_ + 1, videoFrameIntervalsMs_.size());
        }
    }
    videoFrameElapsed_.restart();
    ++videoFrameCountTotal_;

#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
    // HW-decode diag: this is the first DISPLAYED frame after a seek (skipped catch-up
    // frames are not emitted as videoFrame). Latency since the demuxer seek landed +
    // the catch-up GOP-burst count classify S-SEEK (burst>0 & latency spike, steady
    // state clean) vs S-PACE (steady interval_max also high). One line per seek.
    if (seekLatencyPending_) {
        seekLatencyPending_ = false;
        const qint64 latencyMs = seekCatchupTimer_.isValid() ? seekCatchupTimer_.elapsed() : -1;
        const unsigned long long catchupSkips = qavTakePreviewCatchupSkipCount();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/seek_landing"),
            QString("landing_latency_ms=%1 catchup_skips=%2 first_pts_ms=%3 "
                    "interval_avg_ms=%4 interval_max_ms=%5")
                .arg(latencyMs)
                .arg(catchupSkips)
                .arg(ptsSeconds >= 0.0 ? qRound64(ptsSeconds * 1000.0) : -1)
                .arg(videoFrameIntervalAvgMs(), 0, 'f', 1)
                .arg(videoFrameIntervalMaxMs(), 0, 'f', 1));
    }
#endif

    if (firstFrameForSource) {
        // Frame size + rotation diagnostics for the PV-scaling bug: phone PVs
        // carrying rotation metadata can flip portrait<->landscape. FFmpeg
        // applies rotation consistently now (no backend-dependent variance).
        const QVideoFrameFormat fmt = frame.surfaceFormat();
        const QSize frameSize = frame.size();
        const auto rotationDegrees = [](QtVideo::Rotation rotation) -> int {
            switch (rotation) {
            case QtVideo::Rotation::None: return 0;
            case QtVideo::Rotation::Clockwise90: return 90;
            case QtVideo::Rotation::Clockwise180: return 180;
            case QtVideo::Rotation::Clockwise270: return 270;
            }
            return -1;
        };
        appendPreviewStageMediaLog(
            QStringLiteral("video_frame_first"),
            QString("pts_ms=%1 player_position_ms=%2 target_seek_ms=%3 playback_active=%4 "
                    "frame_size=%5x%6 pixel_format=%7 rotation_deg=%8 mirrored=%9 hw_texture=%10")
                .arg(ptsSeconds >= 0.0 ? qRound64(ptsSeconds * 1000.0) : -1)
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(lastSeekMs_)
                .arg(videoPlaybackActive_ ? 1 : 0)
                .arg(frameSize.width()).arg(frameSize.height())
                .arg(QVideoFrameFormat::pixelFormatToString(fmt.pixelFormat()))
                .arg(rotationDegrees(frame.rotation()))
                .arg(frame.mirrored() ? 1 : 0)
                .arg(frame.handleType() == QVideoFrame::RhiTextureHandle ? 1 : 0));
    }
    if (videoPlaybackActive_ && !videoPlaybackActiveElapsed_.isValid()) {
        videoPlaybackActiveElapsed_.restart();
    }

    // Position + paused-seek/prepared-start handshake settling, keyed on the
    // frame's media-time pts (QtAVPlayer doesn't tag the QVideoFrame's
    // start/end like QMediaPlayer did).
    if (ptsSeconds >= 0.0) {
        lastTimelineSecond_ = qMax(0.0, ptsSeconds - timelineOffsetSeconds_);
        const double endSeconds = ptsSeconds + (durationSeconds > 0.0 ? durationSeconds : 0.0);
        settlePendingSeekAcks(ptsSeconds, endSeconds);
    }
    updateClockDelta();
    updateVideoFrameStallState(true);
    emit playbackPositionChanged(lastTimelineSecond_);
    emit diagnosticsChanged();
}
#endif  // MIACODE_USE_QTAVPLAYER

#ifdef MIACODE_USE_QTAVPLAYER
void PreviewStageMediaHost::emitHwDecodeDiagSummary(const char* reason)
{
#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
    // Form-A cumulative counter summary, drained on the GUI thread at low frequency
    // (seek / end-of-media) — never per frame on the render/decode threads, which only
    // do relaxed-atomic increments. Localizes the bug class without a per-frame log:
    // coded!=display => H-CROP candidate; copy_fail/acq_timeout spikes => bridge drops;
    // res_changes correlate format churn with artifact onset. Also flushes any pending
    // D3D11 debug-layer (typed-SRV/NV12) messages from the H2 shared device.
    QAVPreviewDiagCounters c{};
    qavGetPreviewDiagCounters(&c);
    if (c.copiedFramesSingle == 0 && c.copiedFramesTwoDevice == 0 && c.copyFailures == 0) {
        return;  // nothing decoded yet — skip stale-zero lines
    }
    miacode::preview::drainSharedPreviewD3D11DebugMessages();
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        // Keep the pre-existing seek/end summary compact unless the caller
        // explicitly asked for frame-pacing diagnostics.  In particular, do
        // not expose zeroed timing columns when their per-frame measurement is
        // disabled.
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/hwdecode_summary"),
            QString("reason=%1 path=%2 copied_single=%3 copied_two=%4 tex_created=%5 "
                    "acq_timeout=%6 copy_fail=%7 res_changes=%8 dumped=%9 "
                    "completion_waits=%10 completion_wait_timeouts=%11 frames_decode_error=%12 "
                    "corrupt_dropped=%13 codec=%14 coded=%15x%16 disp=%17x%18 fmt=0x%19")
                .arg(QString::fromLatin1(reason))
                .arg(c.lastPath == 1 ? QStringLiteral("single")
                                     : c.lastPath == 0 ? QStringLiteral("two-device")
                                                       : QStringLiteral("none"))
                .arg(c.copiedFramesSingle)
                .arg(c.copiedFramesTwoDevice)
                .arg(c.texturesCreated)
                .arg(c.acquireSyncTimeouts)
                .arg(c.copyFailures)
                .arg(c.resolutionChanges)
                .arg(c.hwFramesDumped)
                .arg(c.completionWaits)
                .arg(c.completionWaitTimeouts)
                .arg(c.framesDecodeError)
                .arg(c.corruptFramesDropped)
                .arg(c.codecName != nullptr ? QString::fromLatin1(c.codecName) : QStringLiteral("?"))
                .arg(c.lastCodedWidth).arg(c.lastCodedHeight)
                .arg(c.lastDisplayWidth).arg(c.lastDisplayHeight)
                .arg(c.lastDxgiFormat, 0, 16));
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/hwdecode_summary"),
        QString("reason=%1 path=%2 copied_single=%3 copied_two=%4 tex_created=%5 "
                "acq_timeout=%6 src_acq_n=%7 src_acq_avg_us=%8 src_acq_max_us=%9 "
                "dest_acq_n=%10 dest_acq_avg_us=%11 dest_acq_max_us=%12 "
                "bridge_n=%13 bridge_avg_us=%14 bridge_max_us=%15 "
                "src_setup_n=%16 src_setup_avg_us=%17 src_setup_max_us=%18 "
                "dest_setup_n=%19 dest_setup_avg_us=%20 dest_setup_max_us=%21 "
                "src_tex_create_n=%22 src_tex_create_avg_us=%23 src_tex_create_max_us=%24 "
                "dest_tex_create_n=%25 dest_tex_create_avg_us=%26 dest_tex_create_max_us=%27 "
                "shared_open_n=%28 shared_open_avg_us=%29 shared_open_max_us=%30 "
                "copy_fail=%31 res_changes=%32 dumped=%33 completion_waits=%34 "
                "completion_wait_timeouts=%35 frames_decode_error=%36 corrupt_dropped=%37 "
                "codec=%38 coded=%39x%40 disp=%41x%42 fmt=0x%43")
            .arg(QString::fromLatin1(reason))
            .arg(c.lastPath == 1 ? QStringLiteral("single")
                                 : c.lastPath == 0 ? QStringLiteral("two-device")
                                                   : QStringLiteral("none"))
            .arg(c.copiedFramesSingle)
            .arg(c.copiedFramesTwoDevice)
            .arg(c.texturesCreated)
            .arg(c.acquireSyncTimeouts)
            .arg(c.srcAcquireWaitSamples)
            .arg(c.srcAcquireWaitSamples > 0 ? c.srcAcquireWaitTotalUs / c.srcAcquireWaitSamples : 0)
            .arg(c.srcAcquireWaitMaxUs)
            .arg(c.destAcquireWaitSamples)
            .arg(c.destAcquireWaitSamples > 0 ? c.destAcquireWaitTotalUs / c.destAcquireWaitSamples : 0)
            .arg(c.destAcquireWaitMaxUs)
            .arg(c.twoDeviceBridgeSamples)
            .arg(c.twoDeviceBridgeSamples > 0 ? c.twoDeviceBridgeTotalUs / c.twoDeviceBridgeSamples : 0)
            .arg(c.twoDeviceBridgeMaxUs)
            .arg(c.twoDeviceSourceSetupSamples)
            .arg(c.twoDeviceSourceSetupSamples > 0
                ? c.twoDeviceSourceSetupTotalUs / c.twoDeviceSourceSetupSamples : 0)
            .arg(c.twoDeviceSourceSetupMaxUs)
            .arg(c.twoDeviceDestinationSetupSamples)
            .arg(c.twoDeviceDestinationSetupSamples > 0
                ? c.twoDeviceDestinationSetupTotalUs / c.twoDeviceDestinationSetupSamples : 0)
            .arg(c.twoDeviceDestinationSetupMaxUs)
            .arg(c.sourceTextureCreateSamples)
            .arg(c.sourceTextureCreateSamples > 0
                ? c.sourceTextureCreateTotalUs / c.sourceTextureCreateSamples : 0)
            .arg(c.sourceTextureCreateMaxUs)
            .arg(c.destinationTextureCreateSamples)
            .arg(c.destinationTextureCreateSamples > 0
                ? c.destinationTextureCreateTotalUs / c.destinationTextureCreateSamples : 0)
            .arg(c.destinationTextureCreateMaxUs)
            .arg(c.sharedResourceOpenSamples)
            .arg(c.sharedResourceOpenSamples > 0
                ? c.sharedResourceOpenTotalUs / c.sharedResourceOpenSamples : 0)
            .arg(c.sharedResourceOpenMaxUs)
            .arg(c.copyFailures)
            .arg(c.resolutionChanges)
            .arg(c.hwFramesDumped)
            .arg(c.completionWaits)
            .arg(c.completionWaitTimeouts)
            .arg(c.framesDecodeError)
            .arg(c.corruptFramesDropped)
            .arg(c.codecName != nullptr ? QString::fromLatin1(c.codecName) : QStringLiteral("?"))
            .arg(c.lastCodedWidth).arg(c.lastCodedHeight)
            .arg(c.lastDisplayWidth).arg(c.lastDisplayHeight)
            .arg(c.lastDxgiFormat, 0, 16));
#else
    Q_UNUSED(reason);
#endif
}
#endif  // MIACODE_USE_QTAVPLAYER

void PreviewStageMediaHost::beginFirstPlaybackRenderTrace()
{
    firstPlaybackBridgeTrace_ = {};
    firstPlaybackBridgeTraceElapsed_.invalidate();
#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
    if (!miacode::debug_options::runtimeDebugOutputEnabled()
        || !miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }

    QAVPreviewDiagCounters c{};
    qavGetPreviewDiagCounters(&c);
    firstPlaybackBridgeTrace_.armed = true;
    firstPlaybackBridgeTrace_.transactionId = playbackTransactionId_;
    firstPlaybackBridgeTrace_.copiedTwoDevice = c.copiedFramesTwoDevice;
    firstPlaybackBridgeTrace_.texturesCreated = c.texturesCreated;
    firstPlaybackBridgeTrace_.acquireTimeouts = c.acquireSyncTimeouts;
    firstPlaybackBridgeTrace_.copyFailures = c.copyFailures;
    firstPlaybackBridgeTrace_.bridgeSamples = c.twoDeviceBridgeSamples;
    firstPlaybackBridgeTrace_.bridgeTotalUs = c.twoDeviceBridgeTotalUs;
    firstPlaybackBridgeTrace_.sourceSetupSamples = c.twoDeviceSourceSetupSamples;
    firstPlaybackBridgeTrace_.sourceSetupTotalUs = c.twoDeviceSourceSetupTotalUs;
    firstPlaybackBridgeTrace_.destinationSetupSamples = c.twoDeviceDestinationSetupSamples;
    firstPlaybackBridgeTrace_.destinationSetupTotalUs = c.twoDeviceDestinationSetupTotalUs;
    firstPlaybackBridgeTrace_.sourceTextureCreateSamples = c.sourceTextureCreateSamples;
    firstPlaybackBridgeTrace_.sourceTextureCreateTotalUs = c.sourceTextureCreateTotalUs;
    firstPlaybackBridgeTrace_.destinationTextureCreateSamples = c.destinationTextureCreateSamples;
    firstPlaybackBridgeTrace_.destinationTextureCreateTotalUs = c.destinationTextureCreateTotalUs;
    firstPlaybackBridgeTrace_.sharedResourceOpenSamples = c.sharedResourceOpenSamples;
    firstPlaybackBridgeTrace_.sharedResourceOpenTotalUs = c.sharedResourceOpenTotalUs;
    firstPlaybackBridgeTrace_.sourceAcquireSamples = c.srcAcquireWaitSamples;
    firstPlaybackBridgeTrace_.sourceAcquireTotalUs = c.srcAcquireWaitTotalUs;
    firstPlaybackBridgeTrace_.destinationAcquireSamples = c.destAcquireWaitSamples;
    firstPlaybackBridgeTrace_.destinationAcquireTotalUs = c.destAcquireWaitTotalUs;
    firstPlaybackBridgeTraceElapsed_.start();

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/first_playback_bridge_trace"),
        QString("action=arm txn=%1 path=%2 baseline_copied_two=%3 baseline_textures=%4")
            .arg(playbackTransactionId_)
            .arg(c.lastPath == 1 ? QStringLiteral("single")
                                 : c.lastPath == 0 ? QStringLiteral("two-device")
                                                   : QStringLiteral("none"))
            .arg(c.copiedFramesTwoDevice)
            .arg(c.texturesCreated));
#endif
}

void PreviewStageMediaHost::noteFirstPlaybackRenderStall(quint64 transactionId,
                                                          qint64 presentWaitMs,
                                                          double visualSecond,
                                                          double audioSecond)
{
#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
    constexpr qint64 kFirstPlaybackTraceWindowMs = 10000;
    constexpr qint64 kMeaningfulRenderStallMs = 100;
    if (!miacode::debug_options::runtimeDebugOutputEnabled()
        || !miacode::debug_options::previewFramePacingDiagnosticsEnabled()
        || !firstPlaybackBridgeTrace_.armed
        || firstPlaybackBridgeTrace_.reported
        || firstPlaybackBridgeTrace_.reportPending
        || transactionId != firstPlaybackBridgeTrace_.transactionId
        || !firstPlaybackBridgeTraceElapsed_.isValid()
        || firstPlaybackBridgeTraceElapsed_.elapsed() > kFirstPlaybackTraceWindowMs
        || presentWaitMs < kMeaningfulRenderStallMs) {
        return;
    }

    // Bridge timings are committed by scope guards on the QSG render thread.
    // The GUI watchdog can run while that thread is still blocked, so capture
    // shortly after the watchdog rather than producing an unhelpful all-zero
    // snapshot from the middle of the stalled call.
    firstPlaybackBridgeTrace_.reportPending = true;
    firstPlaybackBridgeTrace_.presentWaitMs = presentWaitMs;
    firstPlaybackBridgeTrace_.visualSecond = visualSecond;
    firstPlaybackBridgeTrace_.audioSecond = audioSecond;
    QTimer::singleShot(250, this, [this]() {
        emitFirstPlaybackRenderTrace();
    });
#else
    Q_UNUSED(transactionId);
    Q_UNUSED(presentWaitMs);
    Q_UNUSED(visualSecond);
    Q_UNUSED(audioSecond);
#endif
}

void PreviewStageMediaHost::emitFirstPlaybackRenderTrace()
{
#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)
    constexpr qint64 kFirstPlaybackTraceWindowMs = 10000;
    if (!miacode::debug_options::runtimeDebugOutputEnabled()
        || !miacode::debug_options::previewFramePacingDiagnosticsEnabled()
        || !firstPlaybackBridgeTrace_.armed
        || firstPlaybackBridgeTrace_.reported
        || !firstPlaybackBridgeTrace_.reportPending
        || !firstPlaybackBridgeTraceElapsed_.isValid()
        || firstPlaybackBridgeTraceElapsed_.elapsed() > kFirstPlaybackTraceWindowMs) {
        return;
    }

    firstPlaybackBridgeTrace_.reportPending = false;
    firstPlaybackBridgeTrace_.reported = true;
    const quint64 transactionId = firstPlaybackBridgeTrace_.transactionId;
    const qint64 presentWaitMs = firstPlaybackBridgeTrace_.presentWaitMs;
    const double visualSecond = firstPlaybackBridgeTrace_.visualSecond;
    const double audioSecond = firstPlaybackBridgeTrace_.audioSecond;
    QAVPreviewDiagCounters c{};
    qavGetPreviewDiagCounters(&c);
    miacode::preview::drainSharedPreviewD3D11DebugMessages();

    const quint64 bridgeSamples = counterDelta(
        c.twoDeviceBridgeSamples, firstPlaybackBridgeTrace_.bridgeSamples);
    const quint64 bridgeTotalUs = counterDelta(
        c.twoDeviceBridgeTotalUs, firstPlaybackBridgeTrace_.bridgeTotalUs);
    const quint64 sourceSetupSamples = counterDelta(
        c.twoDeviceSourceSetupSamples, firstPlaybackBridgeTrace_.sourceSetupSamples);
    const quint64 sourceSetupTotalUs = counterDelta(
        c.twoDeviceSourceSetupTotalUs, firstPlaybackBridgeTrace_.sourceSetupTotalUs);
    const quint64 destinationSetupSamples = counterDelta(
        c.twoDeviceDestinationSetupSamples, firstPlaybackBridgeTrace_.destinationSetupSamples);
    const quint64 destinationSetupTotalUs = counterDelta(
        c.twoDeviceDestinationSetupTotalUs, firstPlaybackBridgeTrace_.destinationSetupTotalUs);
    const quint64 sourceTextureCreateSamples = counterDelta(
        c.sourceTextureCreateSamples, firstPlaybackBridgeTrace_.sourceTextureCreateSamples);
    const quint64 sourceTextureCreateTotalUs = counterDelta(
        c.sourceTextureCreateTotalUs, firstPlaybackBridgeTrace_.sourceTextureCreateTotalUs);
    const quint64 destinationTextureCreateSamples = counterDelta(
        c.destinationTextureCreateSamples, firstPlaybackBridgeTrace_.destinationTextureCreateSamples);
    const quint64 destinationTextureCreateTotalUs = counterDelta(
        c.destinationTextureCreateTotalUs, firstPlaybackBridgeTrace_.destinationTextureCreateTotalUs);
    const quint64 sharedResourceOpenSamples = counterDelta(
        c.sharedResourceOpenSamples, firstPlaybackBridgeTrace_.sharedResourceOpenSamples);
    const quint64 sharedResourceOpenTotalUs = counterDelta(
        c.sharedResourceOpenTotalUs, firstPlaybackBridgeTrace_.sharedResourceOpenTotalUs);
    const quint64 sourceAcquireSamples = counterDelta(
        c.srcAcquireWaitSamples, firstPlaybackBridgeTrace_.sourceAcquireSamples);
    const quint64 sourceAcquireTotalUs = counterDelta(
        c.srcAcquireWaitTotalUs, firstPlaybackBridgeTrace_.sourceAcquireTotalUs);
    const quint64 destinationAcquireSamples = counterDelta(
        c.destAcquireWaitSamples, firstPlaybackBridgeTrace_.destinationAcquireSamples);
    const quint64 destinationAcquireTotalUs = counterDelta(
        c.destAcquireWaitTotalUs, firstPlaybackBridgeTrace_.destinationAcquireTotalUs);

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/first_playback_bridge_trace"),
        QString("action=render_stall txn=%1 trace_age_ms=%2 present_wait_ms=%3 visual_second=%4 audio_second=%5 "
                "path=%6 copied_two_delta=%7 textures_delta=%8 acq_timeout_delta=%9 copy_fail_delta=%10 "
                "bridge_n=%11 bridge_avg_us=%12 bridge_max_process_us=%13 "
                "src_setup_n=%14 src_setup_avg_us=%15 src_setup_max_process_us=%16 "
                "dest_setup_n=%17 dest_setup_avg_us=%18 dest_setup_max_process_us=%19 "
                "src_tex_create_n=%20 src_tex_create_avg_us=%21 src_tex_create_max_process_us=%22 "
                "dest_tex_create_n=%23 dest_tex_create_avg_us=%24 dest_tex_create_max_process_us=%25 "
                "shared_open_n=%26 shared_open_avg_us=%27 shared_open_max_process_us=%28 "
                "src_acq_n=%29 src_acq_avg_us=%30 src_acq_max_process_us=%31 "
                "dest_acq_n=%32 dest_acq_avg_us=%33 dest_acq_max_process_us=%34")
            .arg(transactionId)
            .arg(firstPlaybackBridgeTraceElapsed_.elapsed())
            .arg(presentWaitMs)
            .arg(visualSecond, 0, 'f', 6)
            .arg(audioSecond, 0, 'f', 6)
            .arg(c.lastPath == 1 ? QStringLiteral("single")
                                 : c.lastPath == 0 ? QStringLiteral("two-device")
                                                   : QStringLiteral("none"))
            .arg(counterDelta(c.copiedFramesTwoDevice, firstPlaybackBridgeTrace_.copiedTwoDevice))
            .arg(counterDelta(c.texturesCreated, firstPlaybackBridgeTrace_.texturesCreated))
            .arg(counterDelta(c.acquireSyncTimeouts, firstPlaybackBridgeTrace_.acquireTimeouts))
            .arg(counterDelta(c.copyFailures, firstPlaybackBridgeTrace_.copyFailures))
            .arg(bridgeSamples)
            .arg(averageMicroseconds(bridgeTotalUs, bridgeSamples))
            .arg(c.twoDeviceBridgeMaxUs)
            .arg(sourceSetupSamples)
            .arg(averageMicroseconds(sourceSetupTotalUs, sourceSetupSamples))
            .arg(c.twoDeviceSourceSetupMaxUs)
            .arg(destinationSetupSamples)
            .arg(averageMicroseconds(destinationSetupTotalUs, destinationSetupSamples))
            .arg(c.twoDeviceDestinationSetupMaxUs)
            .arg(sourceTextureCreateSamples)
            .arg(averageMicroseconds(sourceTextureCreateTotalUs, sourceTextureCreateSamples))
            .arg(c.sourceTextureCreateMaxUs)
            .arg(destinationTextureCreateSamples)
            .arg(averageMicroseconds(destinationTextureCreateTotalUs, destinationTextureCreateSamples))
            .arg(c.destinationTextureCreateMaxUs)
            .arg(sharedResourceOpenSamples)
            .arg(averageMicroseconds(sharedResourceOpenTotalUs, sharedResourceOpenSamples))
            .arg(c.sharedResourceOpenMaxUs)
            .arg(sourceAcquireSamples)
            .arg(averageMicroseconds(sourceAcquireTotalUs, sourceAcquireSamples))
            .arg(c.srcAcquireWaitMaxUs)
            .arg(destinationAcquireSamples)
            .arg(averageMicroseconds(destinationAcquireTotalUs, destinationAcquireSamples))
            .arg(c.destAcquireWaitMaxUs));
#endif
}

void PreviewStageMediaHost::resetVideoFrameDiagnostics()
{
    videoFrameElapsed_.invalidate();
    videoPlaybackActiveElapsed_.invalidate();
    videoFrameIntervalsMs_.fill(0.0);
    videoFrameIntervalWriteIndex_ = 0;
    videoFrameIntervalCount_ = 0;
    videoFrameIntervalSumMs_ = 0.0;
    videoFrameIntervalMaxMs_ = 0.0;
    videoFrameIntervalSampleCount_ = 0;
    videoFrameCountTotal_ = 0;
    videoFrameStallCount_ = 0;
    videoFrameStalled_ = false;
    firstPlaybackBridgeTrace_ = {};
    firstPlaybackBridgeTraceElapsed_.invalidate();
    consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
}


bool PreviewStageMediaHost::updateVideoFrameStallState(bool logTransition)
{
    const qint64 ageMs = currentVideoFrameAgeForDiagnosticsMs();
    const bool stalled =
        hasVideoMedia()
        && videoPlaybackActive_
        && ageMs >= 0
        && ageMs >= videoFrameStallThresholdMs();
    if (videoFrameStalled_ == stalled) {
        return false;
    }
    videoFrameStalled_ = stalled;
    if (videoFrameStalled_) {
        videoFrameStallCount_ += 1;
    }
    if (!logTransition) {
        return true;
    }
    appendPreviewStageMediaLog(
        videoFrameStalled_ ? QStringLiteral("video_frame_stall_begin") : QStringLiteral("video_frame_stall_end"),
        QString("age_ms=%1 threshold_ms=%2 avg_interval_ms=%3 fps=%4 frame_count=%5 playback_second=%6 observed_second=%7 delta=%8")
            .arg(ageMs)
            .arg(videoFrameStallThresholdMs())
            .arg(videoFrameIntervalAvgMs(), 0, 'f', 3)
            .arg(videoFrameRateEstimate(), 0, 'f', 3)
            .arg(videoFrameCountTotal_)
            .arg(currentPlaybackSecond(), 0, 'f', 6)
            .arg(observedPlayheadSecond_, 0, 'f', 6)
            .arg(clockDeltaSeconds_, 0, 'f', 6)
    );
    if (videoFrameStalled_) {
        scheduleVideoPlaybackWatchdog(QStringLiteral("frame_stall"));
    }
    return true;
}


qint64 PreviewStageMediaHost::currentVideoFrameAgeForDiagnosticsMs() const
{
    if (videoFrameElapsed_.isValid()) {
        return videoFrameElapsed_.elapsed();
    }
    if (videoPlaybackActive_ && videoPlaybackActiveElapsed_.isValid()) {
        return videoPlaybackActiveElapsed_.elapsed();
    }
    return -1;
}


qint64 PreviewStageMediaHost::videoFrameStallThresholdMs() const
{
    const double baseIntervalMs = qMax(0.0, videoFrameIntervalAvgMs());
    const double scaledThresholdMs = qMax(
        static_cast<double>(kVideoFrameStallMinMs),
        qMax(33.0, baseIntervalMs) * kVideoFrameStallMultiplier
    );
    return qMax<qint64>(kVideoFrameStallMinMs, qRound64(scaledThresholdMs));
}
