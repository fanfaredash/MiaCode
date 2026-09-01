#include "app/v2/UiRequestService.h"
#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
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
#include "tools/video_export/HudFontSettings.h"

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

using namespace miacode::mainwindow::shared;

MainWindow::DialogsSection::DialogsSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

QString MainWindow::DialogsSection::resolveLatencyDetectorTrackPath() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
}

QString MainWindow::DialogsSection::resolveCurrentChartDirectory() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return QFileInfo(owner_.currentFilePath_).absoluteDir().absolutePath();
}

void MainWindow::DialogsSection::releasePreviewMediaForFileOperation()
{
    owner_.onStopPreview();
    // clearPreviewStageMediaRoute() -> setChartPath("") -> clearMedia() drops the
    // retained frames from both QML sinks and unloads the demuxer. But that unload
    // is ASYNCHRONOUS: the QAVPlayer survives and keeps its own
    // QSharedPointer<QAVFormatContext> ref until the demuxer thread lands the
    // close, so the pv.mp4 avio handle could still be open when we rename the file
    // below -> ERROR_SHARING_VIOLATION ("pv占用"). That async gap — not just a
    // stray sink frame — was the real cause the earlier empty-frame fix missed.
    owner_.clearPreviewStageMediaRoute();
    // Deterministic close: destroy the QAVPlayer so ~QAVPlayer joins its decode
    // threads and releases the format context SYNCHRONOUSLY (avformat_close_input
    // runs before this returns). The player is rebuilt on the post-op reload. We
    // keep the HOST alive so the QML VideoOutput's sink stays attached (destroying
    // the host left the post-op preview blank, "压缩后视频不加载").
    // See project_pv_file_lock_release.
    owner_.releasePreviewStageMediaDecoderForFileOperation();
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
}

void MainWindow::DialogsSection::reloadPreviewMediaAfterFileOperation(bool reloadTrack)
{
    if (owner_.currentFilePath_.isEmpty()) {
        return;
    }
    if (reloadTrack) {
        owner_.lastTrackPath_ = miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
        if (state_.waveformCacheService_ != nullptr) {
            state_.waveformCacheService_->clear();
        }
        if (owner_.previewSfxRuntime_ != nullptr) {
            const QtPreviewSfxRuntime::AssetSubmission reload =
                owner_.previewSfxRuntime_->reloadAssetsForChartWithWarmupPaths(
                    owner_.currentFilePath_,
                    owner_.lastTrackPath_,
                    miacode::preview_sfx::resolveSfxDirectory(),
                    owner_.previewAudioSettings_);
            owner_.previewSfxRuntimePrepared_ = false;
            owner_.previewSfxRuntimePreparationAssetGeneration_ = reload.post.accepted
                ? reload.identity.assetGeneration
                : 0;
            owner_.previewSfxRuntimePreparationSequence_ = reload.post.accepted
                ? reload.identity.sequence
                : 0;
            owner_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(owner_.previewPlaybackRate_);
            owner_.previewSfxRuntime_->resetRetainedPreviewPlaybackTransaction(qMax(0.0, owner_.qtPreviewPauseSecond_));
        }
        owner_.refreshWaveformCache();
        owner_.updatePreviewSliderRange();
        owner_.updatePreviewSliderPosition(qMax(0.0, owner_.qtPreviewPauseSecond_));
    }
    owner_.syncPreviewStageMediaRouteChartPath(
        owner_.currentFilePath_,
        owner_.lastTrackPath_,
        qMax(0.0, owner_.qtPreviewPauseSecond_),
        owner_.applicationServices_.workspace().document().videoPath
    );
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void MainWindow::DialogsSection::onPreviewAudioSettings()
{
}


void MainWindow::DialogsSection::onPreviewVideoSettings()
{
}


void MainWindow::DialogsSection::onAbout()
{
}


void MainWindow::DialogsSection::onSkinSettings()
{
}

void MainWindow::DialogsSection::onReadTitleFromTrack()
{
}

void MainWindow::DialogsSection::onReadArtistFromTrack()
{
}

void MainWindow::DialogsSection::onExtractBackgroundFromTrack()
{
}

void MainWindow::DialogsSection::showMediaOperationCompleteDialog(
    const QString& title,
    const QString& summary,
    const QString& producedFilePath)
{
    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
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
