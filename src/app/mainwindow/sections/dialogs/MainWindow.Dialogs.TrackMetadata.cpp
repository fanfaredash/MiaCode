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
#include "common/ChartMediaImport.h"
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
#include <vector>

#include "common/DebugLog.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <RestartManager.h>
#pragma comment(lib, "Comdlg32.lib")
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

QString promptForChartMediaFile(
    QWidget* logicalParent,
    const QString& initialDir,
    const QString& dialogTitle,
    const QString& filter)
{
#ifdef Q_OS_WIN
    // QFileDialog's Windows-native helper is reached through QuickShell's
    // hidden QWidget backend. The picker can visibly select a file and fill
    // the filename field, yet fail to deliver the Open-button acceptance back
    // to Qt. Call the Windows picker directly and give it the visible
    // QuickShell root HWND so Open has an unambiguous owner and return path.
    std::wstring nativeFilter;
    const QStringList filterSections = filter.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
    for (const QString& section : filterSections) {
        const int patternOpen = section.lastIndexOf(QStringLiteral(" ("));
        const bool hasPattern = patternOpen >= 0 && section.endsWith(QLatin1Char(')'));
        const QString label = section;
        QString patterns = hasPattern
            ? section.mid(patternOpen + 2, section.size() - patternOpen - 3)
            : QStringLiteral("*.*");
        patterns.replace(QLatin1Char(' '), QLatin1Char(';'));

        nativeFilter.append(label.toStdWString());
        nativeFilter.push_back(L'\0');
        nativeFilter.append(patterns.toStdWString());
        nativeFilter.push_back(L'\0');
    }
    if (nativeFilter.empty()) {
        nativeFilter.append(L"Files");
        nativeFilter.push_back(L'\0');
        nativeFilter.append(L"*.*");
        nativeFilter.push_back(L'\0');
    }
    nativeFilter.push_back(L'\0');

    std::vector<wchar_t> selectedPath(32768, L'\0');
    const std::wstring nativeInitialDir = QDir::toNativeSeparators(initialDir).toStdWString();
    const std::wstring nativeTitle = dialogTitle.toStdWString();
    QWindow* ownerWindow = UiDialogs::applicationDialogTransientParent();
    if (ownerWindow == nullptr && logicalParent != nullptr) {
        ownerWindow = logicalParent->windowHandle();
    }

    OPENFILENAMEW request{};
    request.lStructSize = sizeof(request);
    request.hwndOwner = ownerWindow != nullptr
        ? reinterpret_cast<HWND>(ownerWindow->winId())
        : nullptr;
    request.lpstrFilter = nativeFilter.c_str();
    request.nFilterIndex = 1;
    request.lpstrFile = selectedPath.data();
    request.nMaxFile = static_cast<DWORD>(selectedPath.size());
    request.lpstrInitialDir = nativeInitialDir.empty() ? nullptr : nativeInitialDir.c_str();
    request.lpstrTitle = nativeTitle.c_str();
    request.Flags = OFN_EXPLORER
        | OFN_FILEMUSTEXIST
        | OFN_PATHMUSTEXIST
        | OFN_NOCHANGEDIR
        | OFN_ENABLESIZING;

    if (::GetOpenFileNameW(&request) == TRUE) {
        return QDir::fromNativeSeparators(QString::fromWCharArray(selectedPath.data()));
    }
    const DWORD dialogError = ::CommDlgExtendedError();
    if (dialogError != 0) {
        qWarning() << "Native chart-media picker failed:" << dialogError;
    }
    return QString();
#else
    QFileDialog dialog(UiDialogs::effectiveParentWidget(logicalParent));
    dialog.setWindowTitle(dialogTitle);
    if (!initialDir.isEmpty()) {
        dialog.setDirectory(initialDir);
    }
    dialog.setNameFilter(filter);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    UiDialogs::prepareDialogWindow(&dialog, logicalParent);
    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }
    const QStringList selectedFiles = dialog.selectedFiles();
    return selectedFiles.isEmpty() ? QString() : selectedFiles.constFirst();
#endif
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
    releasePreviewMediaForFileOperation();

    if (!cover.save(bgPath, "JPG", 92)) {
        QMessageBox::critical(
            UiDialogs::effectiveParentWidget(&owner_),
            title,
            UiText::text(QStringLiteral("track_metadata.failed_to_write_bg_jpg"))
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
        existed
            ? UiText::text(QStringLiteral("track_metadata.overwrote_bg_jpg_with_embedded"))
            : UiText::text(QStringLiteral("track_metadata.wrote_bg_jpg_from_the")),
        6000
    );
}

void MainWindow::DialogsSection::onImportBackgroundImage()
{
    importBackgroundMedia(false);
}

void MainWindow::DialogsSection::onImportBackgroundVideo()
{
    importBackgroundMedia(true);
}

void MainWindow::DialogsSection::importBackgroundMedia(bool video)
{
    MC_OP("MainWindow::DialogsSection::importBackgroundMedia");
    using miacode::chart_media_import::Kind;

    const Kind kind = video ? Kind::Video : Kind::Image;
    const QString title = UiText::text(video
        ? QStringLiteral("track_metadata.import_background_video")
        : QStringLiteral("track_metadata.import_background_image"));
    const QString chartDirPath = resolveCurrentChartDirectory();
    QWidget* parent = UiDialogs::effectiveParentWidget(&owner_);
    if (chartDirPath.isEmpty()) {
        QMessageBox::warning(
            parent,
            title,
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }

    const QString filter = UiText::text(video
        ? QStringLiteral("track_metadata.video_file_filter")
        : QStringLiteral("track_metadata.image_file_filter"));
    const QString sourcePath = promptForChartMediaFile(&owner_, chartDirPath, title, filter);
    if (sourcePath.isEmpty()) {
        return;
    }

    if (!miacode::chart_media_import::isSupportedSource(sourcePath, kind)) {
        QMessageBox::warning(
            parent,
            title,
            UiText::text(QStringLiteral("track_metadata.unsupported_media_file")));
        return;
    }
    if (!video) {
        QImageReader reader(sourcePath);
        if (!reader.canRead()) {
            QMessageBox::warning(
                parent,
                title,
                UiText::text(QStringLiteral("track_metadata.failed_to_read_image")));
            return;
        }
    }

    const QString targetPath = QDir(chartDirPath).filePath(
        miacode::chart_media_import::targetFileName(sourcePath, kind));
    const QStringList existing = miacode::chart_media_import::existingCandidatePaths(chartDirPath, kind);
    bool replacesExisting = false;
    for (const QString& path : existing) {
        if (!miacode::chart_media_import::pathsReferToSameFile(path, sourcePath)
            || !miacode::chart_media_import::pathsReferToSameFile(path, targetPath)) {
            replacesExisting = true;
            break;
        }
    }
    if (replacesExisting) {
        const auto answer = QMessageBox::question(
            parent,
            title,
            UiText::text(video
                ? QStringLiteral("track_metadata.background_video_exists_overwrite")
                : QStringLiteral("track_metadata.background_image_exists_overwrite")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    owner_.statusBar()->showMessage(
        UiText::text(video
            ? QStringLiteral("track_metadata.copying_background_video")
            : QStringLiteral("track_metadata.copying_background_image")));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    releasePreviewMediaForFileOperation();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const miacode::chart_media_import::Result imported =
        miacode::chart_media_import::importToChartDirectory(sourcePath, chartDirPath, kind);
    QApplication::restoreOverrideCursor();

    if (!imported.ok) {
        reloadPreviewMediaAfterFileOperation(false);
        QMessageBox::critical(
            parent,
            title,
            UiText::text(QStringLiteral("track_metadata.failed_to_import_media"))
                .arg(imported.error));
        return;
    }

    if (video && state_.document_.videoPath != QStringLiteral("pv.mp4")) {
        // Keep explicit &video= authoring aligned with the canonical imported
        // filename. Otherwise an older override would continue to shadow the
        // newly copied pv.mp4 in both preview and export.
        state_.document_.videoPath = QStringLiteral("pv.mp4");
        state_.documentDirty_ = true;
        owner_.updateDirtyState();
    }

    reloadPreviewMediaAfterFileOperation(false);
    owner_.rebuildFieldSidebar();
    _mc_op_.note(QStringLiteral("kind=%1 source=%2 target=%3 changed=%4 backups=%5 cleanup_warnings=%6")
                     .arg(video ? QStringLiteral("video") : QStringLiteral("image"),
                          sourcePath,
                          imported.targetPath)
                     .arg(imported.changed ? 1 : 0)
                     .arg(imported.backupPaths.size())
                     .arg(imported.cleanupWarnings.size()));

    if (!imported.cleanupWarnings.isEmpty()) {
        QMessageBox::warning(
            parent,
            title,
            UiText::text(QStringLiteral("track_metadata.imported_with_cleanup_warning"))
                .arg(imported.cleanupWarnings.join(QStringLiteral("\n"))));
    }
    owner_.statusBar()->showMessage(
        UiText::text(video
            ? QStringLiteral("track_metadata.imported_background_video")
            : QStringLiteral("track_metadata.imported_background_image"))
            .arg(imported.targetPath),
        6000);
}
