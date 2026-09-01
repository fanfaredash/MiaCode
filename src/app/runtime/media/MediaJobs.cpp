#include "app/v2/UiRequestService.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/Id3TagReader.h"
#include "common/OperationLog.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewGameplayConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencyAnalysis.h"

#include <QDesktopServices>
#include <QUrl>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <cmath>

#include "common/DebugLog.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")
#endif

using namespace miacode::runtime::shared;

miacode::runtime::MediaJobsHost::MediaJobsHost(
    Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

QString miacode::runtime::MediaJobsHost::resolveLatencyDetectorTrackPath() const
{
    if (session_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return miacode::chart_assets::resolveTrackPath(session_.currentFilePath_);
}

QString miacode::runtime::MediaJobsHost::resolveCurrentChartDirectory() const
{
    if (session_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return QFileInfo(session_.currentFilePath_).absoluteDir().absolutePath();
}

void miacode::runtime::MediaJobsHost::releasePreviewMediaForFileOperation()
{
    session_.onStopPreview();
    // clearPreviewStageMediaRoute() -> setChartPath("") -> clearMedia() drops the
    // retained frames from both QML sinks and unloads the demuxer. But that unload
    // is ASYNCHRONOUS: the QAVPlayer survives and keeps its own
    // QSharedPointer<QAVFormatContext> ref until the demuxer thread lands the
    // close, so the pv.mp4 avio handle could still be open when we rename the file
    // below -> ERROR_SHARING_VIOLATION ("pv占用"). That async gap — not just a
    // stray sink frame — was the real cause the earlier empty-frame fix missed.
    session_.clearPreviewStageMediaRoute();
    // Deterministic close: destroy the QAVPlayer so ~QAVPlayer joins its decode
    // threads and releases the format context SYNCHRONOUSLY (avformat_close_input
    // runs before this returns). The player is rebuilt on the post-op reload. We
    // keep the HOST alive so the QML VideoOutput's sink stays attached (destroying
    // the host left the post-op preview blank, "压缩后视频不加载").
    // See project_pv_file_lock_release.
    session_.releasePreviewStageMediaDecoderForFileOperation();
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
}

void miacode::runtime::MediaJobsHost::reloadPreviewMediaAfterFileOperation(bool reloadTrack)
{
    if (session_.currentFilePath_.isEmpty()) {
        return;
    }
    if (reloadTrack) {
        session_.lastTrackPath_ = miacode::chart_assets::resolveTrackPath(session_.currentFilePath_);
        if (state_.waveformCacheService_ != nullptr) {
            state_.waveformCacheService_->clear();
        }
        if (session_.previewSfxRuntime_ != nullptr) {
            const QtPreviewSfxRuntime::AssetSubmission reload =
                session_.previewSfxRuntime_->reloadAssetsForChartWithWarmupPaths(
                    session_.currentFilePath_,
                    session_.lastTrackPath_,
                    miacode::preview_sfx::resolveSfxDirectory(),
                    session_.previewAudioSettings_);
            session_.previewSfxRuntimePrepared_ = false;
            session_.previewSfxRuntimePreparationAssetGeneration_ = reload.post.accepted
                ? reload.identity.assetGeneration
                : 0;
            session_.previewSfxRuntimePreparationSequence_ = reload.post.accepted
                ? reload.identity.sequence
                : 0;
            session_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(session_.previewPlaybackRate_);
            session_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(qMax(0.0, session_.pauseSecond_));
        }
        session_.refreshWaveformCache();
        session_.updatePreviewSliderRange();
        session_.updatePreviewSliderPosition(qMax(0.0, session_.pauseSecond_));
    }
    session_.syncPreviewStageMediaRouteChartPath(
        session_.currentFilePath_,
        session_.lastTrackPath_,
        qMax(0.0, session_.pauseSecond_),
        session_.applicationServices_.workspace().document().videoPath
    );
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void miacode::runtime::MediaJobsHost::onPreviewAudioSettings()
{
}


void miacode::runtime::MediaJobsHost::onPreviewVideoSettings()
{
}


void miacode::runtime::MediaJobsHost::onAbout()
{
}


void miacode::runtime::MediaJobsHost::onSkinSettings()
{
}

void miacode::runtime::MediaJobsHost::onReadTitleFromTrack()
{
}

void miacode::runtime::MediaJobsHost::onReadArtistFromTrack()
{
}

void miacode::runtime::MediaJobsHost::onExtractBackgroundFromTrack()
{
}

void miacode::runtime::MediaJobsHost::showMediaOperationCompleteDialog(
    const QString& title,
    const QString& summary,
    const QString& producedFilePath)
{
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    requests->requestNoticeAction(
        miacode::v2::NoticeSeverity::Information,
        title,
        summary,
        QDir::toNativeSeparators(producedFilePath),
        UiText::text(QStringLiteral("dialogs.open_folder")),
        [producedFilePath](bool openFolder) {
            if (!openFolder) {
                return;
            }
            const QString dir = QFileInfo(producedFilePath).absoluteDir().absolutePath();
            if (!dir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            }
        });
}
