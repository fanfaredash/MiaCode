#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
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
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencyDetectorDialog.h"

#include <QDesktopServices>
#include <QUrl>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <cmath>

using namespace miacode::mainwindow::shared;

namespace {

bool mediaToolFileIsExecutable(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

QString resolveMediaToolFfmpegExecutable()
{
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
#endif
    const QString envPath = qEnvironmentVariable("MIACODE_FFMPEG_PATH", qEnvironmentVariable("MIACODE_FFMPEG"));
    if (mediaToolFileIsExecutable(envPath)) {
        return QDir::cleanPath(QFileInfo(envPath).absoluteFilePath());
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        appDir.filePath(ffmpegName),
        appDir.filePath(QStringLiteral("ffmpeg/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../Resources/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
    };
    for (const QString& candidate : candidates) {
        if (mediaToolFileIsExecutable(candidate)) {
            return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(ffmpegName);
    if (mediaToolFileIsExecutable(fromPath)) {
        return QDir::cleanPath(QFileInfo(fromPath).absoluteFilePath());
    }
    return QString();
}

bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    QFile::remove(destinationPath);
    if (!QFile::copy(sourcePath, destinationPath)) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("无法写入文件：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。").arg(destinationPath)
                : QStringLiteral("Failed to write file: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program.").arg(destinationPath);
        }
        return false;
    }
    return true;
}

bool restoreFileFromBackup(const QString& backupPath, const QString& destinationPath, QString* error)
{
    if (!QFileInfo::exists(backupPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("Backup file was not found: %1").arg(backupPath);
        }
        return false;
    }
    if (!copyFileReplacing(backupPath, destinationPath, error)) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("还原备份失败：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。").arg(destinationPath)
                : QStringLiteral("Failed to restore backup to: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program.").arg(destinationPath);
        }
        return false;
    }
    return true;
}

int mediaBlankClockCountFromFields(const QVector<SimaiRawField>& fields)
{
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) != 0
            && field.key.compare(QStringLiteral("clockcount"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        const int value = field.value.trimmed().toInt(&ok);
        if (ok && value > 0) {
            return value;
        }
        return 0;
    }
    return 0;
}

int mediaBlankBeatsFromMeterId(const QString& meterId)
{
    bool ok = false;
    const int slash = meterId.indexOf(QChar('/'));
    const int numerator = slash > 0 ? meterId.left(slash).toInt(&ok) : 0;
    return ok && numerator > 0 ? numerator : 4;
}

bool replaceFileWithTemp(const QString& tempPath, const QString& destinationPath, QString* error)
{
    const QString replacingPath = destinationPath + QStringLiteral(".replacing");
    QFile::remove(replacingPath);
    if (!QFile::rename(destinationPath, replacingPath)) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("无法替换原文件：%1\n\n文件可能仍被预览、播放器、资源管理器预览窗格或其他程序占用。请停止预览并关闭占用该文件的程序后重试。").arg(destinationPath)
                : QStringLiteral("Failed to stage original file for replacement: %1\n\nThe file may still be open in preview, a media player, File Explorer preview pane, or another program. Stop preview and close programs using it, then try again.").arg(destinationPath);
        }
        return false;
    }
    if (!QFile::rename(tempPath, destinationPath)) {
        QFile::rename(replacingPath, destinationPath);
        if (error != nullptr) {
            *error = QStringLiteral("Failed to replace file: %1").arg(destinationPath);
        }
        return false;
    }
    QFile::remove(replacingPath);
    return true;
}

bool runFfmpegBlocking(
    const QString& ffmpegPath,
    const QStringList& args,
    QWidget* parent,
    const QString& label,
    QString* error)
{
    QProgressDialog progress(label, QString(), 0, 0, parent);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    while (!process.waitForFinished(100)) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    progress.close();

    const QString output = QString::fromLocal8Bit(process.readAll()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error != nullptr) {
            *error = output.isEmpty()
                ? QStringLiteral("ffmpeg exited with code %1.").arg(process.exitCode())
                : output.left(2000);
        }
        return false;
    }
    return true;
}

bool probeMediaDurationSeconds(const QString& ffmpegPath, const QString& mediaPath, double* durationSeconds, QString* error)
{
    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-i") << mediaPath
         << QStringLiteral("-f") << QStringLiteral("null")
         << QStringLiteral("-");

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(5000)) {
        if (error != nullptr) {
            *error = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(3000);
        if (error != nullptr) {
            *error = QStringLiteral("Timed out while probing media duration.");
        }
        return false;
    }

    const QString output = QString::fromLocal8Bit(process.readAll());
    static const QRegularExpression durationPattern(
        QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))")
    );
    const QRegularExpressionMatch match = durationPattern.match(output);
    if (!match.hasMatch()) {
        if (error != nullptr) {
            *error = QStringLiteral("Failed to read media duration.");
        }
        return false;
    }

    const double hours = match.captured(1).toDouble();
    const double minutes = match.captured(2).toDouble();
    const double seconds = match.captured(3).toDouble();
    const double totalSeconds = hours * 3600.0 + minutes * 60.0 + seconds;
    if (!(totalSeconds > 0.0)) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid media duration.");
        }
        return false;
    }
    if (durationSeconds != nullptr) {
        *durationSeconds = totalSeconds;
    }
    return true;
}

bool compressVideoUnder20Mb(
    const QString& ffmpegPath,
    const QString& videoPath,
    QWidget* parent,
    QString* error)
{
    constexpr qint64 kTargetBytes = 20LL * 1024LL * 1024LL;
    constexpr int kAudioBitrateKbps = 96;
    constexpr int kMinVideoBitrateKbps = 120;
    const QFileInfo videoInfo(videoPath);
    const qint64 originalBytes = videoInfo.size();
    if (originalBytes > 0 && originalBytes <= kTargetBytes) {
        if (error != nullptr) {
            *error = UiText::isChineseUi()
                ? QStringLiteral("当前视频已经小于 20 MiB，无需压缩。")
                : QStringLiteral("The current video is already under 20 MiB; compression is not needed.");
        }
        return false;
    }
    const QString backupPath = videoInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix())
    );
    const QString tempPath = videoInfo.dir().filePath(QStringLiteral(".miacode_video_compress_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(videoPath, backupPath, error)) {
        return false;
    }

    double durationSeconds = 0.0;
    if (!probeMediaDurationSeconds(ffmpegPath, backupPath, &durationSeconds, error)) {
        return false;
    }

    const qint64 outputTargetBytes = originalBytes > 0
        ? std::min(kTargetBytes, static_cast<qint64>(std::floor(static_cast<double>(originalBytes) * 0.86)))
        : kTargetBytes;
    const double targetBits = static_cast<double>(outputTargetBytes) * 8.0 * 0.965;
    int totalBitrateKbps = static_cast<int>(std::floor(targetBits / durationSeconds / 1000.0));
    int videoBitrateKbps = std::max(kMinVideoBitrateKbps, totalBitrateKbps - kAudioBitrateKbps);

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-map") << QStringLiteral("0:v:0")
         << QStringLiteral("-map") << QStringLiteral("0:a?")
         << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-preset") << QStringLiteral("slow")
         << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-maxrate") << QStringLiteral("%1k").arg(videoBitrateKbps)
         << QStringLiteral("-bufsize") << QStringLiteral("%1k").arg(videoBitrateKbps * 2)
         << QStringLiteral("-vf") << QStringLiteral("scale='min(1280,iw)':-2")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-c:a") << QStringLiteral("aac")
         << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(kAudioBitrateKbps)
         << QStringLiteral("-movflags") << QStringLiteral("+faststart")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在压缩视频...") : QStringLiteral("Compressing video..."),
            error)) {
        QFile::remove(tempPath);
        return false;
    }

    const qint64 compressedBytes = QFileInfo(tempPath).size();
    if (compressedBytes <= 0 || compressedBytes > kTargetBytes || (originalBytes > 0 && compressedBytes >= originalBytes)) {
        QFile::remove(tempPath);
        if (error != nullptr) {
            *error = compressedBytes > kTargetBytes
                ? QStringLiteral("Compressed video is still larger than 20 MiB.")
                : QStringLiteral("Compressed video was not smaller than the original file.");
        }
        return false;
    }
    return replaceFileWithTemp(tempPath, videoPath, error);
}

bool convertTrackTo44100Hz(
    const QString& ffmpegPath,
    const QString& trackPath,
    QWidget* parent,
    QString* error)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_44100_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) {
        return false;
    }

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-vn")
         << QStringLiteral("-ar") << QStringLiteral("44100")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("2")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理音频...") : QStringLiteral("Processing audio..."),
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependTrackSilence(
    const QString& ffmpegPath,
    const QString& trackPath,
    double silenceSeconds,
    QWidget* parent,
    QString* error)
{
    const QFileInfo trackInfo(trackPath);
    const QString backupPath = trackInfo.dir().filePath(QStringLiteral("track_bak.mp3"));
    const QString tempPath = trackInfo.dir().filePath(QStringLiteral(".miacode_track_prepend_tmp.mp3"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(trackPath, backupPath, error)) {
        return false;
    }

    const QString silenceDuration = QString::number(silenceSeconds, 'f', 6);
    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("lavfi")
         << QStringLiteral("-i") << QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=44100:d=%1").arg(silenceDuration)
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-filter_complex")
         << QStringLiteral("[0:a]atrim=duration=%1,asetpts=PTS-STARTPTS[s];[1:a]aresample=44100,aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS[a];[s][a]concat=n=2:v=0:a=1[out]").arg(silenceDuration)
         << QStringLiteral("-map") << QStringLiteral("[out]")
         << QStringLiteral("-c:a") << QStringLiteral("libmp3lame")
         << QStringLiteral("-q:a") << QStringLiteral("2")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理 track.mp3...") : QStringLiteral("Processing track.mp3..."),
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, trackPath, error);
}

bool prependPvBlack(
    const QString& ffmpegPath,
    const QString& pvPath,
    double silenceSeconds,
    QWidget* parent,
    QString* error)
{
    const QFileInfo pvInfo(pvPath);
    const QString backupPath = pvInfo.dir().filePath(
        QStringLiteral("%1_bak.%2").arg(pvInfo.completeBaseName(), pvInfo.suffix())
    );
    const QString tempPath = pvInfo.dir().filePath(QStringLiteral(".miacode_pv_prepend_tmp.mp4"));
    QFile::remove(tempPath);
    if (!copyFileReplacing(pvPath, backupPath, error)) {
        return false;
    }

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("lavfi")
         << QStringLiteral("-t") << QString::number(silenceSeconds, 'f', 6)
         << QStringLiteral("-i") << QStringLiteral("color=c=black:s=1920x1080:r=30")
         << QStringLiteral("-i") << backupPath
         << QStringLiteral("-filter_complex")
         << QStringLiteral("[0:v]format=yuv420p[v0];[1:v]scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p[v1];[v0][v1]concat=n=2:v=1:a=0[v]")
         << QStringLiteral("-map") << QStringLiteral("[v]")
         << QStringLiteral("-an")
         << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-preset") << QStringLiteral("veryfast")
         << QStringLiteral("-crf") << QStringLiteral("18")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-movflags") << QStringLiteral("+faststart")
         << tempPath;
    if (!runFfmpegBlocking(
            ffmpegPath,
            args,
            parent,
            UiText::isChineseUi() ? QStringLiteral("正在处理 pv.mp4...") : QStringLiteral("Processing pv.mp4..."),
            error)) {
        QFile::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, pvPath, error);
}

} // namespace

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
    owner_.clearPreviewStageMediaRoute();
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
            owner_.previewSfxRuntime_->setChartPath(QString());
            owner_.previewSfxRuntime_->setChartPath(owner_.currentFilePath_);
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
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void MainWindow::DialogsSection::updateLatencyDetectorAvailability()
{
    const bool enabled = !resolveLatencyDetectorTrackPath().isEmpty();
    if (owner_.latencyDetectorAction_ != nullptr) {
        owner_.latencyDetectorAction_->setEnabled(enabled);
    }
    if (owner_.latencyDetectorButton_ != nullptr) {
        owner_.latencyDetectorButton_->setEnabled(enabled);
    }
}

void MainWindow::DialogsSection::onPreviewAudioSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewAudioSettings");
    openPreviewSettingsDialog(
        true,
        false,
        uiText("dialog.audio_settings.title", "Audio Settings")
    );
}

void MainWindow::DialogsSection::onPreviewVideoSettings()
{
    MC_OP("MainWindow::DialogsSection::onPreviewVideoSettings");
    openPreviewSettingsDialog(
        false,
        true,
        uiText("dialog.video_settings.title", "Preview Settings")
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
    dialog.setWindowTitle(uiText("action.about", "About"));
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
    addRow(0, uiText("about.platform", "Release Platform"), platform);
    addRow(1, uiText("about.build_type", "Build Type"), buildType);
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

void MainWindow::DialogsSection::onOpenLatencyDetector()
{
    MC_OP("MainWindow::DialogsSection::onOpenLatencyDetector");
    const QString trackPath = resolveLatencyDetectorTrackPath();
    _mc_op_.note(QStringLiteral("track=%1").arg(trackPath));
    bool wholeBpmOk = false;
    const double wholeBpm = owner_.parsedWholeBpm(&wholeBpmOk);
    const QString meterId = owner_.parsedLatencyMeterId();
    const double offsetSeconds = owner_.parsedRawFirstSeconds();
    if (trackPath.isEmpty()) {
        owner_.statusBar()->showMessage(UiText::isChineseUi()
            ? QStringLiteral("当前谱面目录缺少 track.mp3，无法打开BPM&偏移检测。")
            : QStringLiteral("track.mp3 was not found next to the current chart."));
        updateLatencyDetectorAvailability();
        return;
    }

    if (owner_.latencyDetectorDialog_ != nullptr) {
        if (owner_.latencyDetectorDialog_->trackPath() == trackPath) {
            owner_.latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
            owner_.latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
            owner_.latencyDetectorDialog_->setMeterId(meterId);
            owner_.latencyDetectorDialog_->raise();
            owner_.latencyDetectorDialog_->activateWindow();
            return;
        }
        owner_.latencyDetectorDialog_->close();
        owner_.latencyDetectorDialog_.clear();
    }

    owner_.latencyDetectorDialog_ = new LatencyDetectorDialog(
        trackPath,
        owner_.currentFilePath_,
        owner_.ensureWaveformCacheService(),
        owner_.previewAudioSettings_,
        UiDialogs::effectiveParentWidget(&owner_)
    );
    owner_.windowSection_->applySystemWindowBackdrop(owner_.latencyDetectorDialog_);
    UiDialogs::prepareDialogWindow(
        owner_.latencyDetectorDialog_,
        &owner_,
        true,
        UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls
    );
    owner_.latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
    owner_.latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
    owner_.latencyDetectorDialog_->setMeterId(meterId);
    connect(owner_.latencyDetectorDialog_, &LatencyDetectorDialog::offsetChanged, &owner_, [this](double seconds) {
        owner_.applyLatencyDetectorOffset(seconds);
    });
    connect(owner_.latencyDetectorDialog_, &LatencyDetectorDialog::bpmChanged, &owner_, [this](double bpm) {
        owner_.applyLatencyDetectorBpm(bpm);
    });
    connect(owner_.latencyDetectorDialog_, &QObject::destroyed, &owner_, [this]() {
        owner_.latencyDetectorDialog_.clear();
    });
    owner_.latencyDetectorDialog_->show();
    owner_.latencyDetectorDialog_->raise();
    owner_.latencyDetectorDialog_->activateWindow();
}

void MainWindow::DialogsSection::onPrependTrackSilence()
{
    onPrependMediaBlank(MediaBlankTarget::Track);
}

void MainWindow::DialogsSection::onPrependPvBlack()
{
    onPrependMediaBlank(MediaBlankTarget::Pv);
}

void MainWindow::DialogsSection::onCompressBackgroundVideo()
{
    MC_OP("MainWindow::DialogsSection::onCompressBackgroundVideo");
    const QString title = UiText::isChineseUi() ? QStringLiteral("视频压缩") : QStringLiteral("Compress Video");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(owner_.currentFilePath_, owner_.document_.videoPath);
    if (!QFileInfo::exists(videoPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少背景视频 .mp4。")
                : QStringLiteral("No background .mp4 video was found next to the current chart.")
        );
        return;
    }

    const QFileInfo videoInfo(videoPath);
    const QString backupName = QStringLiteral("%1_bak.%2").arg(videoInfo.completeBaseName(), videoInfo.suffix());
    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("将压缩 %1 到 20M 内，并生成/覆盖备份 %2。是否继续？").arg(videoInfo.fileName(), backupName)
                : QStringLiteral("Compress %1 under 20 MiB and create/replace backup %2?").arg(videoInfo.fileName(), backupName)
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    if (!compressVideoUnder20Mb(ffmpegPath, videoPath, UiDialogs::effectiveParentWidget(&owner_), &error)) {
        QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        return;
    }
    reloadPreviewMediaAfterFileOperation(false);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已压缩 %1 到 20M 内。").arg(videoInfo.fileName())
            : QStringLiteral("Compressed %1 under 20 MiB.").arg(videoInfo.fileName()),
        6000
    );
}

void MainWindow::DialogsSection::onConvertTrackTo44100Hz()
{
    MC_OP("MainWindow::DialogsSection::onConvertTrackTo44100Hz");
    const QString title = UiText::isChineseUi() ? QStringLiteral("采样率转换") : QStringLiteral("Sample Rate");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QString trackPath = QDir(chartDirPath).filePath(QStringLiteral("track.mp3"));
    if (!QFileInfo::exists(trackPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return;
    }

    if (QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("将 track.mp3 处理为 44100Hz，并生成/覆盖备份 track_bak.mp3。是否继续？")
                : QStringLiteral("Convert track.mp3 to 44100 Hz and create/replace backup track_bak.mp3?")
        ) != QMessageBox::Yes) {
        return;
    }

    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    if (!convertTrackTo44100Hz(ffmpegPath, trackPath, UiDialogs::effectiveParentWidget(&owner_), &error)) {
        QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
        return;
    }
    reloadPreviewMediaAfterFileOperation(true);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已将 track.mp3 处理为 44100Hz。")
            : QStringLiteral("Converted track.mp3 to 44100 Hz."),
        6000
    );
}

namespace {

// Shared helper for the title/artist buttons. Resolves track.mp3, reads
// its ID3v2 tag, and returns the requested field's value. Reports any
// failure through QMessageBox at `parent`; on success returns the
// trimmed field value, or an empty string if the tag exists but the
// requested field is blank (the caller decides whether to warn).
enum class TrackTagField {
    Title,
    Artist,
};

QString readTrackTagField(
    const QString& trackPath,
    TrackTagField field,
    QWidget* parent,
    const QString& dialogTitle)
{
    if (trackPath.isEmpty()) {
        QMessageBox::warning(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return QString();
    }
    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid) {
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("没能在 track.mp3 中读取到 ID3v2 标签。")
                : QStringLiteral("No ID3v2 tag was found in track.mp3.")
        );
        return QString();
    }
    const QString value = (field == TrackTagField::Title ? tag.title : tag.artist).trimmed();
    if (value.isEmpty()) {
        const QString fieldLabelZh = (field == TrackTagField::Title)
            ? QStringLiteral("标题") : QStringLiteral("曲师");
        const QString fieldLabelEn = (field == TrackTagField::Title)
            ? QStringLiteral("title") : QStringLiteral("artist");
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("track.mp3 的 ID3 标签里没有%1信息。").arg(fieldLabelZh)
                : QStringLiteral("track.mp3's ID3 tag carries no %1.").arg(fieldLabelEn)
        );
        return QString();
    }
    return value;
}

}  // namespace

void MainWindow::DialogsSection::onReadTitleFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadTitleFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取标题")
        : QStringLiteral("Read Title from MP3");
    const QString trackPath = resolveLatencyDetectorTrackPath();
    const QString value = readTrackTagField(
        trackPath, TrackTagField::Title, UiDialogs::effectiveParentWidget(&owner_), title);
    if (value.isEmpty() || ui_.titleEdit_ == nullptr) {
        return;
    }
    // setText() fires QLineEdit::textChanged, which the FrameBootstrap
    // wiring already routes through markCurrentFieldDirty() — no extra
    // dirty-tracking call needed here.
    ui_.titleEdit_->setText(value);
    _mc_op_.note(QStringLiteral("track=%1 title=%2").arg(trackPath, value));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已从 track.mp3 读取标题。")
            : QStringLiteral("Loaded title from track.mp3."),
        6000
    );
}

void MainWindow::DialogsSection::onReadArtistFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadArtistFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取曲师")
        : QStringLiteral("Read Artist from MP3");
    const QString trackPath = resolveLatencyDetectorTrackPath();
    const QString value = readTrackTagField(
        trackPath, TrackTagField::Artist, UiDialogs::effectiveParentWidget(&owner_), title);
    if (value.isEmpty() || ui_.artistEdit_ == nullptr) {
        return;
    }
    ui_.artistEdit_->setText(value);
    _mc_op_.note(QStringLiteral("track=%1 artist=%2").arg(trackPath, value));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已从 track.mp3 读取曲师。")
            : QStringLiteral("Loaded artist from track.mp3."),
        6000
    );
}

void MainWindow::DialogsSection::onExtractBackgroundFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onExtractBackgroundFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("提取封面为 bg.jpg")
        : QStringLiteral("Extract Cover to bg.jpg");
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }
    const QString trackPath = resolveLatencyDetectorTrackPath();
    if (trackPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 track.mp3。")
                : QStringLiteral("track.mp3 was not found next to the current chart.")
        );
        return;
    }

    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid || tag.pictureBytes.isEmpty()) {
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("track.mp3 中没有内嵌的封面图。")
                : QStringLiteral("track.mp3 has no embedded cover artwork.")
        );
        return;
    }

    // Decode the APIC payload via QImage so we can re-encode to JPEG
    // regardless of the embedded format (PNG, WebP, etc.). Writing the
    // raw bytes verbatim would be slightly higher quality but would
    // require renaming bg.jpg when the source isn't JPEG, which doesn't
    // match the "always bg.jpg" naming the user asked for.
    QImage cover;
    if (!cover.loadFromData(tag.pictureBytes)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("内嵌封面解码失败（MIME=%1）。").arg(tag.pictureMimeType)
                : QStringLiteral("Failed to decode embedded cover (MIME=%1).").arg(tag.pictureMimeType)
        );
        return;
    }

    const QString bgPath = QDir(chartDirPath).filePath(QStringLiteral("bg.jpg"));
    const bool existed = QFileInfo::exists(bgPath);
    if (existed) {
        const auto answer = QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("bg.jpg 已经存在，是否覆盖？")
                : QStringLiteral("bg.jpg already exists. Overwrite?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    // Drop the preview's grip on the bg file before overwriting it so
    // Windows doesn't reject the write with a sharing-violation error,
    // mirroring the pattern used by the track/video file operations.
    releasePreviewMediaForFileOperation();

    if (!cover.save(bgPath, "JPG", 92)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("写入 bg.jpg 失败。")
                : QStringLiteral("Failed to write bg.jpg.")
        );
        // Still try to reload — the previous file (if any) is back.
        reloadPreviewMediaAfterFileOperation(false);
        return;
    }

    // reloadPreviewMediaAfterFileOperation re-runs the chart-path
    // resolution pipeline; PreviewStageMediaHost::setChartPath was
    // cleared by releasePreviewMediaForFileOperation above, so the
    // re-resolution lands on the brand-new bg.jpg even if the chart
    // path itself didn't change.
    reloadPreviewMediaAfterFileOperation(false);
    _mc_op_.note(QStringLiteral("bg=%1 replaced=%2 source_mime=%3 source_bytes=%4")
                     .arg(bgPath)
                     .arg(existed ? 1 : 0)
                     .arg(tag.pictureMimeType)
                     .arg(tag.pictureBytes.size()));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? (existed
                   ? QStringLiteral("已覆盖 bg.jpg（来源：track.mp3 内嵌封面）。")
                   : QStringLiteral("已生成 bg.jpg（来源：track.mp3 内嵌封面）。"))
            : (existed
                   ? QStringLiteral("Overwrote bg.jpg with embedded cover from track.mp3.")
                   : QStringLiteral("Wrote bg.jpg from track.mp3's embedded cover.")),
        6000
    );
}

void MainWindow::DialogsSection::onPrependMediaBlank(MediaBlankTarget target)
{
    MC_OP("MainWindow::DialogsSection::onPrependMediaBlank");
    const bool isTrack = target == MediaBlankTarget::Track;
    const QString title = isTrack
        ? (UiText::isChineseUi() ? QStringLiteral("音频开头静音处理") : QStringLiteral("Prepend Track Silence"))
        : (UiText::isChineseUi() ? QStringLiteral("视频开头黑幕处理") : QStringLiteral("Prepend PV Black Screen"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("请先打开或保存一个谱面文件。")
                : QStringLiteral("Open or save a chart file first.")
        );
        return;
    }

    const QDir chartDir(chartDirPath);
    const QString trackPath = chartDir.filePath(QStringLiteral("track.mp3"));
    const QString videoPath = miacode::chart_assets::resolveChartVideoPath(owner_.currentFilePath_, owner_.document_.videoPath);
    const QString inputPath = isTrack ? trackPath : videoPath;
    const QFileInfo inputInfo(inputPath);
    const QString inputName = isTrack ? QStringLiteral("track.mp3") : inputInfo.fileName();
    const QString backupName = isTrack
        ? QStringLiteral("track_bak.mp3")
        : QStringLiteral("%1_bak.%2").arg(inputInfo.completeBaseName(), inputInfo.suffix());
    const QString backupPath = inputPath.isEmpty()
        ? QString()
        : inputInfo.dir().filePath(backupName);
    if (!QFileInfo::exists(inputPath)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("当前谱面目录缺少 %1。").arg(isTrack ? inputName : QStringLiteral("背景视频 .mp4"))
                : QStringLiteral("%1 was not found next to the current chart.").arg(isTrack ? inputName : QStringLiteral("background .mp4 video"))
        );
        return;
    }

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* beatsSpin = new QDoubleSpinBox(&dialog);
    beatsSpin->setRange(0.125, 512.0);
    beatsSpin->setDecimals(3);
    beatsSpin->setSingleStep(1.0);
    auto* bpmSpin = new QDoubleSpinBox(&dialog);
    bpmSpin->setRange(1.0, 999.0);
    bpmSpin->setDecimals(3);
    bpmSpin->setSingleStep(1.0);
    const QVector<SimaiRawField> extraFields = SimaiDocument::parseRawFields(
        ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    const QString chartText = owner_.activeChartText();
    const auto detectedBeats = [extraFields]() {
        const int clockCount = mediaBlankClockCountFromFields(extraFields);
        return clockCount > 0 ? clockCount : 4;
    };
    const auto detectedBpm = [extraFields, chartText]() {
        const double wholeBpm = miacode::chart_clock::wholeBpmFromFields(extraFields);
        if (wholeBpm > 0.0) {
            return wholeBpm;
        }
        const double chartBpm = miacode::chart_clock::firstBpmFromChart(chartText);
        return chartBpm > 0.0 ? chartBpm : miacode::chart_clock::kFallbackClockBpm;
    };
    const auto analyzeTrackBpmAndMeter = [this]() -> QPair<double, QString> {
        LatencyDetectorDialog detector(
            miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_),
            owner_.currentFilePath_,
            owner_.ensureWaveformCacheService(),
            owner_.previewAudioSettings_,
            UiDialogs::effectiveParentWidget(&owner_)
        );
        double bpm = 0.0;
        QString meterId;
        if (!detector.detectBpmAndMeter(&bpm, &meterId)) {
            return {0.0, QString()};
        }
        return {bpm, meterId};
    };
    beatsSpin->setValue(detectedBeats());
    bpmSpin->setValue(detectedBpm());
    auto* beatsRow = new QWidget(&dialog);
    auto* beatsRowLayout = new QHBoxLayout(beatsRow);
    beatsRowLayout->setContentsMargins(0, 0, 0, 0);
    beatsRowLayout->setSpacing(6);
    beatsRowLayout->addWidget(beatsSpin, 1);
    auto* detectBeatsButton = new QPushButton(UiText::isChineseUi() ? QStringLiteral("自动检测") : QStringLiteral("Detect"), beatsRow);
    beatsRowLayout->addWidget(detectBeatsButton, 0);
    auto* bpmRow = new QWidget(&dialog);
    auto* bpmRowLayout = new QHBoxLayout(bpmRow);
    bpmRowLayout->setContentsMargins(0, 0, 0, 0);
    bpmRowLayout->setSpacing(6);
    bpmRowLayout->addWidget(bpmSpin, 1);
    auto* detectBpmButton = new QPushButton(UiText::isChineseUi() ? QStringLiteral("自动检测") : QStringLiteral("Detect"), bpmRow);
    bpmRowLayout->addWidget(detectBpmButton, 0);
    const auto applyDetectedBeats = [beatsSpin, analyzeTrackBpmAndMeter, detectedBeats]() {
        const QPair<double, QString> detected = analyzeTrackBpmAndMeter();
        beatsSpin->setValue(detected.second.isEmpty() ? detectedBeats() : mediaBlankBeatsFromMeterId(detected.second));
    };
    const auto applyDetectedBpm = [bpmSpin, analyzeTrackBpmAndMeter, detectedBpm]() {
        const QPair<double, QString> detected = analyzeTrackBpmAndMeter();
        bpmSpin->setValue(detected.first > 0.0 ? detected.first : detectedBpm());
    };
    connect(detectBeatsButton, &QPushButton::clicked, &dialog, applyDetectedBeats);
    connect(detectBpmButton, &QPushButton::clicked, &dialog, applyDetectedBpm);
    form->addRow(UiText::isChineseUi() ? QStringLiteral("拍数") : QStringLiteral("Beats"), beatsRow);
    form->addRow(QStringLiteral("BPM"), bpmRow);
    layout->addLayout(form);

    auto* hint = new QLabel(QStringLiteral("%1 -> %2").arg(inputName, backupName), &dialog);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(hint);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* restoreButton = buttonBox->addButton(
        UiText::isChineseUi() ? QStringLiteral("还原备份") : QStringLiteral("Restore Backup"),
        QDialogButtonBox::ActionRole
    );
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(restoreButton, &QPushButton::clicked, &dialog, [backupPath, inputPath, isTrack, title, this]() {
        releasePreviewMediaForFileOperation();
        QString error;
        if (!restoreFileFromBackup(backupPath, inputPath, &error)) {
            QMessageBox::critical(UiDialogs::effectiveParentWidget(&owner_), title, error);
            return;
        }
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi() ? QStringLiteral("已还原备份。") : QStringLiteral("Backup restored.")
        );
        reloadPreviewMediaAfterFileOperation(isTrack);
    });
    layout->addWidget(buttonBox, 0, Qt::AlignRight);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const double silenceSeconds = beatsSpin->value() * 60.0 / bpmSpin->value();
    const QString ffmpegPath = resolveMediaToolFfmpegExecutable();
    if (ffmpegPath.isEmpty()) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。")
                : QStringLiteral("ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH.")
        );
        return;
    }

    releasePreviewMediaForFileOperation();

    QString error;
    const QWidget* parent = UiDialogs::effectiveParentWidget(&owner_);
    if (isTrack && !prependTrackSilence(ffmpegPath, trackPath, silenceSeconds, const_cast<QWidget*>(parent), &error)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            UiText::isChineseUi() ? QStringLiteral("track.mp3 处理失败") : QStringLiteral("track.mp3 Failed"),
            error
        );
        return;
    }
    if (!isTrack && !prependPvBlack(ffmpegPath, videoPath, silenceSeconds, const_cast<QWidget*>(parent), &error)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            UiText::isChineseUi() ? QStringLiteral("视频处理失败") : QStringLiteral("Video Failed"),
            error
        );
        return;
    }

    reloadPreviewMediaAfterFileOperation(isTrack);
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已为 %1 开头添加 %2 秒空白。").arg(inputName).arg(silenceSeconds, 0, 'f', 3)
            : QStringLiteral("Prepended %1 seconds of blank media to %2.").arg(silenceSeconds, 0, 'f', 3).arg(inputName),
        6000
    );
}

void MainWindow::DialogsSection::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    owner_.previewAudioSettings_.normalize();

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(520);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    const auto createDialogMenuButton = [](QWidget* parent, const QString& text) {
        auto* button = new QToolButton(parent);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        return button;
    };
    const auto flowSpeedValueLabel = [](double flowSpeed) {
        const double snapped = qRound(flowSpeed * 4.0) / 4.0;
        const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
        const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
        return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
    };
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered, bool italic = false) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        QFont buttonFont = button->font();
        buttonFont.setItalic(italic);
        button->setFont(buttonFont);
        button->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton {"
                " color: %1;"
                " background: transparent;"
                " border: none;"
                " padding: 6px 20px 6px 12px;"
                " text-align: left;"
                "}"
                "QToolButton:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
            if (onTriggered) {
                onTriggered();
            }
            action->trigger();
            menu->close();
        });
        action->setDefaultWidget(button);
        menu->addAction(action);
    };

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* audioGroup = new QGroupBox(uiText("dialog.render_settings.audio_group", "Audio"), &dialog);
    auto* audioFormLayout = new QFormLayout(audioGroup);
    audioFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    audioFormLayout->setHorizontalSpacing(10);
    audioFormLayout->setVerticalSpacing(8);

    const auto addAudioRow = [&](const QString& labelText,
                                 int valuePercent,
                                 QSlider** sliderOut,
                                 QLabel** labelOut,
                                 QToolButton** muteButtonOut = nullptr,
                                 int maximumPercent = 100) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, maximumPercent);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        QToolButton* muteButton = nullptr;
        if (muteButtonOut != nullptr) {
            muteButton = new QToolButton(row);
            muteButton->setCursor(Qt::PointingHandCursor);
            muteButton->setAutoRaise(true);
            muteButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            muteButton->setFixedSize(18, 18);
            muteButton->setIconSize(QSize(14, 14));
        }
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        if (muteButton != nullptr) {
            rowLayout->addWidget(muteButton, 0);
        }
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
        if (muteButtonOut != nullptr) {
            *muteButtonOut = muteButton;
        }
    };

    const QString masterAudioLabelText = uiText("dialog.render_settings.audio.global", "Global Volume");
    QSlider* masterSlider = nullptr;
    QLabel* masterLabel = nullptr;
    QToolButton* masterMuteButton = nullptr;
    addAudioRow(
        masterAudioLabelText,
        owner_.previewAudioSettings_.globalPercent(),
        &masterSlider,
        &masterLabel,
        &masterMuteButton,
        100
    );
    auto* audioSeparator = new QFrame(audioGroup);
    audioSeparator->setFrameShape(QFrame::HLine);
    audioSeparator->setFrameShadow(QFrame::Plain);
    audioSeparator->setLineWidth(1);
    audioSeparator->setStyleSheet(
        QStringLiteral("color: %1; background: %1;")
            .arg(UiTheme::colors().textInverse.name(QColor::HexRgb)));
    audioFormLayout->addRow(QString(), audioSeparator);

    const QString bgmAudioLabelText = uiText("dialog.render_settings.audio.track", "Track Volume");
    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    QToolButton* bgmMuteButton = nullptr;
    addAudioRow(bgmAudioLabelText, owner_.previewAudioSettings_.trackPercent(), &bgmSlider, &bgmLabel, &bgmMuteButton);
    const QString answerAudioLabelText = uiText("dialog.render_settings.audio.answer", "Answer Volume");
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    QToolButton* answerMuteButton = nullptr;
    addAudioRow(
        answerAudioLabelText,
        owner_.previewAudioSettings_.answerPercent(),
        &answerSlider,
        &answerLabel,
        &answerMuteButton
    );
    const QString judgeAudioLabelText = uiText("dialog.render_settings.audio.tap", "Tap Volume");
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    QToolButton* judgeMuteButton = nullptr;
    addAudioRow(judgeAudioLabelText, owner_.previewAudioSettings_.tapPercent(), &judgeSlider, &judgeLabel, &judgeMuteButton);
    const QString exAudioLabelText = uiText("dialog.render_settings.audio.ex", "EX Volume");
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    QToolButton* exMuteButton = nullptr;
    addAudioRow(exAudioLabelText, owner_.previewAudioSettings_.exPercent(), &exSlider, &exLabel, &exMuteButton);
    const QString breakAudioLabelText = uiText("dialog.render_settings.audio.break", "Break Volume");
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    QToolButton* breakMuteButton = nullptr;
    addAudioRow(breakAudioLabelText, owner_.previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel, &breakMuteButton);
    const QString breakSlideAudioLabelText = uiText("dialog.render_settings.audio.break_slide", "Break Slide Volume");
    QSlider* breakSlideSlider = nullptr;
    QLabel* breakSlideLabel = nullptr;
    QToolButton* breakSlideMuteButton = nullptr;
    addAudioRow(
        breakSlideAudioLabelText,
        owner_.previewAudioSettings_.breakSlidePercent(),
        &breakSlideSlider,
        &breakSlideLabel,
        &breakSlideMuteButton
    );
    const QString slideAudioLabelText = uiText("dialog.render_settings.audio.slide", "Slide Volume");
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    QToolButton* slideMuteButton = nullptr;
    addAudioRow(slideAudioLabelText, owner_.previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel, &slideMuteButton);
    auto* breakSlideTailCheerCheck = new QCheckBox(
        uiText("dialog.render_settings.audio.break_slide_tail_cheer_mute", "Disable breakslide tail cheer"),
        audioGroup);
    breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
    const QString touchAudioLabelText = uiText("dialog.render_settings.audio.touch", "Touch Volume");
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    QToolButton* touchMuteButton = nullptr;
    addAudioRow(touchAudioLabelText, owner_.previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel, &touchMuteButton);
    const QString fireworkAudioLabelText = uiText("dialog.render_settings.audio.firework", "Firework Volume");
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    QToolButton* fireworkMuteButton = nullptr;
    addAudioRow(
        fireworkAudioLabelText,
        owner_.previewAudioSettings_.fireworkPercent(),
        &fireworkSlider,
        &fireworkLabel,
        &fireworkMuteButton
    );
    audioFormLayout->addRow(QString(), breakSlideTailCheerCheck);

    const auto addVideoSliderRow = [](
        QWidget* parent,
        int minimum,
        int maximum,
        int step,
        int value,
        const QString& suffix,
        QSlider** sliderOut,
        QLabel** labelOut
    ) {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(value);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);
    auto* gameplayGroup = new QGroupBox(uiText("dialog.render_settings.gameplay_group", "Gameplay"), &dialog);
    auto* gameplayLayout = new QGridLayout(gameplayGroup);
    gameplayLayout->setContentsMargins(10, 8, 10, 8);
    gameplayLayout->setHorizontalSpacing(10);
    gameplayLayout->setVerticalSpacing(8);
    gameplayLayout->setColumnStretch(0, 1);
    gameplayLayout->setColumnStretch(1, 1);

    QSlider* outerBrightnessSlider = nullptr;
    QLabel* outerBrightnessLabel = nullptr;
    QWidget* outerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessOuter_ * 100.0),
        QStringLiteral("%"),
        &outerBrightnessSlider,
        &outerBrightnessLabel
    );
    QSlider* innerBrightnessSlider = nullptr;
    QLabel* innerBrightnessLabel = nullptr;
    QWidget* innerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessInner_ * 100.0),
        QStringLiteral("%"),
        &innerBrightnessSlider,
        &innerBrightnessLabel
    );
    QSlider* layoutSquareScaleSlider = nullptr;
    QLabel* layoutSquareScaleLabel = nullptr;
    QWidget* layoutSquareScaleRow = addVideoSliderRow(
        videoGroup,
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(owner_.previewLayoutSquareScale_ * 100.0),
        QStringLiteral("%"),
        &layoutSquareScaleSlider,
        &layoutSquareScaleLabel
    );
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const auto snapFlowSpeed = [flowSpeedMin, flowSpeedMax, flowSpeedStep](double flowSpeed) {
        return qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
    };
    double selectedTapFlowSpeed = snapFlowSpeed(owner_.previewTapFlowSpeed_);
    double selectedTouchFlowSpeed = snapFlowSpeed(owner_.previewTouchFlowSpeed_);
    const auto createFlowSpeedEdit = [&](double& selectedFlowSpeed, const std::function<void(double)>& applyFlowSpeed) {
        auto* flowSpeedEdit = new QLineEdit(gameplayGroup);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
        flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
        flowSpeedEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
        flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowSpeedValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, &dialog, [&, flowSpeedEdit, applyFlowSpeed]() {
            bool ok = false;
            const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
            if (!ok) {
                flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
                return;
            }
            selectedFlowSpeed = snapFlowSpeed(typedSpeed);
            flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
            applyFlowSpeed(selectedFlowSpeed);
            owner_.savePortableState();
        });
        return flowSpeedEdit;
    };
    auto* tapFlowSpeedEdit = createFlowSpeedEdit(selectedTapFlowSpeed, [this](double flowSpeed) {
        owner_.previewTapFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTapFlowSpeed(flowSpeed);
        }
    });
    auto* touchFlowSpeedEdit = createFlowSpeedEdit(selectedTouchFlowSpeed, [this](double flowSpeed) {
        owner_.previewTouchFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTouchFlowSpeed(flowSpeed);
        }
    });

    struct CanvasFrameRateOption {
        PreviewCanvasFrameRateMode mode;
        QString label;
    };
    const double detectedRefreshRate = owner_.currentPreviewCanvasRefreshRate();
    const QString displayRefreshLabel = QStringLiteral("%1 (%2 Hz)")
        .arg(uiText(
            "dialog.render_settings.preview.canvas_frame_rate.display",
            "Display Refresh Rate"
        ))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    QList<CanvasFrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"),
    });
    // Only expose the 120 FPS option on a display that can sustain it.
    // The backend clamps Fps120 to display refresh at runtime (see
    // previewCanvasTargetFrameIntervalNs), so leaving it in the menu
    // would advertise a setting that silently degrades to display
    // refresh — confusing the user. Threshold uses an epsilon (119.5)
    // so panels that report 119.88 Hz (common OEM round-down of true
    // 120 Hz) still see the option.
    if (detectedRefreshRate >= 119.5) {
        canvasFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"),
        });
    }
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    QString selectedCanvasFrameRateLabel = canvasFrameRateOptions.front().label;
    bool foundExactSelectedFrameRate = false;
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        if (option.mode == owner_.previewCanvasFrameRateMode_) {
            selectedCanvasFrameRateLabel = option.label;
            foundExactSelectedFrameRate = true;
            break;
        }
    }
    // Saved mode is no longer offered (user previously had Fps120 on a
    // higher-refresh display, then connected to a lower one). Show the
    // DisplayRefresh label since that's what the backend actually
    // applies at runtime — the saved value itself is preserved so
    // reconnecting to a 120 Hz panel restores the original selection.
    if (!foundExactSelectedFrameRate) {
        for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
            if (option.mode == PreviewCanvasFrameRateMode::DisplayRefresh) {
                selectedCanvasFrameRateLabel = option.label;
                break;
            }
        }
    }
    auto* canvasFrameRateButton = createDialogMenuButton(videoGroup, selectedCanvasFrameRateLabel);
    canvasFrameRateButton->setFixedHeight(tapFlowSpeedEdit->sizeHint().height());
    canvasFrameRateButton->setStyleSheet(
        UiTheme::dialogMenuButtonStyleSheet()
        + QStringLiteral("QToolButton { text-align: center; padding: 2px 22px 2px 10px; }")
    );
    auto* canvasFrameRateMenu = new QMenu(canvasFrameRateButton);
    styleRoundedMenu(*canvasFrameRateMenu);
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        const PreviewCanvasFrameRateMode mode = option.mode;
        const QString label = option.label;
        addDialogMenuChoice(canvasFrameRateMenu, label, [this, canvasFrameRateButton, mode, label]() {
            canvasFrameRateButton->setText(label);
            owner_.setPreviewCanvasFrameRateMode(mode, true);
        });
    }
    canvasFrameRateButton->setMenu(canvasFrameRateMenu);

    const QString scaleFillLabel = uiText("dialog.render_settings.video.scale.fill", "Fill (crop if needed)");
    const QString scaleFitLabel = uiText("dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)");
    const QString scaleSquareFitLabel = uiText(
        "dialog.render_settings.video.scale.square_fit",
        "1:1 Fit (center square)");
    const QString importSkinLabel = uiText("dialog.render_settings.video.skin.import", "Import...");
    const QString slideStackOrderDxLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.dx_style",
        "DX Style"
    );
    const QString slideStackOrderFinaleLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.finale_style",
        "FiNALE Style"
    );
    const auto slideStackOrderLabelForValue = [slideStackOrderDxLabel, slideStackOrderFinaleLabel](bool earlierOnTop) {
        return earlierOnTop ? slideStackOrderDxLabel : slideStackOrderFinaleLabel;
    };
    const QString currentSkinButtonLabel = owner_.previewSkinDisplayName(owner_.previewSkinDirectoryName_);
    auto* skinButton = createDialogMenuButton(
        gameplayGroup,
        currentSkinButtonLabel
    );
    skinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* skinMenu = new QMenu(skinButton);
    styleRoundedMenu(*skinMenu);
    for (const QString& skinDirectoryName : owner_.availablePreviewSkinDirectoryNames()) {
        const QString skinLabel = owner_.previewSkinDisplayName(skinDirectoryName);
        addDialogMenuChoice(skinMenu, skinLabel, [this, skinButton, skinDirectoryName, skinLabel]() {
            owner_.previewSkinDirectoryName_ = skinDirectoryName;
            owner_.previewSkinVariant_ =
                skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
                    ? PreviewSkinVariant::Dx
                    : PreviewSkinVariant::Standard;
            skinButton->setText(skinLabel);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
            }
            owner_.savePortableState();
        });
    }
    if (!skinMenu->actions().isEmpty()) {
        skinMenu->addSeparator();
    }
    addDialogMenuChoice(skinMenu, importSkinLabel, [this]() {
        const QString skinRoot = owner_.resolvePreviewSkinRootDir();
        if (!skinRoot.isEmpty()) {
            QDir().mkpath(skinRoot);
            QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
        }
    });
    skinButton->setMenu(skinMenu);
    const QString disabledLabel = uiText("dialog.render_settings.option.disabled", "Disabled");
    const QString slideJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.slide", "slide");
    const QString tapJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.tap", "tap");
    const QString touchJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.touch", "touch");
    const auto judgeEffectButtonLabel = [
        this,
        slideJudgeChoiceLabel,
        tapJudgeChoiceLabel,
        touchJudgeChoiceLabel,
        disabledLabel
    ]() {
        QStringList parts;
        if (owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay) {
            parts.append(slideJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTapJudgeOverlay) {
            parts.append(tapJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTouchJudgeOverlay) {
            parts.append(touchJudgeChoiceLabel);
        }
        return parts.isEmpty() ? disabledLabel : parts.join(QStringLiteral(", "));
    };
    auto* judgeEffectButton = createDialogMenuButton(gameplayGroup, judgeEffectButtonLabel());
    judgeEffectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeEffectMenu = new QMenu(judgeEffectButton);
    styleRoundedMenu(*judgeEffectMenu);
    // QCheckBox-in-QWidgetAction so the menu stays open during multi-selection (regular QActions auto-close).
    const auto addJudgeEffectChoice = [
        this,
        &dialog,
        judgeEffectButton,
        judgeEffectButtonLabel,
        judgeEffectMenu
    ](const QString& label, bool MuriRenderOptions::*memberPtr) {
        auto* action = new QWidgetAction(judgeEffectMenu);
        auto* checkbox = new QCheckBox(label, judgeEffectMenu);
        checkbox->setChecked(owner_.muriRenderOptions_.*memberPtr);
        checkbox->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        checkbox->setStyleSheet(
            QStringLiteral(
                "QCheckBox {"
                " color: %1;"
                " background: transparent;"
                " padding: 6px 20px 6px 12px;"
                "}"
                "QCheckBox:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(
            checkbox,
            &QCheckBox::toggled,
            &dialog,
            [this, judgeEffectButton, judgeEffectButtonLabel, memberPtr](bool checked) {
                if (owner_.muriRenderOptions_.*memberPtr == checked) {
                    return;
                }
                owner_.muriRenderOptions_.*memberPtr = checked;
                judgeEffectButton->setText(judgeEffectButtonLabel());
                owner_.applyMuriRenderOptions();
                owner_.savePortableState();
            }
        );
        action->setDefaultWidget(checkbox);
        judgeEffectMenu->addAction(action);
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);
    judgeEffectButton->setMenu(judgeEffectMenu);
    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText(
        "dialog.render_settings.gameplay.judge_line.area_labeled",
        "Judge Area (Labeled)"
    );
    const QString judgeLineImportLabel = uiText("dialog.render_settings.gameplay.judge_line.import", "Import...");
    const auto judgeLineLabelForVariant = [
        judgeLinePointLabel,
        judgeLineLineLabel,
        judgeLineAreaLabel,
        judgeLineAreaLabeledLabel
    ](PreviewOutlineVariant variant) {
        switch (variant) {
        case PreviewOutlineVariant::Point:
            return judgeLinePointLabel;
        case PreviewOutlineVariant::JudgeArea:
            return judgeLineAreaLabel;
        case PreviewOutlineVariant::JudgeAreaLabeled:
            return judgeLineAreaLabeledLabel;
        case PreviewOutlineVariant::Line:
        default:
            return judgeLineLineLabel;
        }
    };
    const auto judgeLineButtonLabel = [&]() {
        return owner_.previewCustomOutlineFileName_.isEmpty()
            ? judgeLineLabelForVariant(owner_.previewOutlineVariant_)
            : owner_.previewCustomOutlineFileName_;
    };
    auto* judgeLineButton = createDialogMenuButton(gameplayGroup, judgeLineButtonLabel());
    judgeLineButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeLineMenu = new QMenu(judgeLineButton);
    styleRoundedMenu(*judgeLineMenu);
    addDialogMenuChoice(judgeLineMenu, judgeLinePointLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Point, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineLineLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Line, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineAreaLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeArea, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(
        judgeLineMenu,
        judgeLineAreaLabeledLabel,
        [this, judgeLineButton, judgeLineLabelForVariant]() {
            owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled, false, true);
            judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
        }
    );
    const QStringList customOutlineNames = owner_.availablePreviewCustomOutlineFileNames();
    if (!customOutlineNames.isEmpty()) {
        judgeLineMenu->addSeparator();
    }
    for (const QString& fileName : customOutlineNames) {
        addDialogMenuChoice(judgeLineMenu, fileName, [this, judgeLineButton, fileName]() {
            owner_.applyPreviewCustomOutlineFileName(fileName, true);
            judgeLineButton->setText(fileName);
        });
    }
    judgeLineMenu->addSeparator();
    addDialogMenuChoice(judgeLineMenu, judgeLineImportLabel, [this]() {
        const QString outlineDir = owner_.resolvePreviewCustomOutlineDir();
        if (!outlineDir.isEmpty()) {
            QDir().mkpath(outlineDir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
        }
    });
    judgeLineButton->setMenu(judgeLineMenu);
    auto* forceLabeledJudgeLineWhenPausedCheck = new QCheckBox(
        uiText(
            "dialog.render_settings.gameplay.force_labeled_judge_line_when_paused",
            "Hide PV / BG while preview is paused"
        ),
        videoGroup
    );
    forceLabeledJudgeLineWhenPausedCheck->setChecked(owner_.previewForceLabeledJudgeLineWhenPaused_);
    auto* slideStackOrderButton = createDialogMenuButton(
        gameplayGroup,
        slideStackOrderLabelForValue(owner_.previewSlideEarlierSecondAndTextOnTop_)
    );
    slideStackOrderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* slideStackOrderMenu = new QMenu(slideStackOrderButton);
    styleRoundedMenu(*slideStackOrderMenu);
    const auto setSlideStackOrder = [
        this,
        slideStackOrderButton,
        slideStackOrderLabelForValue
    ](bool earlierOnTop) {
        if (owner_.previewSlideEarlierSecondAndTextOnTop_ == earlierOnTop) {
            slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
            return;
        }
        owner_.previewSlideEarlierSecondAndTextOnTop_ = earlierOnTop;
        slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderDxLabel, [setSlideStackOrder]() {
        setSlideStackOrder(true);
    });
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderFinaleLabel, [setSlideStackOrder]() {
        setSlideStackOrder(false);
    });
    slideStackOrderButton->setMenu(slideStackOrderMenu);
    PreviewBackgroundScaleMode selectedScaleMode = owner_.previewBackgroundScaleMode_;
    const auto scaleModeLabelFor = [&](PreviewBackgroundScaleMode mode) {
        switch (mode) {
        case PreviewBackgroundScaleMode::FitContain:
            return scaleFitLabel;
        case PreviewBackgroundScaleMode::SquareFitContain:
            return scaleSquareFitLabel;
        case PreviewBackgroundScaleMode::FillCrop:
        default:
            return scaleFillLabel;
        }
    };
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        scaleModeLabelFor(selectedScaleMode)
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    const auto setScaleMode = [&](PreviewBackgroundScaleMode mode) {
        selectedScaleMode = mode;
        scaleModeButton->setText(scaleModeLabelFor(selectedScaleMode));
        owner_.previewBackgroundScaleMode_ = selectedScaleMode;
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FillCrop);
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FitContain);
    });
    addDialogMenuChoice(scaleModeMenu, scaleSquareFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::SquareFitContain);
    });
    scaleModeButton->setMenu(scaleModeMenu);

    auto* smoothBrightnessCheck = new QCheckBox(
        uiText("dialog.render_settings.video.smooth_brightness", "Smooth brightness"),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(owner_.previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        uiText("dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"),
        videoGroup
    );
    timestampCheck->setChecked(owner_.previewShowTimestamp_);
    auto* debugCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.debug", "Show preview debug info"),
        videoGroup
    );
    debugCheck->setChecked(owner_.previewShowDebugInfo_);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Layout Size"), layoutSquareScaleRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"), scaleModeButton);
    videoFormLayout->addRow(
        uiText("dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"),
        canvasFrameRateButton
    );
    auto* videoCheckRow = new QWidget(videoGroup);
    auto* videoCheckLayout = new QGridLayout(videoCheckRow);
    videoCheckLayout->setContentsMargins(0, 0, 0, 0);
    videoCheckLayout->setHorizontalSpacing(6);
    videoCheckLayout->setVerticalSpacing(6);
    videoCheckLayout->setColumnStretch(0, 1);
    videoCheckLayout->setColumnStretch(1, 1);
    videoCheckLayout->addWidget(smoothBrightnessCheck, 0, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(timestampCheck, 0, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(debugCheck, 1, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(forceLabeledJudgeLineWhenPausedCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    const auto addGameplayField = [gameplayGroup, gameplayLayout](int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(gameplayGroup);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };
    addGameplayField(
        0,
        0,
        uiText("dialog.render_settings.video.tap_flow_speed", "Tap Flow Speed"),
        tapFlowSpeedEdit
    );
    addGameplayField(
        0,
        1,
        uiText("dialog.render_settings.video.touch_flow_speed", "Touch Flow Speed"),
        touchFlowSpeedEdit
    );
    addGameplayField(
        1,
        0,
        uiText("dialog.render_settings.video.skin", "Skin"),
        skinButton
    );
    addGameplayField(
        1,
        1,
        uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"),
        judgeLineButton
    );
    addGameplayField(
        2,
        0,
        uiText("dialog.render_settings.gameplay.judge_effect", "Judge Effect"),
        judgeEffectButton
    );
    addGameplayField(
        2,
        1,
        uiText("dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"),
        slideStackOrderButton
    );
    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    gameplayGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        rootLayout->addWidget(videoGroup, 0);
        rootLayout->addWidget(gameplayGroup, 0);
    }
    auto* buttonBox = new QDialogButtonBox(&dialog);
    QPushButton* saveLocalAudioPresetButton = nullptr;
    QPushButton* applyLocalAudioPresetButton = nullptr;
    if (includeAudioSettings) {
        saveLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.set_software_default_audio", "Save Local Preset"),
            QDialogButtonBox::ActionRole
        );
        applyLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.restore_project_default", "Apply Local Preset"),
            QDialogButtonBox::ActionRole
        );
        if (saveLocalAudioPresetButton != nullptr) {
            saveLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
        if (applyLocalAudioPresetButton != nullptr) {
            applyLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;
    auto* dialogAuditionRuntime = new QtPreviewSfxRuntime(&dialog);
    QString dialogAuditionSfxDir;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto playDialogLocalSfxAudition = [
        this,
        dialogAuditionRuntime,
        &dialogAuditionSfxDir
    ](const QString& audition) {
        if (audition.isEmpty()) {
            return false;
        }
        QString resolvedKind = previewSfxNormalizedKind(audition);
        if (resolvedKind == QStringLiteral("break_slide")) {
            resolvedKind = QStringLiteral("break_slide_start");
        }
        const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
        if (sfxDir.isEmpty()) {
            return false;
        }
        const QString resolvedSfxDir = QDir::cleanPath(sfxDir);
        if (dialogAuditionSfxDir != resolvedSfxDir || !dialogAuditionRuntime->audioEngineInitialized()) {
            dialogAuditionRuntime->setWarmupResolvedPaths(QString(), QString(), resolvedSfxDir);
            dialogAuditionRuntime->reloadAssets(owner_.previewAudioSettings_);
            dialogAuditionSfxDir = resolvedSfxDir;
        } else {
            dialogAuditionRuntime->applyLevels(owner_.previewAudioSettings_);
        }
        if (!dialogAuditionRuntime->audioEngineInitialized()) {
            return false;
        }
        dialogAuditionRuntime->stopAll();
        return dialogAuditionRuntime->audition(resolvedKind);
    };

    const QString muteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.mute", "Mute %1");
    const QString unmuteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.unmute", "Unmute %1");
    const QString muteButtonStyleSheet = QStringLiteral(
        "QToolButton { border: none; background: transparent; padding: 0; margin: 0; }"
        "QToolButton:hover { border: none; background: transparent; }"
        "QToolButton:pressed { border: none; background: transparent; }"
        "QToolButton:disabled { border: none; background: transparent; }"
    );

    const auto makeAudioMuteIcon = [](bool muted) {
        constexpr int kExtent = 16;
        QPixmap pixmap(kExtent, kExtent);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#101010"));
        painter.drawPolygon(QPolygonF{
            QPointF(1.5, 5.5),
            QPointF(4.4, 5.5),
            QPointF(8.0, 2.8),
            QPointF(8.0, 13.2),
            QPointF(4.4, 10.5),
            QPointF(1.5, 10.5),
        });

        QPen pen(QColor("#101010"));
        pen.setWidthF(1.35);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        if (muted) {
            painter.drawLine(QPointF(10.1, 5.5), QPointF(13.4, 10.5));
            painter.drawLine(QPointF(13.4, 5.5), QPointF(10.1, 10.5));
        } else {
            QPainterPath waveNear;
            waveNear.moveTo(9.3, 6.2);
            waveNear.quadTo(10.8, 8.0, 9.3, 9.8);
            painter.drawPath(waveNear);

            QPainterPath waveFar;
            waveFar.moveTo(10.8, 4.5);
            waveFar.quadTo(13.9, 8.0, 10.8, 11.5);
            painter.drawPath(waveFar);
        }

        return QIcon(pixmap);
    };

    const auto syncAudioControlsFromCurrentSettings = [
        this,
        masterSlider,
        masterLabel,
        masterMuteButton,
        masterAudioLabelText,
        muteAudioButtonTooltip,
        unmuteAudioButtonTooltip,
        muteButtonStyleSheet,
        makeAudioMuteIcon,
        bgmSlider,
        bgmLabel,
        bgmMuteButton,
        answerSlider,
        answerLabel,
        answerMuteButton,
        judgeSlider,
        judgeLabel,
        judgeMuteButton,
        breakSlider,
        breakLabel,
        breakMuteButton,
        breakSlideSlider,
        breakSlideLabel,
        breakSlideMuteButton,
        slideSlider,
        slideLabel,
        slideMuteButton,
        breakSlideTailCheerCheck,
        exSlider,
        exLabel,
        exMuteButton,
        touchSlider,
        touchLabel,
        touchMuteButton,
        fireworkSlider,
        fireworkLabel,
        fireworkMuteButton,
        bgmAudioLabelText,
        answerAudioLabelText,
        judgeAudioLabelText,
        exAudioLabelText,
        breakAudioLabelText,
        breakSlideAudioLabelText,
        slideAudioLabelText,
        touchAudioLabelText,
        fireworkAudioLabelText
    ]() {
        owner_.previewAudioSettings_.normalize();
        const auto syncAudioRow = [](QSlider* slider, QLabel* label, int valuePercent) {
            if (slider == nullptr || label == nullptr) {
                return;
            }
            const QSignalBlocker blocker(slider);
            slider->setValue(valuePercent);
            label->setText(QString::number(valuePercent) + "%");
        };
        syncAudioRow(masterSlider, masterLabel, owner_.previewAudioSettings_.globalPercent());
        if (masterMuteButton != nullptr) {
            masterMuteButton->setStyleSheet(muteButtonStyleSheet);
            masterMuteButton->setIcon(makeAudioMuteIcon(owner_.previewAudioSettings_.globalMuted()));
            masterMuteButton->setToolTip(
                (owner_.previewAudioSettings_.globalMuted() ? unmuteAudioButtonTooltip : muteAudioButtonTooltip)
                    .arg(masterAudioLabelText)
            );
        }
        const auto syncPerChannelMuteButton = [
            &makeAudioMuteIcon,
            &muteButtonStyleSheet,
            &muteAudioButtonTooltip,
            &unmuteAudioButtonTooltip
        ](QToolButton* button, bool muted, const QString& labelText) {
            if (button == nullptr) {
                return;
            }
            button->setStyleSheet(muteButtonStyleSheet);
            button->setIcon(makeAudioMuteIcon(muted));
            button->setToolTip((muted ? unmuteAudioButtonTooltip : muteAudioButtonTooltip).arg(labelText));
        };
        syncAudioRow(bgmSlider, bgmLabel, owner_.previewAudioSettings_.trackPercent());
        syncAudioRow(answerSlider, answerLabel, owner_.previewAudioSettings_.answerPercent());
        syncAudioRow(judgeSlider, judgeLabel, owner_.previewAudioSettings_.tapPercent());
        syncAudioRow(exSlider, exLabel, owner_.previewAudioSettings_.exPercent());
        syncAudioRow(breakSlider, breakLabel, owner_.previewAudioSettings_.breakPercent());
        syncAudioRow(breakSlideSlider, breakSlideLabel, owner_.previewAudioSettings_.breakSlidePercent());
        syncAudioRow(slideSlider, slideLabel, owner_.previewAudioSettings_.slidePercent());
        if (breakSlideTailCheerCheck != nullptr) {
            const QSignalBlocker blocker(breakSlideTailCheerCheck);
            breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
        }
        syncAudioRow(touchSlider, touchLabel, owner_.previewAudioSettings_.touchPercent());
        syncAudioRow(fireworkSlider, fireworkLabel, owner_.previewAudioSettings_.fireworkPercent());
        syncPerChannelMuteButton(bgmMuteButton, owner_.previewAudioSettings_.trackMuted(), bgmAudioLabelText);
        syncPerChannelMuteButton(answerMuteButton, owner_.previewAudioSettings_.answerMuted(), answerAudioLabelText);
        syncPerChannelMuteButton(judgeMuteButton, owner_.previewAudioSettings_.tapMuted(), judgeAudioLabelText);
        syncPerChannelMuteButton(exMuteButton, owner_.previewAudioSettings_.exMuted(), exAudioLabelText);
        syncPerChannelMuteButton(breakMuteButton, owner_.previewAudioSettings_.breakMuted(), breakAudioLabelText);
        syncPerChannelMuteButton(
            breakSlideMuteButton,
            owner_.previewAudioSettings_.breakSlideMuted(),
            breakSlideAudioLabelText
        );
        syncPerChannelMuteButton(slideMuteButton, owner_.previewAudioSettings_.slideMuted(), slideAudioLabelText);
        syncPerChannelMuteButton(touchMuteButton, owner_.previewAudioSettings_.touchMuted(), touchAudioLabelText);
        syncPerChannelMuteButton(fireworkMuteButton, owner_.previewAudioSettings_.fireworkMuted(), fireworkAudioLabelText);
    };

    const auto commitAudioSettingsChange = [
        this,
        syncAudioControlsFromCurrentSettings,
        queueAudioApply
    ](const QString& audition) {
        owner_.previewAudioSettings_.normalize();
        syncAudioControlsFromCurrentSettings();
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.savePortableState();
        queueAudioApply(audition);
    };

    const auto connectAudioSlider = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QSlider* slider, void (PreviewAudioSettings::*setter)(int), const QString& audition) {
        connect(slider, &QSlider::valueChanged, &dialog, [this, setter, audition, commitAudioSettingsChange](int value) {
            (owner_.previewAudioSettings_.*setter)(value);
            commitAudioSettingsChange(audition);
        });
    };

    syncAudioControlsFromCurrentSettings();

    connectAudioSlider(masterSlider, &PreviewAudioSettings::setGlobalPercent, "answer");
    connect(masterMuteButton, &QToolButton::clicked, &dialog, [this, commitAudioSettingsChange]() {
        const bool muteAll = !owner_.previewAudioSettings_.globalMuted();
        if (muteAll) {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (!owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (!owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (!owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (!owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (!owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (!owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (!owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (!owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (!owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        } else {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        }
        commitAudioSettingsChange(owner_.previewAudioSettings_.globalMuted() ? QString() : QStringLiteral("answer"));
    });
    connectAudioSlider(bgmSlider, &PreviewAudioSettings::setTrackPercent, QString());
    connectAudioSlider(answerSlider, &PreviewAudioSettings::setAnswerPercent, "answer");
    connectAudioSlider(judgeSlider, &PreviewAudioSettings::setTapPercent, "judge");
    connectAudioSlider(exSlider, &PreviewAudioSettings::setExPercent, "ex");
    connectAudioSlider(breakSlider, &PreviewAudioSettings::setBreakPercent, "break");
    connectAudioSlider(breakSlideSlider, &PreviewAudioSettings::setBreakSlidePercent, "break_slide");
    connectAudioSlider(slideSlider, &PreviewAudioSettings::setSlidePercent, "slide");
    connectAudioSlider(touchSlider, &PreviewAudioSettings::setTouchPercent, "touch");
    connectAudioSlider(fireworkSlider, &PreviewAudioSettings::setFireworkPercent, "firework");
    connect(breakSlideTailCheerCheck, &QCheckBox::toggled, &dialog, [this, commitAudioSettingsChange](bool checked) {
        owner_.previewAudioSettings_.breakSlideTailCheerMuted = checked;
        commitAudioSettingsChange(QString());
    });
    const auto connectPerChannelMute = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QToolButton* button, void (PreviewAudioSettings::*toggleMuted)(), bool (PreviewAudioSettings::*mutedGetter)() const, const QString& audition) {
        connect(button, &QToolButton::clicked, &dialog, [this, toggleMuted, mutedGetter, audition, commitAudioSettingsChange]() {
            (owner_.previewAudioSettings_.*toggleMuted)();
            const bool mutedNow = (owner_.previewAudioSettings_.*mutedGetter)();
            commitAudioSettingsChange(mutedNow ? QString() : audition);
        });
    };
    connectPerChannelMute(bgmMuteButton, &PreviewAudioSettings::toggleTrackMuted, &PreviewAudioSettings::trackMuted, QString());
    connectPerChannelMute(answerMuteButton, &PreviewAudioSettings::toggleAnswerMuted, &PreviewAudioSettings::answerMuted, "answer");
    connectPerChannelMute(judgeMuteButton, &PreviewAudioSettings::toggleTapMuted, &PreviewAudioSettings::tapMuted, "judge");
    connectPerChannelMute(exMuteButton, &PreviewAudioSettings::toggleExMuted, &PreviewAudioSettings::exMuted, "ex");
    connectPerChannelMute(breakMuteButton, &PreviewAudioSettings::toggleBreakMuted, &PreviewAudioSettings::breakMuted, "break");
    connectPerChannelMute(
        breakSlideMuteButton,
        &PreviewAudioSettings::toggleBreakSlideMuted,
        &PreviewAudioSettings::breakSlideMuted,
        "break_slide"
    );
    connectPerChannelMute(slideMuteButton, &PreviewAudioSettings::toggleSlideMuted, &PreviewAudioSettings::slideMuted, "slide");
    connectPerChannelMute(touchMuteButton, &PreviewAudioSettings::toggleTouchMuted, &PreviewAudioSettings::touchMuted, "touch");
    connectPerChannelMute(fireworkMuteButton, &PreviewAudioSettings::toggleFireworkMuted, &PreviewAudioSettings::fireworkMuted, "firework");
    if (saveLocalAudioPresetButton != nullptr) {
        connect(saveLocalAudioPresetButton, &QPushButton::clicked, &dialog, [this]() {
            owner_.previewAudioSettings_.normalize();
            owner_.softwarePreviewAudioSettings_ = owner_.previewAudioSettings_;
            owner_.softwarePreviewAudioSettings_.normalize();
            owner_.savePortableState();
        });
    }
    if (applyLocalAudioPresetButton != nullptr) {
        connect(
            applyLocalAudioPresetButton,
            &QPushButton::clicked,
            &dialog,
            [this, audioApplyTimer, &pendingAudition, syncAudioControlsFromCurrentSettings]() {
                owner_.previewAudioSettings_ = owner_.softwarePreviewAudioSettings_;
                owner_.previewAudioSettings_.normalize();
                syncAudioControlsFromCurrentSettings();
                owner_.applyPreviewAudioSettingsToRuntime();
                owner_.savePortableState();
                if (audioApplyTimer->isActive()) {
                    audioApplyTimer->stop();
                }
                pendingAudition.clear();
            }
        );
    }

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, masterSlider, bgmSlider, answerSlider, judgeSlider, breakSlider, breakSlideSlider, slideSlider, exSlider, touchSlider, fireworkSlider, &pendingAudition, playDialogLocalSfxAudition]() {
        if (masterSlider->isSliderDown()
            || bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || judgeSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || breakSlideSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || fireworkSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition, playDialogLocalSfxAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });

    connect(outerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, outerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessOuter_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        outerBrightnessLabel->setText(QString::number(value) + "%");
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
        }
        owner_.savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        }
        owner_.savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(owner_.previewLayoutSquareScale_ * 100.0)) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        }
        owner_.savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewSmoothBrightness_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        }
        owner_.savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowTimestamp_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
        }
        owner_.savePortableState();
    });
    connect(forceLabeledJudgeLineWhenPausedCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewForceLabeledJudgeLineWhenPaused_ = checked;
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        owner_.applyPreviewStageMediaRouteVisualSettings();
        owner_.savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowDebugInfo_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowDebugInfo(owner_.previewShowDebugInfo_);
        }
        owner_.savePortableState();
    });
    dialog.adjustSize();
    dialog.exec();
}
