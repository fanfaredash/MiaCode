#include "preview/runtime/PreviewStageMediaHost.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/PreviewVideoGeometryConfig.h"
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

QString normalizedLocalPath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

}  // namespace

bool PreviewStageMediaHost::hasResolvedMedia() const
{
    return mediaKind_ != MediaKind::None;
}


bool PreviewStageMediaHost::hasVideoMedia() const
{
    return mediaKind_ == MediaKind::Video;
}


QImage PreviewStageMediaHost::currentBackgroundImage() const
{
    // Phase 4d — when per-pixel alpha is enabled the DComp HWND is
    // transparent at the OS level, so QML's PreviewStageMediaItem
    // (Image/VideoOutput) below shows through natively. The CPU
    // detour (StageBackgroundSource painting the bg) is no longer
    // needed and should be skipped to avoid duplicate paint cost.
    if (miacode::debug_options::previewDCompPerPixelAlphaEnabled()) {
        return QImage();
    }
    // Phase 4c-8 (image) + 4c-9 (video) — return the cached bg image
    // for both kinds. For Image: loadImageMedia() seeds it once at
    // chart-load. For Video: noteVideoFrameArrived() converts each
    // QVideoFrame to a QImage and updates it as new frames flow in.
    // The DComp surface reads this every snapshot tick; image-mode
    // returns the same content frame-to-frame (cheap), video-mode
    // returns the latest decoded frame.
    if (mediaKind_ != MediaKind::Image && mediaKind_ != MediaKind::Video) {
        return QImage();
    }
    return loadedBackgroundImage_;
}


QString PreviewStageMediaHost::resolvedVideoPath() const
{
    if (mediaKind_ != MediaKind::Video) {
        return QString();
    }
    return mediaPath_;
}


int PreviewStageMediaHost::backgroundScaleMode() const
{
    return static_cast<int>(backgroundScaleMode_);
}


void PreviewStageMediaHost::setBackgroundScaleModeValue(int mode)
{
    if (mode == static_cast<int>(PreviewBackgroundScaleMode::SquareFitContain)) {
        setBackgroundScaleMode(PreviewBackgroundScaleMode::SquareFitContain);
        return;
    }
    if (mode == static_cast<int>(PreviewBackgroundScaleMode::InnerCircleFitOuterFill)) {
        setBackgroundScaleMode(PreviewBackgroundScaleMode::InnerCircleFitOuterFill);
        return;
    }
    setBackgroundScaleMode(
        mode == static_cast<int>(PreviewBackgroundScaleMode::FitContain)
            ? PreviewBackgroundScaleMode::FitContain
            : PreviewBackgroundScaleMode::FillCrop
    );
}


void PreviewStageMediaHost::setBackgroundScaleMode(PreviewBackgroundScaleMode mode)
{
    if (backgroundScaleMode_ == mode) {
        return;
    }
    backgroundScaleMode_ = mode;
    refreshInnerVideoSinkForScaleMode();
    emit backgroundScaleModeChanged();
}

bool PreviewStageMediaHost::innerVideoSinkActive() const
{
    return innerVideoSink_ != nullptr
        && backgroundScaleMode_ == PreviewBackgroundScaleMode::InnerCircleFitOuterFill;
}

void PreviewStageMediaHost::refreshInnerVideoSinkForScaleMode()
{
    if (innerVideoSink_ == nullptr || innerVideoSink_ == videoSink_) {
        return;
    }
    if (innerVideoSinkActive()) {
#ifdef MIACODE_USE_QTAVPLAYER
        if (lastVideoFrame_.isValid()) {
            innerVideoSink_->setVideoFrame(lastVideoFrame_);
        }
#endif
        return;
    }
    // Leaving the mode: drop the retained frame so the inner sink stops pinning
    // a decode-pool surface (same reason releaseVideoBackend() clears it).
    innerVideoSink_->setVideoFrame(QVideoFrame());
}

double PreviewStageMediaHost::layoutSquareScale() const
{
    return layoutSquareScale_;
}

void PreviewStageMediaHost::setLayoutSquareScale(double scale)
{
    const double normalized = miacode::preview_video::normalizedLayoutSquareScale(scale);
    if (qFuzzyCompare(layoutSquareScale_ + 1.0, normalized + 1.0)) {
        return;
    }
    layoutSquareScale_ = normalized;
    emit layoutSquareScaleChanged();
}


void PreviewStageMediaHost::setChartPath(const QString& chartPath,
                                         const QString& chartVideoOverridePath)
{
    const QString normalizedChartPath = normalizedLocalPath(chartPath);
    // Resolve the media up-front so the skip decision can be content-aware.
    // Keying on (chart-path, video-override) jointly is necessary but NOT
    // sufficient: the user may keep the same chart + same `&video=` yet rewrite
    // the bg/pv bytes in place (the in-app video tools do exactly that, and
    // users swap same-named files), so a same-named clip with new content must
    // still force a re-decode. Phase 4c — explicit `&video=` beats the sibling
    // heuristic; fall back to the legacy resolver for image-only backgrounds.
    QString resolvedPath;
    if (!normalizedChartPath.isEmpty()) {
        resolvedPath = miacode::chart_assets::resolveChartVideoPath(
            normalizedChartPath, chartVideoOverridePath);
        if (resolvedPath.isEmpty()) {
            resolvedPath = resolveMediaPath(normalizedChartPath);
        }
    }
    const QString mediaStamp = miacode::fs::fileContentStamp(resolvedPath);
    if (normalizedChartPath == chartPath_
        && chartVideoOverridePath == chartVideoOverridePath_
        && mediaStamp == mediaStamp_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    chartVideoOverridePath_ = chartVideoOverridePath;
    mediaStamp_ = mediaStamp;
    clearMedia();
    if (chartPath_.isEmpty()) {
        appendPreviewStageMediaLog(QStringLiteral("set_chart_path"), QStringLiteral("chart=(empty) kind=none"));
        return;
    }

    if (resolvedPath.isEmpty()) {
        appendPreviewStageMediaLog(
            QStringLiteral("set_chart_path"),
            QStringLiteral("chart=%1 kind=none").arg(chartPath_)
        );
        return;
    }

    mediaPath_ = resolvedPath;
    const bool isVideo = miacode::chart_assets::isVideoBackgroundPath(mediaPath_);
    if (isVideo) {
        loadVideoMedia(mediaPath_);
    } else {
        loadImageMedia(mediaPath_);
    }

    appendPreviewStageMediaLog(
        QStringLiteral("set_chart_path"),
        QStringLiteral("chart=%1 media=%2 kind=%3 override=%4")
            .arg(chartPath_)
            .arg(mediaPath_)
            .arg(debugMediaTypeName())
            .arg(chartVideoOverridePath_.isEmpty()
                    ? QStringLiteral("(none)")
                    : chartVideoOverridePath_)
    );
}


void PreviewStageMediaHost::clearMedia()
{
#ifdef MIACODE_USE_QTAVPLAYER
    if (videoFrameConnection_) {
        QObject::disconnect(videoFrameConnection_);
        videoFrameConnection_ = QMetaObject::Connection();
    }
    lastVideoFrame_ = QVideoFrame();
    lastFramePtsSeconds_ = -1.0;
    videoBackendLoaded_ = false;
    // Push an empty frame so the QML VideoOutput's sink RELEASES the last
    // decoded frame. A retained QtAVPlayer QVideoFrame transitively holds a
    // QAVStream -> QSharedPointer<QAVFormatContext>, which keeps the FFmpeg
    // avio handle on the video file OPEN even after the QAVPlayer is destroyed
    // (the QML VideoOutput outlives this host). That leaked read handle is the
    // real cause of "pv占用" / "无法替换原文件": read still works but rename is
    // refused. Dropping the sink frame lets the format-context refcount reach
    // zero so ~QAVFormatContext -> avformat_close_input runs. Must happen
    // BEFORE videoSink_.clear() (which only nulls our QPointer, not the sink's
    // frame). See project_pv_file_lock_release.
    if (videoSink_ != nullptr) {
        videoSink_->setVideoFrame(QVideoFrame());
    }
    videoSink_.clear();
    // handleDecodedVideoFrame pushes each decoded frame into BOTH the outer sink
    // and the inner-circle sink (InnerCircleFitOuterFill scale mode). A frame
    // left in the inner sink pins the same QAVFormatContext -> pv.mp4 stays open.
    // Release it symmetrically with the outer sink.
    if (innerVideoSink_ != nullptr) {
        innerVideoSink_->setVideoFrame(QVideoFrame());
    }
    innerVideoSink_.clear();
    if (player_ != nullptr) {
        player_->stop();
        player_->setSource(QString());
    }
#elif defined(HAVE_QT_MULTIMEDIA)
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();
    if (player_ != nullptr) {
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        player_->stop();
        player_->setSource(QUrl());
    }
#endif
    // Phase 4c-8 — clear the cached image so a chart switch from
    // image→video doesn't leave the old image visible behind the
    // new video.
    loadedBackgroundImage_ = QImage();
    ++videoSourceGeneration_;
    mediaKind_ = MediaKind::None;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    mediaPath_.clear();
    imageSource_ = QUrl();
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    observedPlayheadSecond_ = 0.0;
    clockDeltaSeconds_ = 0.0;
    resetVideoFrameDiagnostics();
    if (shuttingDown_) {
        return;
    }
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
}


void PreviewStageMediaHost::releaseDecoderForFileReplace()
{
    MC_OP("PreviewStageMediaHost::releaseDecoderForFileReplace");
    // Soft path first: drops the retained sink frames (both sinks), unloads the
    // demuxer, resets media state and generation counters. On its own this is
    // not enough on Windows because the QAVPlayer keeps its own
    // QSharedPointer<QAVFormatContext> ref alive until its async unload lands, so
    // the pv.mp4 avio handle can still be open right after this returns.
    clearMedia();
#ifdef MIACODE_USE_QTAVPLAYER
    // Destroy the player so ~QAVPlayer runs synchronously: it stops and JOINS the
    // demux/decode threads and releases the format context, so
    // avformat_close_input has executed by the time we return. This is the same
    // hard teardown recoverVideoBackend uses; initializeBackendObjects() rebuilds
    // a fresh player on the next load.
    if (player_ != nullptr) {
        player_->stop();
        player_->setSource(QString());
        delete player_;
        player_ = nullptr;
    }
    videoBackendLoaded_ = false;
#elif defined(HAVE_QT_MULTIMEDIA)
    // POSIX rename-while-open is legal, so the file lock never bites here, but
    // tear the backend down the same way for parity and a clean reload.
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
#endif
    appendPreviewStageMediaLog(
        QStringLiteral("release_decoder_for_file_replace"),
        QStringLiteral("player_destroyed=1"));
}


QString PreviewStageMediaHost::resolveMediaPath(const QString& chartPath) const
{
    // Resolved-media warmup cache removed (2026-06-03). It keyed on the chart-path
    // string only and never re-validated, so a same-named bg/pv with new content
    // was shadowed by the stale cached path. Always resolve live; same-content
    // re-decode is avoided by the content-stamp check in setChartPath().
    return miacode::chart_assets::resolveBackgroundMediaPath(chartPath);
}


void PreviewStageMediaHost::loadImageMedia(const QString& path)
{
    imageSource_ = QUrl::fromLocalFile(path);
    // Phase 4c-8 — also load the QImage so DComp's StageBackgroundSource
    // can render it. The QML PreviewStageMediaItem still binds to
    // imageSource_ but is occluded by the DComp HWND on top; the DComp
    // surface needs its own copy to actually paint the bg in the
    // chart-preview area.
    loadedBackgroundImage_ = QImage(path);
    if (loadedBackgroundImage_.isNull()) {
        appendPreviewStageMediaLog(
            QStringLiteral("load_image_failed"),
            QStringLiteral("path=%1").arg(path));
    }
    mediaKind_ = MediaKind::Image;
    ++videoSourceGeneration_;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    resetVideoFrameDiagnostics();
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
}


void PreviewStageMediaHost::loadVideoMedia(const QString& path)
{
    MC_OP("PreviewStageMediaHost::loadVideoMedia");
#ifdef MIACODE_USE_QTAVPLAYER
    initializeBackendObjects();
    if (player_ == nullptr) {
        return;
    }

    imageSource_ = QUrl();
    // Clear stale bg + arm the toImage() throttle so the first decoded frame
    // after this chart switch is captured immediately for the DComp fallback.
    loadedBackgroundImage_ = QImage();
    videoFrameToImageThrottle_.invalidate();
    mediaKind_ = MediaKind::Video;
    softwareDecodeFallbackTried_ = false;
    videoBackendLoaded_ = false;
    lastVideoFrame_ = QVideoFrame();
    lastFramePtsSeconds_ = -1.0;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    resetVideoFrameDiagnostics();
    bindVideoOutput();

    // (Re)connect the decoded-frame stream, capturing the current source
    // generation by value (clearMedia() bumped it just before this). Any
    // in-flight frame from a previous source carries the old generation and is
    // dropped in handleDecodedVideoFrame. Default (auto/queued) connection ->
    // the handler runs on the GUI thread.
    if (videoFrameConnection_) {
        QObject::disconnect(videoFrameConnection_);
        videoFrameConnection_ = QMetaObject::Connection();
    }
    const quint64 sourceGeneration = videoSourceGeneration_;
    videoFrameConnection_ = connect(player_, &QAVPlayer::videoFrame, this,
        [this, sourceGeneration](const QAVVideoFrame& avFrame) {
            const double ptsSeconds = avFrame.pts();
            const double durationSeconds = avFrame.duration();
            // Implicit QAVVideoFrame -> QVideoFrame; a D3D11VA hardware frame
            // stays a zero-copy RhiTexture handle through this conversion.
            QVideoFrame vf = avFrame;
            handleDecodedVideoFrame(vf, ptsSeconds, durationSeconds, sourceGeneration);
        });

    player_->setSource(path);
    player_->setSpeed(static_cast<qreal>(playbackRate_));
    // Queued before LoadedMedia by QtAVPlayer; lands the player paused on the
    // first frame so the bg shows immediately and playback is timeline-driven.
    player_->pause();
    player_->seek(0);
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    Q_UNUSED(path);
#else
    initializeBackendObjects();
    if (player_ == nullptr) {
        return;
    }

    imageSource_ = QUrl();
    // Phase 4c-9 — clear stale image bg from a prior chart so the
    // DComp surface paints nothing until the first decoded video
    // frame arrives via noteVideoFrameArrived(). Also invalidate
    // the toImage() throttle so the very first frame after this
    // chart switch is captured immediately (otherwise a recently-
    // armed throttle from the previous chart could delay it up to
    // 33ms on top of the QMediaPlayer prepare latency).
    loadedBackgroundImage_ = QImage();
    videoFrameToImageThrottle_.invalidate();
    mediaKind_ = MediaKind::Video;
    pausedSeekCompletionPending_ = false;
    pausedSeekTargetMs_ = -1;
    pausedSeekTargetSecond_ = 0.0;
    pausedSeekGeneration_ = 0;
    ++pausedSeekTimeoutSerial_;
    preparedPlaybackPending_ = false;
    preparedPlaybackReady_ = false;
    preparedPlaybackTargetMs_ = -1;
    preparedPlaybackTargetSecond_ = 0.0;
    preparedPlaybackTransaction_ = 0;
    ++preparedPlaybackTimeoutSerial_;
    lastTimelineSecond_ = 0.0;
    lastSeekMs_ = -1;
    videoPlaybackActive_ = false;
    videoPlaybackPendingStart_ = false;
    ++videoPlaybackWatchdogSerial_;
    consecutiveVideoBackendRecoveryCount_ = 0;
    resetVideoFrameDiagnostics();
    syncMediaStatusBeaconBudget_ = qMax(syncMediaStatusBeaconBudget_, 12);
    syncVideoFrameBeaconBudget_ = qMax(syncVideoFrameBeaconBudget_, 8);
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/before_bind tid=%lu rate=%.3f status=%d state=%d",
            currentBeaconTid(),
            playbackRate_,
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    bindVideoOutput();
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_bind");
    {
        char buf[260];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/before_set_source tid=%lu rate=%.3f path_len=%d",
            currentBeaconTid(),
            playbackRate_,
            static_cast<int>(path.size()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->setSource(QUrl::fromLocalFile(path));
    {
        char buf[220];
        std::snprintf(buf, sizeof(buf),
            "preview/load_video/after_set_source tid=%lu status=%d state=%d",
            currentBeaconTid(),
            static_cast<int>(player_->mediaStatus()),
            static_cast<int>(playerPlaybackState(player_)));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    player_->pause();
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_pause");
    player_->setPosition(0);
    miacode::oplog::appendStartupBeaconLine("preview/load_video/after_set_position_zero");
    updateClockDelta();
    emit imageSourceChanged();
    emit mediaStateChanged();
    emit diagnosticsChanged();
#endif
}


void PreviewStageMediaHost::bindVideoOutput()
{
#ifdef MIACODE_USE_QTAVPLAYER
    // Push model: resolve the QVideoSink owned by the QML VideoOutput; decoded
    // frames are pushed into it from handleDecodedVideoFrame. The decoded-frame
    // signal connection itself is owned by loadVideoMedia (per source
    // generation), so this only (re)binds the sink target.
    videoSink_.clear();
    innerVideoSink_.clear();
    const auto resolveSink = [](QObject* videoOutputObject) -> QVideoSink* {
        if (videoOutputObject == nullptr) {
            return nullptr;
        }
        QObject* sinkObject = nullptr;
        const QVariant sinkVariant = videoOutputObject->property("videoSink");
        if (sinkVariant.isValid()) {
            sinkObject = sinkVariant.value<QObject*>();
        }
        return qobject_cast<QVideoSink*>(sinkObject);
    };
    videoSink_ = resolveSink(videoOutputObject_);
    innerVideoSink_ = resolveSink(innerVideoOutputObject_);
    if (lastVideoFrame_.isValid()) {
        if (videoSink_ != nullptr) {
            videoSink_->setVideoFrame(lastVideoFrame_);
        }
        if (innerVideoSinkActive() && innerVideoSink_ != videoSink_) {
            innerVideoSink_->setVideoFrame(lastVideoFrame_);
        }
    }
    appendPreviewStageMediaLog(
        QStringLiteral("bind_video_output"),
        QStringLiteral("attached=%1 sink=%2 inner_attached=%3 inner_sink=%4")
            .arg(videoOutputObject_ != nullptr ? 1 : 0)
            .arg(videoSink_ != nullptr ? 1 : 0)
            .arg(innerVideoOutputObject_ != nullptr ? 1 : 0)
            .arg(innerVideoSink_ != nullptr ? 1 : 0));
    return;
#elif !defined(HAVE_QT_MULTIMEDIA)
    innerVideoSink_.clear();
    return;
#else
    if (videoSinkFrameConnection_) {
        QObject::disconnect(videoSinkFrameConnection_);
        videoSinkFrameConnection_ = QMetaObject::Connection();
    }
    videoSink_.clear();
    innerVideoSink_.clear();

    if (player_ == nullptr) {
        return;
    }

    if (videoOutputObject_ == nullptr) {
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_clear_output");
        player_->setVideoOutput(static_cast<QObject*>(nullptr));
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_clear_output");
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=0 sink=0"));
        return;
    }

    miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_set_output_object");
    player_->setVideoOutput(videoOutputObject_);
    miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_set_output_object");

    QObject* sinkObject = nullptr;
    const QVariant sinkVariant = videoOutputObject_->property("videoSink");
    if (sinkVariant.isValid()) {
        sinkObject = sinkVariant.value<QObject*>();
    }
    videoSink_ = qobject_cast<QVideoSink*>(sinkObject);
    if (videoSink_ != nullptr) {
        const quint64 sourceGeneration = videoSourceGeneration_;
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/before_connect_sink");
        videoSinkFrameConnection_ = QObject::connect(videoSink_, &QVideoSink::videoFrameChanged, this, [this, sourceGeneration](const QVideoFrame& frame) {
            if (syncVideoFrameBeaconBudget_ > 0) {
                char buf[180];
                std::snprintf(buf, sizeof(buf),
                    "preview/frame/sink_signal tid=%lu source_gen=%llu",
                    currentBeaconTid(),
                    static_cast<unsigned long long>(sourceGeneration));
                miacode::oplog::appendStartupBeaconLine(buf);
            }
            noteVideoFrameArrived(frame, sourceGeneration);
        });
        miacode::oplog::appendStartupBeaconLine("preview/bind_video_output/after_connect_sink");
        appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=1"));
        return;
    }

    appendPreviewStageMediaLog(QStringLiteral("bind_video_output"), QStringLiteral("attached=1 sink=0"));
#endif
}

