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
        UiText::text(QStringLiteral("track_metadata.mp3_audio_mp3_all_files"))
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
            UiText::text(QStringLiteral("track_metadata.no_id3v2_tag_was_found"))
        );
        return QString();
    }
    const QString value = (field == TrackTagField::Title ? tag.title : tag.artist).trimmed();
    if (value.isEmpty()) {
        const QString fieldLabel = (field == TrackTagField::Title)
            ? UiText::text(QStringLiteral("track_metadata.title"))
            : UiText::text(QStringLiteral("track_metadata.artist"));
        QMessageBox::information(
            parent,
            dialogTitle,
            UiText::text(QStringLiteral("track_metadata.the_selected_mp3_s_id3"))
                .arg(fieldLabel)
        );
        return QString();
    }
    return value;
}

}  // namespace

void MainWindow::DialogsSection::onReadTitleFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadTitleFromTrack");
    const QString title = UiText::text(QStringLiteral("track_metadata.read_title_from_mp3"));
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
        UiText::text(QStringLiteral("track_metadata.loaded_title_from_mp3")),
        6000
    );
}

void MainWindow::DialogsSection::onReadArtistFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onReadArtistFromTrack");
    const QString title = UiText::text(QStringLiteral("track_metadata.read_artist_from_mp3"));
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
        UiText::text(QStringLiteral("track_metadata.loaded_artist_from_mp3")),
        6000
    );
}

void MainWindow::DialogsSection::onExtractBackgroundFromTrack()
{
    MC_OP("MainWindow::DialogsSection::onExtractBackgroundFromTrack");
    const QString title = UiText::text(QStringLiteral("track_metadata.extract_cover_to_bg_jpg"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart"))
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
            UiText::text(QStringLiteral("track_metadata.the_selected_mp3_has_no"))
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
            UiText::text(QStringLiteral("track_metadata.failed_to_decode_embedded_cover")).arg(tag.pictureMimeType)
        );
        return;
    }

    const QString bgPath = QDir(chartDirPath).filePath(QStringLiteral("bg.jpg"));
    const bool existed = QFileInfo::exists(bgPath);
    if (existed) {
        const auto answer = QMessageBox::question(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("track_metadata.bg_jpg_already_exists_overwrite")),
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
    if (!releasePreviewMediaForFileOperation()) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            QStringLiteral("Preview media could not be released; no file was changed."));
        return;
    }

    if (!cover.save(bgPath, "JPG", 92)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("track_metadata.failed_to_write_bg_jpg"))
        );
        // Still try to reload — the previous file (if any) is back.
        if (!reloadPreviewMediaAfterFileOperation(false)) {
            QMessageBox::warning(
                UiDialogs::effectiveParentWidget(&owner_),
                title,
                QStringLiteral("Preview media could not be restored."));
        }
        return;
    }

    // reloadPreviewMediaAfterFileOperation re-runs the chart-path
    // resolution pipeline; PreviewStageMediaHost::setChartPath was
    // cleared by releasePreviewMediaForFileOperation above, so the
    // re-resolution lands on the brand-new bg.jpg even if the chart
    // path itself didn't change.
    if (!reloadPreviewMediaAfterFileOperation(false)) {
        QMessageBox::warning(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            QStringLiteral("The image was written, but preview media could not be restored."));
        return;
    }
    _mc_op_.note(QStringLiteral("bg=%1 replaced=%2 source_mime=%3 source_bytes=%4")
                     .arg(bgPath)
                     .arg(existed ? 1 : 0)
                     .arg(tag.pictureMimeType)
                     .arg(tag.pictureBytes.size()));
    owner_.statusBar()->showMessage(
        existed
            ? UiText::text(QStringLiteral("track_metadata.overwrote_bg_jpg_with_embedded"))
            : UiText::text(QStringLiteral("track_metadata.wrote_bg_jpg_from_the")),
        6000
    );
}
