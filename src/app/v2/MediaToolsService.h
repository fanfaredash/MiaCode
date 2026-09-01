#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "MediaToolsEngine.h"

namespace miacode::v2 {

class ChartWorkspace;
class JobProgressService;
class PreviewSurface;
class UiRequestService;

// Widgets-free owner of the six single-file media operations exposed through
// MediaToolsEngine.  The preview is a live slot because the preview surface is
// installed after the application services are constructed and can be
// withdrawn during window teardown.
class MediaToolsService final : public QObject, public MediaToolsEngine
{
public:
    explicit MediaToolsService(
        ChartWorkspace& workspace,
        UiRequestService& uiRequests,
        JobProgressService& jobProgress,
        PreviewSurface*& previewSurfaceSlot,
        QObject* parent = nullptr);
    ~MediaToolsService() override;

    bool hasActiveMediaOperation() const { return activeMediaOperation_; }

    // Stop confirmation callbacks before the QML/backend owners begin to
    // disappear. QPointer protects against destruction; this flag also makes
    // an already-queued callback harmless during an orderly shutdown.
    void invalidateCallbacks();

    void convertTrackTo44100Hz() override;
    void compressBackgroundVideo() override;
    QVariantMap mediaBlankContext(bool isTrack) override;
    QVariantMap detectMediaBlankTiming(bool isTrack) override;
    void restoreMediaBlankBackup(bool isTrack) override;
    void applyMediaBlank(bool isTrack, double beats, double bpm) override;

private:
    struct MediaBlankPaths {
        QString inputPath;
        QString inputName;
        QString backupName;
        QString backupPath;
        QString title;
        bool isTrack = false;
    };

    MediaBlankPaths resolveMediaBlankPaths(bool isTrack) const;
    QString resolveCurrentChartDirectory() const;
    bool workspaceMediaOperationMatches(
        quint64 expectedRevision, const QString& expectedPath,
        const QString& currentPath) const;
    PreviewSurface* beginMediaFileOperation(const QString& title);
    bool endMediaFileOperation(PreviewSurface* surface, bool reloadTrack,
                               const QString& title);
    void runConvertTrackTo44100Hz(
        const QString& title, quint64 expectedRevision, const QString& trackPath);
    void runCompressBackgroundVideo(
        const QString& title, quint64 expectedRevision, const QString& videoPath,
        const QString& backupName);
    void showMediaOperationComplete(
        const QString& title, const QString& summary, const QString& producedFilePath);

    ChartWorkspace* workspace_ = nullptr;
    UiRequestService* uiRequests_ = nullptr;
    JobProgressService* jobProgress_ = nullptr;
    PreviewSurface** previewSurfaceSlot_ = nullptr;
    bool callbacksInvalidated_ = false;
    bool activeMediaOperation_ = false;
};

}  // namespace miacode::v2
