#include "app/v2/UiRequestService.h"
#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../preview/MainWindow.PreviewSection.h"
#include "../window/MainWindow.WindowSection.h"

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

QString MainWindow::DialogsSection::resolveCurrentChartDirectory() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return QFileInfo(owner_.currentFilePath_).absoluteDir().absolutePath();
}

bool MainWindow::DialogsSection::releasePreviewMediaForFileOperation()
{
    if (QCoreApplication::instance() == nullptr) {
        return false;
    }
    if (owner_.previewMediaFileOperationActive_) {
        return false;
    }
    // Acquire the transaction lock before any event pumping.  Re-entrant
    // release requests must fail while the decoder is being torn down.
    owner_.previewMediaFileOperationActive_ = true;
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
    if (!owner_.releasePreviewStageMediaDecoderForFileOperation()) {
        owner_.previewMediaFileOperationActive_ = false;
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(25);
    }
    return true;
}

bool MainWindow::DialogsSection::reloadPreviewMediaAfterFileOperation(bool reloadTrack)
{
    const auto clearOperationLock = [this]() {
        owner_.previewMediaFileOperationActive_ = false;
    };
    if (owner_.previewSection_ == nullptr
        || !owner_.previewSection_->previewUsesStageMediaHostRoute()) {
        clearOperationLock();
        return false;
    }
    const QFileInfo chartInfo(owner_.currentFilePath_);
    if (owner_.currentFilePath_.isEmpty() || !chartInfo.exists() || !chartInfo.isFile()) {
        clearOperationLock();
        return false;
    }
    bool restoreAccepted = true;
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
            restoreAccepted = reload.post.accepted;
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
        owner_.document_.videoPath
    );
    if (owner_.previewStageMediaHost_ == nullptr) {
        clearOperationLock();
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    // The route request has been accepted and its immediate synchronization
    // events drained; this does not claim that an asynchronous decoder has
    // already produced a frame.
    clearOperationLock();
    return restoreAccepted;
}

void MainWindow::DialogsSection::onPreviewAudioSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewAudioSettings");
    openPreviewSettingsDialog(
        true,
        false,
        UiText::text(QStringLiteral("dialog.audio_settings.title"))
    );
}

void MainWindow::DialogsSection::onPreviewVideoSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewVideoSettings");
    openPreviewSettingsDialog(
        false,
        true,
        UiText::text(QStringLiteral("dialog.video_settings.title"))
    );
}

void MainWindow::DialogsSection::onAbout()
{
    MC_OP("MainWindow::DialogsSection::onAbout");
    QString buildType = "Release";
#ifndef NDEBUG
    buildType = "Debug";
#endif
    const QString platform = QString("%1 / %2 / %3")
        .arg(QSysInfo::productType())
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(QSysInfo::buildAbi());

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(UiText::text(QStringLiteral("action.about")));
    dialog.setModal(true);
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName("AboutIcon");
    iconLabel->setFixedSize(64, 64);
    QPixmap appIcon = QIcon(":/icons/app.png").pixmap(48, 48);
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon);
        iconLabel->setAlignment(Qt::AlignCenter);
    }
    owner_.aboutIconLabel_ = iconLabel;
    iconLabel->installEventFilter(&owner_);
    titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* titleTextCol = new QVBoxLayout();
    titleTextCol->setSpacing(4);
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    QString displayVersion = QString::fromLatin1(MIACODE_DISPLAY_VERSION_STRING).trimmed();
    if (displayVersion.isEmpty()) {
        displayVersion = QCoreApplication::applicationVersion().trimmed();
    }
    if (displayVersion.isEmpty()) {
        displayVersion = QStringLiteral("0.0.0");
    }
    auto* versionLabel = new QLabel(QStringLiteral("v%1").arg(displayVersion), card);
    versionLabel->setObjectName("AboutVersion");
    titleTextCol->addWidget(titleLabel, 0, Qt::AlignLeft);
    titleTextCol->addWidget(versionLabel, 0, Qt::AlignLeft);
    titleRow->addLayout(titleTextCol, 0);
    titleRow->addStretch(1);
    cardLayout->addLayout(titleRow);

    auto* infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    auto addRow = [card, infoGrid](int row, const QString& key, const QString& value) {
        auto* k = new QLabel(key, card);
        k->setObjectName("AboutKey");
        auto* v = new QLabel(value, card);
        v->setObjectName("AboutValue");
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoGrid->addWidget(k, row, 0);
        infoGrid->addWidget(v, row, 1);
    };
    addRow(0, UiText::text(QStringLiteral("about.platform")), platform);
    addRow(1, UiText::text(QStringLiteral("about.build_type")), buildType);
    cardLayout->addLayout(infoGrid);
    rootLayout->addWidget(card);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);
    dialog.exec();
    if (owner_.aboutIconLabel_ != nullptr) {
        owner_.aboutIconLabel_->removeEventFilter(&owner_);
    }
    owner_.aboutIconLabel_.clear();
    owner_.invalidStarPreviewAboutClickCount_ = 0;
    owner_.invalidStarPreviewAboutClickElapsed_.invalidate();
}
