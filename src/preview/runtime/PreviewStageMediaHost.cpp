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

constexpr int kVideoFrameIntervalWindowSize = 120;

}  // namespace

PreviewStageMediaHost::PreviewStageMediaHost(QObject* parent)
    : QObject(parent)
{
    videoFrameIntervalsMs_.resize(kVideoFrameIntervalWindowSize);
    videoFrameIntervalsMs_.fill(0.0);
}

PreviewStageMediaHost::~PreviewStageMediaHost()
{
    QElapsedTimer timer;
    timer.start();
    const QString mediaType = debugMediaTypeName();
    const bool hadPlayer = player_ != nullptr;
    shutdownForAppExit();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/stage_media_host"),
        QStringLiteral("destructor"),
        timer.elapsed(),
        QStringLiteral("had_player=%1 media_kind=%2")
            .arg(hadPlayer ? 1 : 0)
            .arg(mediaType)
    );
}

void PreviewStageMediaHost::shutdownForAppExit()
{
    shuttingDown_ = true;
    clearMedia();
}

void PreviewStageMediaHost::setWarmupResolvedMediaPath(const QString& chartPath, const QString& mediaPath)
{
    // No-op: the resolved-media cache was removed (see resolveMediaPath). Media
    // resolution is now always live; the warmup worker still byte-prefetches the
    // file into the OS cache. Kept as a no-op to preserve the call interface.
    Q_UNUSED(chartPath);
    Q_UNUSED(mediaPath);
}

void PreviewStageMediaHost::attachVideoOutputObject(QObject* videoOutputObject)
{
    attachVideoOutputObjects(videoOutputObject, nullptr);
}

void PreviewStageMediaHost::attachVideoOutputObjects(QObject* videoOutputObject, QObject* innerVideoOutputObject)
{
    if (videoOutputObject_ == videoOutputObject && innerVideoOutputObject_ == innerVideoOutputObject) {
        return;
    }
    videoOutputObject_ = videoOutputObject;
    innerVideoOutputObject_ = innerVideoOutputObject;
    bindVideoOutput();
}

void PreviewStageMediaHost::detachVideoOutputObject(QObject* videoOutputObject)
{
    detachVideoOutputObjects(videoOutputObject, nullptr);
}

void PreviewStageMediaHost::detachVideoOutputObjects(QObject* videoOutputObject, QObject* innerVideoOutputObject)
{
    if (videoOutputObject_ == nullptr || videoOutputObject == nullptr) {
        return;
    }
    if (videoOutputObject_ != videoOutputObject) {
        return;
    }
    if (innerVideoOutputObject != nullptr && innerVideoOutputObject_ != innerVideoOutputObject) {
        return;
    }
    videoOutputObject_.clear();
    innerVideoOutputObject_.clear();
    bindVideoOutput();
}

bool PreviewStageMediaHost::mediaVisible() const
{
    return mediaVisible_;
}

void PreviewStageMediaHost::setMediaVisible(bool visible)
{
    if (mediaVisible_ == visible) {
        return;
    }
    mediaVisible_ = visible;
    // Phase 4c-9 — invalidate the toImage() throttle so the first
    // QVideoSink frame after a visibility-on transition is captured
    // immediately (bg shouldn't ghost between hide→show with stale
    // content, especially at scrub time). Only used by the legacy
    // CPU detour; ignored under per-pixel alpha mode.
    if (visible) {
        videoFrameToImageThrottle_.invalidate();
    }
    emit mediaVisibilityChanged();
}

QUrl PreviewStageMediaHost::imageSource() const
{
    return imageSource_;
}

void PreviewStageMediaHost::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}

QString PreviewStageMediaHost::debugMediaTypeName() const
{
    switch (mediaKind_) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::None:
    default:
        return QStringLiteral("none");
    }
}
