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

constexpr int kVideoBackendRecoveryMaxConsecutive = 3;

// F1: detect whether the render GPU is integrated, so auto mode can default
// preview decode to software (D3D11VA is unreliable on iGPUs). Probes DXGI
// adapter 0 — the adapter driving the primary output, which Qt's D3D11 RHI uses
// by default. Integrated GPUs share system memory and report little/no
// DedicatedVideoMemory; discrete GPUs report >=1GB. One-time, cached. Probe
// failure or a discrete adapter => false (keep hardware). Caller logs the result.
bool detectIntegratedRenderAdapter(QString *descOut)
{
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, &adapter)))
        return false;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc)))
        return false;
    if (descOut)
        *descOut = QString::fromWCharArray(desc.Description);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        return false; // WARP / software renderer, not an iGPU
    const quint64 dedicatedMb =
        static_cast<quint64>(desc.DedicatedVideoMemory) / (1024ull * 1024ull);
    return dedicatedMb < 512; // < 512 MB dedicated VRAM => integrated
#else
    Q_UNUSED(descOut);
    return false;
#endif
}

bool isIntegratedRenderAdapter(QString *descOut = nullptr)
{
    static QString cachedDesc;
    static const bool cached = detectIntegratedRenderAdapter(&cachedDesc);
    if (descOut)
        *descOut = cachedDesc;
    return cached;
}

#ifdef MIACODE_USE_QTAVPLAYER
QString avMediaStatusName(QAVPlayer::MediaStatus status)
{
    switch (status) {
    case QAVPlayer::NoMedia:
        return QStringLiteral("NoMedia");
    case QAVPlayer::LoadedMedia:
        return QStringLiteral("LoadedMedia");
    case QAVPlayer::EndOfMedia:
        return QStringLiteral("EndOfMedia");
    case QAVPlayer::InvalidMedia:
        return QStringLiteral("InvalidMedia");
    default:
        return QStringLiteral("UnknownMediaStatus");
    }
}

QString avStateName(QAVPlayer::State state)
{
    switch (state) {
    case QAVPlayer::StoppedState:
        return QStringLiteral("StoppedState");
    case QAVPlayer::PlayingState:
        return QStringLiteral("PlayingState");
    case QAVPlayer::PausedState:
        return QStringLiteral("PausedState");
    default:
        return QStringLiteral("UnknownState");
    }
}
#endif

#if defined(HAVE_QT_MULTIMEDIA) && !defined(MIACODE_USE_QTAVPLAYER)
QString mediaStatusName(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::NoMedia:
        return QStringLiteral("NoMedia");
    case QMediaPlayer::LoadingMedia:
        return QStringLiteral("LoadingMedia");
    case QMediaPlayer::LoadedMedia:
        return QStringLiteral("LoadedMedia");
    case QMediaPlayer::StalledMedia:
        return QStringLiteral("StalledMedia");
    case QMediaPlayer::BufferingMedia:
        return QStringLiteral("BufferingMedia");
    case QMediaPlayer::BufferedMedia:
        return QStringLiteral("BufferedMedia");
    case QMediaPlayer::EndOfMedia:
        return QStringLiteral("EndOfMedia");
    case QMediaPlayer::InvalidMedia:
        return QStringLiteral("InvalidMedia");
    default:
        return QStringLiteral("UnknownMediaStatus");
    }
}

QString mediaErrorName(QMediaPlayer::Error error)
{
    switch (error) {
    case QMediaPlayer::NoError:
        return QStringLiteral("NoError");
    case QMediaPlayer::ResourceError:
        return QStringLiteral("ResourceError");
    case QMediaPlayer::FormatError:
        return QStringLiteral("FormatError");
    case QMediaPlayer::NetworkError:
        return QStringLiteral("NetworkError");
    case QMediaPlayer::AccessDeniedError:
        return QStringLiteral("AccessDeniedError");
    default:
        return QStringLiteral("UnknownError");
    }
}
#endif

}  // namespace

void PreviewStageMediaHost::initializeBackendObjects()
{
#ifdef MIACODE_USE_QTAVPLAYER
    if (player_ != nullptr) {
        return;
    }

    // QAVVideoFrame must be a registered metatype for the queued videoFrame
    // delivery below (QtAVPlayer decode thread -> host GUI thread).
    qRegisterMetaType<QAVVideoFrame>();

    player_ = new QAVPlayer(this);
    // Wire the HW-decode diagnostics (log sink + bounded readback-dump budget) into the
    // decode path before any media loads. Idempotent; covers both H2 and two-device paths.
    miacode::preview::installPreviewDecodeDiagnostics();
    player_->setSpeed(static_cast<qreal>(playbackRate_));
    // F1: decide hardware vs software preview decode.
    //   - softwareDecodeFallbackTried_: a prior D3D11VA decode failed -> retry in software.
    //   - MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO tri-state: Auto / ForceSoftware / ForceHardware.
    //   - Auto defaults INTEGRATED GPUs to software (D3D11VA preview decode is unreliable on
    //     Intel/Arc iGPUs: startup stutter, NV12 green padding, AV1 hardware corruption — all
    //     clean in software), while discrete GPUs keep hardware. ForceHardware overrides the
    //     iGPU auto-detect; ForceSoftware always software. See DebugOptions.h.
    // Decode mode: the persisted user preference (硬件渲染 / 软件渲染, default
    // HARDWARE) decides. MIACODE_PREVIEW_FORCE_SOFTWARE_VIDEO stays a dev override on
    // top (ForceSoftware / ForceHardware win; Auto / unset honors the user pref).
    // The legacy "Auto => integrated GPU silently defaults to software" behaviour is
    // gone: the default is hardware for everyone, and a user on an affected iGPU flips
    // the preference to software at runtime (hot-switch, no restart). The session-only
    // softwareDecodeFallbackTried_ latch still forces software after a hardware
    // InvalidMedia. adapterDesc/integratedGpu are kept for the diagnostic line only.
    using DecodePref = miacode::debug_options::PreviewVideoDecodePreference;
    const DecodePref decodePref = miacode::debug_options::previewVideoDecodePreference();
    QString adapterDesc;
    const bool integratedGpu = isIntegratedRenderAdapter(&adapterDesc);
    bool forceSoftware = videoDecodePreferSoftware_;
    switch (decodePref) {
    case DecodePref::ForceSoftware: forceSoftware = true; break;
    case DecodePref::ForceHardware: forceSoftware = false; break;
    case DecodePref::Auto:          break;  // env unset -> honor the user preference
    }
    const bool useSoftware = softwareDecodeFallbackTried_ || forceSoftware;
    if (useSoftware) {
        player_->setInputVideoCodec(QStringLiteral("software"));
    }
    const char *prefName = decodePref == DecodePref::ForceSoftware ? "force_sw"
                         : decodePref == DecodePref::ForceHardware ? "force_hw"
                                                                   : "auto";
    // NOTE (P0): `probe_adapter` is a HEURISTIC — the DXGI adapter-0 desc read
    // by detectIntegratedRenderAdapter() to guess iGPU vs dGPU for the decode
    // path. It is NOT the adapter Qt Quick's RHI actually bound; for that, see
    // the `quick_shell/device` runtime log (QSGRendererInterface DeviceResource).
    appendPreviewStageMediaLog(
        QStringLiteral("media_backend"),
        QString("backend=qtavplayer ffmpeg=1 qt_runtime_version=%1 force_software=%2 pref=%3 igpu=%4 probe_adapter=\"%5\" probe_adapter_source=dxgi_enum0_heuristic single_device=%6")
            .arg(QString::fromLatin1(qVersion()))
            .arg(useSoftware ? 1 : 0)
            .arg(QString::fromLatin1(prefName))
            .arg(integratedGpu ? 1 : 0)
            .arg(adapterDesc)
            .arg(miacode::preview::sharedPreviewD3D11DeviceActive() ? 1 : 0));

    // Seek landing (frame-accurate). Backs up the pts match in
    // handleDecodedVideoFrame for the paused-seek / prepared-start handshakes —
    // it covers low-fps sources where the decoded frame pts can sit a whole
    // frame interval before the requested target.
    seekedConnection_ = connect(player_, &QAVPlayer::seeked, this, [this](qint64 posMs) {
        if (mediaKind_ != MediaKind::Video) {
            return;
        }
        const double mediaSecond = static_cast<double>(posMs) / 1000.0;
        lastTimelineSecond_ = qMax(0.0, mediaSecond - timelineOffsetSeconds_);
        settlePendingSeekAcks(mediaSecond, mediaSecond);
        updateClockDelta();
#if defined(Q_OS_WIN)
        // HW-decode diag: the demuxer seek landed; the decoder now catches up to the
        // target GOP (skipFrame decode-but-don't-display bursts). Arm the bounded
        // readback so seek-time green/garble is captured, zero the catch-up counter,
        // and start the latency clock — read back at the first DISPLAYED frame below.
        qavArmPreviewHwFrameDump();
        qavTakePreviewCatchupSkipCount();  // discard pre-seek residual
        seekCatchupTimer_.restart();
        seekLatencyPending_ = true;
        emitHwDecodeDiagSummary("seek");
#endif
    });

    connect(player_, &QAVPlayer::mediaStatusChanged, this, [this](QAVPlayer::MediaStatus status) {
        appendPreviewStageMediaLog(
            QStringLiteral("media_status"),
            QString("status=%1 state=%2 position_ms=%3 kind=%4")
                .arg(avMediaStatusName(status))
                .arg(avStateName(player_ != nullptr ? player_->state() : QAVPlayer::StoppedState))
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(debugMediaTypeName()));
        if (status == QAVPlayer::LoadedMedia) {
            videoBackendLoaded_ = true;
            return;
        }
        if (status == QAVPlayer::EndOfMedia) {
            // A PV/BG video is subordinate visual media, not the preview
            // transport's lifetime owner. Leave lastVideoFrame_ in both video
            // sinks so the final decoded frame stays visible while the chart,
            // BGM and SFX continue to the unified content-duration endpoint.
            videoPlaybackActive_ = false;
            videoPlaybackPendingStart_ = false;
            videoPlaybackActiveElapsed_.invalidate();
            updateClockDelta();
            updateVideoFrameStallState(true);
            emitHwDecodeDiagSummary("eom");
            emit diagnosticsChanged();
            return;
        }
        if (status == QAVPlayer::InvalidMedia && mediaKind_ == MediaKind::Video) {
            maybeRetryWithSoftwareDecode();
        }
    });

    connect(player_, &QAVPlayer::errorOccurred, this, [this](QAVPlayer::Error error, const QString& errorString) {
        appendPreviewStageMediaLog(
            QStringLiteral("media_error"),
            QString("error=%1 text=\"%2\" kind=%3")
                .arg(static_cast<int>(error))
                .arg(errorString)
                .arg(debugMediaTypeName()));
    });
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    return;
#else
    if (player_ != nullptr) {
        return;
    }

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    audioOutput_->setMuted(true);
    audioOutput_->setVolume(0.0f);
    player_->setAudioOutput(audioOutput_);
    // G2 fix: setPlaybackRate is safe on a freshly constructed player
    // (no source attached yet → no demuxer/decoder worker running, so the
    // rate write can't race with the in-flight decode loop that fast-fails
    // at non-1.0 rates on Qt 6.8). This call is the load-bearing piece
    // of the new rate-change story: PreviewStageMediaHost::setPlaybackRate
    // forwards every rate != 1.0 / rate != oldRate change through
    // recoverVideoBackend(), which deletes this player and re-enters
    // initializeBackendObjects() with the desired rate already in
    // playbackRate_. The line below is where that desired rate lands.
    player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
    // One-shot diagnostic so the user-machine vs dev-machine PV-scaling
    // bug can be pinned down without a custom build. The QtMultimedia
    // backend (FFmpeg vs native WMF on Windows) is selected at runtime
    // based on QT_MEDIA_BACKEND + plugin availability + silent fallback;
    // it's never logged elsewhere, but the chosen backend can change how
    // a video frame's pixel size + rotation get reported (rotation
    // metadata in particular is applied by some backends and ignored by
    // others, which inverts the aspect of phone-captured PVs).
    appendPreviewStageMediaLog(
        QStringLiteral("media_backend"),
        QString("qt_runtime_version=%1 qt_media_backend_env=%2")
            .arg(QString::fromLatin1(qVersion()))
            .arg(qEnvironmentVariable(
                "QT_MEDIA_BACKEND",
                QStringLiteral("(unset:default)")))
    );
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        if (mediaKind_ != MediaKind::Video) {
            return;
        }
        lastTimelineSecond_ = qMax(0.0, static_cast<double>(positionMs) / 1000.0 - timelineOffsetSeconds_);
        updateClockDelta();
        if (pausedSeekCompletionPending_
            && pausedSeekTargetMs_ >= 0
            && videoSink_ == nullptr
            && qAbs(positionMs - pausedSeekTargetMs_) <= kPausedSeekAckToleranceMs) {
            appendPreviewStageMediaLog(
                QStringLiteral("paused_seek_media_ack"),
                QString("generation=%1 second=%2 position_ms=%3 target_ms=%4 source=position_fallback")
                    .arg(pausedSeekGeneration_)
                    .arg(pausedSeekTargetSecond_, 0, 'f', 6)
                    .arg(positionMs)
                    .arg(pausedSeekTargetMs_)
            );
            pausedSeekCompletionPending_ = false;
            emit pausedSeekCompleted(pausedSeekTargetSecond_, pausedSeekGeneration_);
        }
        if (preparedPlaybackPending_
            && preparedPlaybackTargetMs_ >= 0
            && videoSink_ == nullptr
            && qAbs(positionMs - preparedPlaybackTargetMs_) <= kPausedSeekAckToleranceMs) {
            appendPreviewStageMediaLog(
                QStringLiteral("prepare_playback_ready"),
                QString("txn=%1 second=%2 position_ms=%3 target_ms=%4 source=position_fallback")
                    .arg(preparedPlaybackTransaction_)
                    .arg(preparedPlaybackTargetSecond_, 0, 'f', 6)
                    .arg(positionMs)
                    .arg(preparedPlaybackTargetMs_)
            );
            preparedPlaybackPending_ = false;
            preparedPlaybackReady_ = true;
            emit playbackStartPrepared(preparedPlaybackTargetSecond_, preparedPlaybackTransaction_);
        }
        emit playbackPositionChanged(lastTimelineSecond_);
        emit diagnosticsChanged();
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (syncMediaStatusBeaconBudget_ > 0) {
            --syncMediaStatusBeaconBudget_;
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "preview/media/status_changed tid=%lu status=%d state=%d rate=%.3f pos=%lld pending_rate=%d kind=%d",
                currentBeaconTid(),
                static_cast<int>(status),
                static_cast<int>(playerPlaybackState(player_)),
                player_ != nullptr ? static_cast<double>(player_->playbackRate()) : -1.0,
                player_ != nullptr ? static_cast<long long>(player_->position()) : -1ll,
                pendingPlaybackRateApply_ ? 1 : 0,
                static_cast<int>(mediaKind_));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("media_status"),
            QString("status=%1 playback_state=%2 position_ms=%3 kind=%4 recovering=%5")
                .arg(mediaStatusName(status))
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(debugMediaTypeName())
                .arg(recoveringVideoBackend_ ? 1 : 0)
        );
        // G2 Commit 1: flush a deferred setPlaybackRate write the moment the
        // player lands in a stable state. The original setPlaybackRate call
        // returned without touching the player and stashed pendingPlaybackRateApply_;
        // here is where the cached playbackRate_ actually reaches QMediaPlayer.
        if (pendingPlaybackRateApply_ && player_ != nullptr) {
            const bool stableNow = status == QMediaPlayer::NoMedia
                                || status == QMediaPlayer::LoadedMedia
                                || status == QMediaPlayer::BufferedMedia
                                || status == QMediaPlayer::EndOfMedia;
            if (stableNow) {
                pendingPlaybackRateApply_ = false;
                // Reachable only for rate==1.0 deferrals: setPlaybackRate
                // routes any rate != 1.0 through recoverVideoBackend() now,
                // which rebuilds the player and applies the rate inside
                // initializeBackendObjects (pre-setSource, safe). A
                // pending 1.0 flush is harmless even if the player ended
                // up rebuilt in between, because rebuilds always apply
                // the latest cached playbackRate_ themselves.
                {
                    char buf[180];
                    std::snprintf(buf, sizeof(buf),
                        "preview/rate/qt_host_flush_about_to_setrate tid=%lu rate=%.3f status=%d",
                        currentBeaconTid(),
                        playbackRate_,
                        static_cast<int>(status));
                    miacode::oplog::appendStartupBeaconLine(buf);
                }
                player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
                {
                    char buf[180];
                    std::snprintf(buf, sizeof(buf),
                        "preview/rate/qt_host_flush_setrate_done tid=%lu rate=%.3f player_rate=%.3f",
                        currentBeaconTid(),
                        playbackRate_,
                        player_ != nullptr ? static_cast<double>(player_->playbackRate()) : -1.0);
                    miacode::oplog::appendStartupBeaconLine(buf);
                }
                appendPreviewStageMediaLog(
                    QStringLiteral("playback_rate_flushed"),
                    QString("rate=%1 status=%2 kind=%3")
                        .arg(playbackRate_, 0, 'f', 3)
                        .arg(mediaStatusName(status))
                        .arg(debugMediaTypeName()));
            }
        }
        if (status == QMediaPlayer::EndOfMedia) {
            // Match the QtAVPlayer path: keep the sink's current (last) frame
            // and end only this subordinate video stream. The main preview
            // transport is completed exclusively by the timeline/content
            // duration policy.
            videoPlaybackActive_ = false;
            videoPlaybackPendingStart_ = false;
            videoPlaybackActiveElapsed_.invalidate();
            ++videoPlaybackWatchdogSerial_;
            updateClockDelta();
            updateVideoFrameStallState(true);
            emit diagnosticsChanged();
            return;
        }
        if (status == QMediaPlayer::StalledMedia && mediaKind_ == MediaKind::Video && videoPlaybackActive_) {
            scheduleVideoPlaybackWatchdog(QStringLiteral("media_status_stalled"));
            return;
        }
        if (status != QMediaPlayer::InvalidMedia
            || mediaKind_ != MediaKind::Video
            || recoveringVideoBackend_) {
            return;
        }
        const double targetSecond = videoPlaybackActive_ ? observedPlayheadSecond_ : currentPlaybackSecond();
        QMetaObject::invokeMethod(
            this,
            [this, targetSecond]() {
                recoverVideoBackend(QStringLiteral("media_status_invalid"), targetSecond, videoPlaybackActive_);
            },
            Qt::QueuedConnection
        );
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString& errorString) {
        appendPreviewStageMediaLog(
            QStringLiteral("media_error"),
            QString("error=%1 text=\"%2\" playback_state=%3 position_ms=%4 kind=%5 recovering=%6")
                .arg(mediaErrorName(error))
                .arg(errorString)
                .arg(playbackStateName(playerPlaybackState(player_)))
                .arg(player_ != nullptr ? player_->position() : -1)
                .arg(debugMediaTypeName())
                .arg(recoveringVideoBackend_ ? 1 : 0)
        );
        if (error == QMediaPlayer::NoError
            || mediaKind_ != MediaKind::Video
            || recoveringVideoBackend_) {
            return;
        }
        const QString reason = QStringLiteral("media_error_%1").arg(mediaErrorName(error));
        const double targetSecond = videoPlaybackActive_ ? observedPlayheadSecond_ : currentPlaybackSecond();
        QMetaObject::invokeMethod(
            this,
            [this, reason, targetSecond]() {
                recoverVideoBackend(reason, targetSecond, videoPlaybackActive_);
            },
            Qt::QueuedConnection
        );
    });
#endif
}


void PreviewStageMediaHost::setPlaybackRate(double rate)
{
    MC_OP("PreviewStageMediaHost::setPlaybackRate");
    const double oldRate = playbackRate_;
    playbackRate_ = qMax(0.05, rate);
#ifdef MIACODE_USE_QTAVPLAYER
    Q_UNUSED(oldRate);
    // The whole reason for the migration: QAVPlayer::setSpeed applies the rate
    // inside QtAVPlayer's own decode loop. It does NOT rebuild a Qt converter
    // pipeline mid-flight, so it cannot race the QSG D3D11 texture
    // sampler — i.e. the "倍速闪退" crash class is structurally gone, and all
    // the deferred-apply / recover-rebuild scaffolding below is unnecessary.
    if (player_ != nullptr) {
        player_->setSpeed(static_cast<qreal>(playbackRate_));
    }
    const miacode::diagnostics::StageRateKey rateKey{
        playbackRate_, static_cast<int>(mediaKind_)};
    if (playbackRateLogGate_.shouldEmit(
            miacode::diagnostics::PlaybackRateLogKind::Ordinary, rateKey)) {
        appendPreviewStageMediaLog(
            QStringLiteral("playback_rate"),
            QString("rate=%1 kind=%2")
                .arg(playbackRate_, 0, 'f', 3)
                .arg(debugMediaTypeName()));
    }
#elif defined(HAVE_QT_MULTIMEDIA)
    if (mediaKind_ == MediaKind::Video && player_ != nullptr) {
        syncVideoFrameBeaconBudget_ = 24;
        syncMediaStatusBeaconBudget_ = 16;
    }
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_enter tid=%lu rate=%.3f old=%.3f player_null=%d kind=%d",
            currentBeaconTid(),
            playbackRate_,
            oldRate,
            player_ == nullptr ? 1 : 0,
            static_cast<int>(mediaKind_));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (player_ == nullptr) {
        // No player yet — playbackRate_ is the cached value that
        // initializeBackendObjects() will apply on a fresh player BEFORE
        // setSource. That ordering (rate-before-source) is verified safe
        // even at 0.5x / 1.5x (logs_24 startup line 134-161).
        return;
    }
    // Keep the media-status snapshot before applying a rate change so
    // diagnostics can still reconstruct whether the backend was loaded,
    // buffering, or stalled when the request arrived.
    const QMediaPlayer::MediaStatus status = player_->mediaStatus();
    const QMediaPlayer::PlaybackState playbackState = playerPlaybackState(player_);
    const qreal playerRateBefore = player_->playbackRate();
    const bool stable = status == QMediaPlayer::NoMedia
                     || status == QMediaPlayer::LoadedMedia
                     || status == QMediaPlayer::BufferedMedia
                     || status == QMediaPlayer::EndOfMedia;
    if (!stable) {
        pendingPlaybackRateApply_ = true;
        {
            char buf[240];
            std::snprintf(buf, sizeof(buf),
                "preview/rate/qt_host_deferred tid=%lu rate=%.3f status=%d state=%d player_rate=%.3f pos=%lld",
                currentBeaconTid(),
                playbackRate_,
                static_cast<int>(status),
                static_cast<int>(playbackState),
                static_cast<double>(playerRateBefore),
                static_cast<long long>(player_->position()));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        appendPreviewStageMediaLog(
            QStringLiteral("playback_rate_deferred"),
            QString("rate=%1 status=%2 kind=%3")
                .arg(playbackRate_, 0, 'f', 3)
                .arg(mediaStatusName(status))
                .arg(debugMediaTypeName()));
        return;
    }
    pendingPlaybackRateApply_ = false;
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_about_to_setrate tid=%lu rate=%.3f old=%.3f status=%d state=%d player_rate=%.3f pos=%lld",
            currentBeaconTid(),
            playbackRate_,
            oldRate,
            static_cast<int>(status),
            static_cast<int>(playbackState),
            static_cast<double>(playerRateBefore),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setPlaybackRate(static_cast<qreal>(playbackRate_));
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/rate/qt_host_setrate_done tid=%lu rate=%.3f player_rate=%.3f status=%d state=%d pos=%lld",
            currentBeaconTid(),
            playbackRate_,
            static_cast<double>(player_->playbackRate()),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)),
            static_cast<long long>(player_->position()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
}


bool PreviewStageMediaHost::recoverVideoBackend(const QString& reason, double targetSecond, bool resumePlayback)
{
// QMediaPlayer-only scaffolding: QtAVPlayer doesn't need a delete-and-rebuild
// recovery (no silent WMF fallback, no converter-rebuild crash). No-op there.
#if !defined(HAVE_QT_MULTIMEDIA) || defined(MIACODE_USE_QTAVPLAYER)
    Q_UNUSED(reason);
    Q_UNUSED(targetSecond);
    Q_UNUSED(resumePlayback);
    return false;
#else
    if (recoveringVideoBackend_
        || shuttingDown_
        || mediaKind_ != MediaKind::Video
        || mediaPath_.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_skip"),
            QString("reason=%1 recovering=%2 shutting_down=%3 kind=%4 has_path=%5")
                .arg(reason)
                .arg(recoveringVideoBackend_ ? 1 : 0)
                .arg(shuttingDown_ ? 1 : 0)
                .arg(debugMediaTypeName())
                .arg(mediaPath_.isEmpty() ? 0 : 1)
        );
        return false;
    }
    if (consecutiveVideoBackendRecoveryCount_ >= kVideoBackendRecoveryMaxConsecutive) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_skip"),
            QString("reason=%1 recovery_count=%2 limit=%3 frame_count=%4")
                .arg(reason)
                .arg(consecutiveVideoBackendRecoveryCount_)
                .arg(kVideoBackendRecoveryMaxConsecutive)
                .arg(videoFrameCountTotal_)
        );
        return false;
    }

    const QString path = mediaPath_;
    const double clampedTargetSecond = qMax(0.0, qIsFinite(targetSecond) ? targetSecond : currentPlaybackSecond());
    const qint64 targetMs = qMax<qint64>(0, qRound64((clampedTargetSecond + timelineOffsetSeconds_) * 1000.0));
    const bool completePausedSeek = pausedSeekCompletionPending_;
    const quint64 pausedSeekGeneration = pausedSeekGeneration_;
    const double pausedSeekSecond = pausedSeekTargetSecond_;
    const bool completePreparedPlayback = preparedPlaybackPending_;
    const quint64 preparedTransaction = preparedPlaybackTransaction_;
    const double preparedSecond = preparedPlaybackTargetSecond_;

    ++consecutiveVideoBackendRecoveryCount_;
    ++pausedSeekTimeoutSerial_;
    ++preparedPlaybackTimeoutSerial_;
    ++videoPlaybackWatchdogSerial_;
    pausedSeekCompletionPending_ = false;
    preparedPlaybackPending_ = false;
    if (completePreparedPlayback) {
        preparedPlaybackReady_ = true;
    }

    appendPreviewStageMediaLog(
        QStringLiteral("video_backend_recover_begin"),
        QString("reason=%1 recovery_count=%2 target_second=%3 target_ms=%4 resume=%5 frame_count=%6 age_ms=%7 playback_state=%8 position_ms=%9 sink=%10")
            .arg(reason)
            .arg(consecutiveVideoBackendRecoveryCount_)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(resumePlayback ? 1 : 0)
            .arg(videoFrameCountTotal_)
            .arg(currentVideoFrameAgeForDiagnosticsMs())
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(player_ != nullptr ? player_->position() : -1)
            .arg(videoSink_ != nullptr ? 1 : 0)
    );

    recoveringVideoBackend_ = true;
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();
    if (player_ != nullptr) {
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        player_->stop();
        player_->setSource(QUrl());
        player_->setAudioOutput(nullptr);
        delete player_;
        player_ = nullptr;
    }
    if (audioOutput_ != nullptr) {
        delete audioOutput_;
        audioOutput_ = nullptr;
    }

    initializeBackendObjects();
    if (player_ == nullptr) {
        recoveringVideoBackend_ = false;
        appendPreviewStageMediaLog(
            QStringLiteral("video_backend_recover_failed"),
            QString("reason=%1 target_second=%2")
                .arg(reason)
                .arg(clampedTargetSecond, 0, 'f', 6)
        );
        return false;
    }

    imageSource_ = QUrl();
    mediaKind_ = MediaKind::Video;
    ++videoSourceGeneration_;
    resetVideoFrameDiagnostics();
    observedPlayheadSecond_ = clampedTargetSecond;
    lastTimelineSecond_ = clampedTargetSecond;
    lastSeekMs_ = targetMs;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    videoPlaybackActiveElapsed_.invalidate();
    bindVideoOutput();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->pause();
    player_->setPosition(targetMs);
    if (resumePlayback) {
        if (videoFrameElapsed_.isValid()) {
            videoFrameElapsed_.restart();
        }
        player_->play();
        videoPlaybackActive_ = true;
        videoPlaybackActiveElapsed_.restart();
        consecutiveVideoPlaybackSoftRecoveryCount_ = 0;
        scheduleVideoPlaybackWatchdog(QStringLiteral("backend_recover_resume"));
    }
    updateClockDelta();
    recoveringVideoBackend_ = false;

    appendPreviewStageMediaLog(
        QStringLiteral("video_backend_recover_end"),
        QString("reason=%1 target_second=%2 target_ms=%3 resume=%4 playback_state=%5 sink=%6")
            .arg(reason)
            .arg(clampedTargetSecond, 0, 'f', 6)
            .arg(targetMs)
            .arg(resumePlayback ? 1 : 0)
            .arg(playbackStateName(playerPlaybackState(player_)))
            .arg(videoSink_ != nullptr ? 1 : 0)
    );
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
    if (completePausedSeek) {
        appendPreviewStageMediaLog(
            QStringLiteral("paused_seek_media_ack"),
            QString("generation=%1 second=%2 target_ms=%3 source=recover")
                .arg(pausedSeekGeneration)
                .arg(pausedSeekSecond, 0, 'f', 6)
                .arg(pausedSeekTargetMs_)
        );
        emit pausedSeekCompleted(pausedSeekSecond, pausedSeekGeneration);
    }
    if (completePreparedPlayback) {
        appendPreviewStageMediaLog(
            QStringLiteral("prepare_playback_ready"),
            QString("txn=%1 second=%2 target_ms=%3 source=recover")
                .arg(preparedTransaction)
                .arg(preparedSecond, 0, 'f', 6)
                .arg(preparedPlaybackTargetMs_)
        );
        emit playbackStartPrepared(preparedSecond, preparedTransaction);
    }
    return true;
#endif
}


#ifdef MIACODE_USE_QTAVPLAYER
void PreviewStageMediaHost::maybeRetryWithSoftwareDecode()
{
    if (softwareDecodeFallbackTried_
        || player_ == nullptr
        || mediaKind_ != MediaKind::Video
        || mediaPath_.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("video_software_fallback_skip"),
            QString("tried=%1 has_player=%2 kind=%3 has_path=%4")
                .arg(softwareDecodeFallbackTried_ ? 1 : 0)
                .arg(player_ != nullptr ? 1 : 0)
                .arg(debugMediaTypeName())
                .arg(mediaPath_.isEmpty() ? 0 : 1));
        return;
    }
    softwareDecodeFallbackTried_ = true;
    appendPreviewStageMediaLog(
        QStringLiteral("video_software_fallback"),
        QString("path=%1 reason=invalid_media resume_ms=%2 resume_playing=%3")
            .arg(mediaPath_)
            .arg(lastSeekMs_ >= 0 ? lastSeekMs_ : 0)
            .arg(videoPlaybackActive_ ? 1 : 0));
    const qint64 resumeMs = lastSeekMs_ >= 0 ? lastSeekMs_ : 0;
    const bool resumePlaying = videoPlaybackActive_;
    // Re-open forcing FFmpeg software decode (same as QT_AVPLAYER_NO_HWDEVICE).
    // setInputVideoCodec must precede setSource to apply on (re)load; the empty
    // setSource forces a reload since setSource(sameUrl) is a no-op.
    player_->stop();
    player_->setInputVideoCodec(QStringLiteral("software"));
    player_->setSource(QString());
    player_->setSource(mediaPath_);
    player_->setSpeed(static_cast<qreal>(playbackRate_));
    player_->seek(resumeMs);
    if (resumePlaying) {
        player_->play();
    } else {
        player_->pause();
    }
}
#endif  // MIACODE_USE_QTAVPLAYER

#ifdef MIACODE_USE_QTAVPLAYER
void PreviewStageMediaHost::reloadVideoDecodeInPlace()
{
    if (player_ == nullptr || mediaKind_ != MediaKind::Video || mediaPath_.isEmpty()) {
        return;
    }
    // Capture the live position + play state, flip the decoder on the SAME player,
    // reload in place (the empty setSource forces a reload since setSource(sameUrl)
    // is a no-op), then restore position + play state. Reuses the existing video
    // sink — no player recreation, no app restart. Bidirectional vs the one-way
    // software fallback: empty codec => hardware D3D11VA, "software" => FFmpeg CPU.
    const double second = qMax(0.0, currentPlaybackSecond());
    const qint64 resumeMs = qMax<qint64>(0, qRound64((second + timelineOffsetSeconds_) * 1000.0));
    const bool resumePlaying = videoPlaybackActive_;
    appendPreviewStageMediaLog(
        QStringLiteral("video_decode_reload"),
        QString("prefer_software=%1 resume_ms=%2 resume_playing=%3 path=%4")
            .arg(videoDecodePreferSoftware_ ? 1 : 0)
            .arg(resumeMs)
            .arg(resumePlaying ? 1 : 0)
            .arg(mediaPath_));
    player_->stop();
    player_->setInputVideoCodec(
        videoDecodePreferSoftware_ ? QStringLiteral("software") : QString());
    player_->setSource(QString());
    player_->setSource(mediaPath_);
    player_->setSpeed(static_cast<qreal>(playbackRate_));
    player_->seek(resumeMs);
    if (resumePlaying) {
        player_->play();
    } else {
        player_->pause();
    }
}
#endif  // MIACODE_USE_QTAVPLAYER

void PreviewStageMediaHost::setVideoDecodePreference(bool preferSoftware)
{
    if (videoDecodePreferSoftware_ == preferSoftware) {
        return;
    }
    videoDecodePreferSoftware_ = preferSoftware;
    appendPreviewStageMediaLog(
        QStringLiteral("video_decode_preference"),
        QString("prefer_software=%1 has_player=%2 kind=%3")
            .arg(preferSoftware ? 1 : 0)
            .arg(player_ != nullptr ? 1 : 0)
            .arg(debugMediaTypeName()));
#ifdef MIACODE_USE_QTAVPLAYER
    // An explicit user choice clears the session-only auto-fallback latch so a
    // future backend rebuild honors the chosen mode (and so switching back to
    // hardware isn't immediately re-forced to software by a stale fallback flag).
    softwareDecodeFallbackTried_ = false;
    if (player_ != nullptr) {
        if (mediaKind_ == MediaKind::Video && !mediaPath_.isEmpty()) {
            reloadVideoDecodeInPlace();  // hot-switch the currently-loaded PV
        } else {
            // No PV loaded yet: apply now so the next load uses the chosen decoder.
            player_->setInputVideoCodec(
                preferSoftware ? QStringLiteral("software") : QString());
        }
    }
#endif
}
