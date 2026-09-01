#pragma once

#include "../../MainWindow.h"

#include <functional>

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onSkinSettings();
    void onAbout();
    void onCompressBackgroundVideo();
    void onConvertTrackTo44100Hz();
    void onReadTitleFromTrack();
    void onReadArtistFromTrack();
    void onExtractBackgroundFromTrack();

    enum class MediaBlankTarget {
        Track,
        Pv,
    };
    // Prepend-blank, split so the QML dialog sits between the request and the
    // work instead of a modal QDialog blocking inside the handler.
    QVariantMap prependMediaBlankContext(MediaBlankTarget target);
    QVariantMap detectMediaBlankTiming(MediaBlankTarget target);
    void restoreMediaBlankBackup(MediaBlankTarget target);
    void applyMediaBlank(MediaBlankTarget target, double beats, double bpm);

private:
    // Identity of the media a blank is prepended to. Request, apply and restore
    // all resolve their paths from this one place so they cannot disagree.
    struct MediaBlankPaths {
        QString inputPath;
        QString inputName;
        QString backupName;
        QString backupPath;
        QString title;
        bool isTrack = false;
    };
    MediaBlankPaths resolveMediaBlankPaths(MediaBlankTarget target) const;

    QString resolveLatencyDetectorTrackPath() const;
    QString resolveCurrentChartDirectory() const;
    void releasePreviewMediaForFileOperation();
    void reloadPreviewMediaAfterFileOperation(bool reloadTrack);
    // Shows an export-style "done" dialog naming the produced file, with an
    // "Open Folder" button that reveals its containing directory.
    void runConvertTrackTo44100Hz(const QString& title, const QString& trackPath);
    void runCompressBackgroundVideo(
        const QString& title, const QString& videoPath, const QString& backupName);
    void showMediaOperationCompleteDialog(
        const QString& title,
        const QString& summary,
        const QString& producedFilePath);

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
