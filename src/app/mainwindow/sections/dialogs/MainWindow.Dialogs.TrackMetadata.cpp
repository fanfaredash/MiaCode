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

namespace {

// Prompts the user to pick an MP3 to read ID3 metadata from. Returns an empty
// string if the dialog is cancelled. Opens in the current chart's directory
// when known (where track.mp3 usually lives), but the user is free to point at
// any MP3.
QString promptForMetadataMp3(QWidget* parent, const QString& initialDir, const QString& dialogTitle)
{
    return QFileDialog::getOpenFileName(
        parent,
        dialogTitle,
        initialDir,
        UiText::isChineseUi()
            ? QStringLiteral("MP3 音频 (*.mp3);;所有文件 (*.*)")
            : QStringLiteral("MP3 audio (*.mp3);;All files (*.*)")
    );
}

// Shared helper for the title/artist buttons. Reads the ID3v2 tag of the
// user-selected MP3 at `trackPath` and returns the requested field's value.
// Reports any failure through QMessageBox at `parent`; on success returns the
// trimmed field value, or an empty string if the tag exists but the requested
// field is blank (the caller decides whether to warn). `trackPath` is assumed
// non-empty — callers handle a cancelled file picker before calling.
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
        return QString();
    }
    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid) {
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::isChineseUi()
                ? QStringLiteral("没能在所选 MP3 中读取到 ID3v2 标签。")
                : QStringLiteral("No ID3v2 tag was found in the selected MP3.")
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
                ? QStringLiteral("所选 MP3 的 ID3 标签里没有%1信息。").arg(fieldLabelZh)
                : QStringLiteral("The selected MP3's ID3 tag carries no %1.").arg(fieldLabelEn)
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
    const QString trackPath = promptForMetadataMp3(
        UiDialogs::effectiveParentWidget(&owner_), resolveCurrentChartDirectory(), title);
    if (trackPath.isEmpty()) {
        return;  // user cancelled the file picker
    }
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
            ? QStringLiteral("已从 MP3 读取标题。")
            : QStringLiteral("Loaded title from MP3."),
        6000
    );
}

void MainWindow::DialogsSection::onReadArtistFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadArtistFromTrack");
    const QString title = UiText::isChineseUi()
        ? QStringLiteral("从 MP3 读取曲师")
        : QStringLiteral("Read Artist from MP3");
    const QString trackPath = promptForMetadataMp3(
        UiDialogs::effectiveParentWidget(&owner_), resolveCurrentChartDirectory(), title);
    if (trackPath.isEmpty()) {
        return;  // user cancelled the file picker
    }
    const QString value = readTrackTagField(
        trackPath, TrackTagField::Artist, UiDialogs::effectiveParentWidget(&owner_), title);
    if (value.isEmpty() || ui_.artistEdit_ == nullptr) {
        return;
    }
    ui_.artistEdit_->setText(value);
    _mc_op_.note(QStringLiteral("track=%1 artist=%2").arg(trackPath, value));
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已从 MP3 读取曲师。")
            : QStringLiteral("Loaded artist from MP3."),
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
    const QString trackPath = promptForMetadataMp3(
        UiDialogs::effectiveParentWidget(&owner_), chartDirPath, title);
    if (trackPath.isEmpty()) {
        return;  // user cancelled the file picker
    }

    const miacode::id3::Tag tag = miacode::id3::readTagFromFile(trackPath);
    if (!tag.valid || tag.pictureBytes.isEmpty()) {
        QMessageBox::information(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::isChineseUi()
                ? QStringLiteral("所选 MP3 中没有内嵌的封面图。")
                : QStringLiteral("The selected MP3 has no embedded cover artwork.")
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
                   ? QStringLiteral("已覆盖 bg.jpg（来源：所选 MP3 内嵌封面）。")
                   : QStringLiteral("已生成 bg.jpg（来源：所选 MP3 内嵌封面）。"))
            : (existed
                   ? QStringLiteral("Overwrote bg.jpg with embedded cover from the selected MP3.")
                   : QStringLiteral("Wrote bg.jpg from the selected MP3's embedded cover.")),
        6000
    );
}
