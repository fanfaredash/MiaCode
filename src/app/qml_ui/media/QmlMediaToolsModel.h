#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "tools/media/PvBatchCompressionScanner.h"

#include <atomic>

class MainWindow;
class QThread;

namespace miacode::qml_ui {

// QML-facing surface for 音视频处理. The single-file tools forward to
// MainWindow's narrow entry points; the PV batch queue is owned here, because
// it is the only one with state of its own to keep between openings.
class QmlMediaToolsModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString batchDirectory READ batchDirectory NOTIFY batchChanged)
    Q_PROPERTY(QVariantList batchJobs READ batchJobs NOTIFY batchChanged)
    Q_PROPERTY(QString batchSummary READ batchSummary NOTIFY batchChanged)
    Q_PROPERTY(bool batchRunning READ batchRunning NOTIFY batchRunningChanged)

public:
    explicit QmlMediaToolsModel(MainWindow& backend, QObject* parent = nullptr);
    ~QmlMediaToolsModel() override;

    QString batchDirectory() const { return batchDirectory_; }
    QVariantList batchJobs() const;
    QString batchSummary() const { return batchSummary_; }
    bool batchRunning() const { return batchRunning_; }

    // ---- single-file tools ----
    Q_INVOKABLE void convertTrackTo44100Hz();
    Q_INVOKABLE void compressBackgroundVideo();

    // ---- prepend blank ----
    // Returns { available, title, isTrack, inputName, backupName, hasBackup,
    // beats, bpm }. When available is false the reason has already been posted
    // as a notice, so the caller simply does not open its dialog.
    Q_INVOKABLE QVariantMap prependContext(bool isTrack);
    Q_INVOKABLE QVariantMap detectPrependTiming(bool isTrack);
    Q_INVOKABLE void restorePrependBackup(bool isTrack);
    Q_INVOKABLE void applyPrepend(bool isTrack, double beats, double bpm);

    // ---- PV batch queue ----
    Q_INVOKABLE void chooseBatchDirectory();
    Q_INVOKABLE void addBatchFolder();
    Q_INVOKABLE void removeBatchJob(int index);
    Q_INVOKABLE void clearBatchQueue();
    Q_INVOKABLE void startBatchCompression();

signals:
    void batchChanged();
    void batchRunningChanged();

private:
    void setBatchDirectory(const QString& path);
    void rescanBatchDirectory();
    void setRowStatus(int row, const QString& status);
    void finishBatch(int succeeded, int failed, bool canceled, const QString& fatalError);

    MainWindow* backend_ = nullptr;
    QString batchDirectory_;
    QString batchSummary_;
    QList<miacode::media::PvCompressionJob> jobs_;
    QStringList jobStatuses_;
    std::atomic_bool batchCancelRequested_ = false;
    bool batchRunning_ = false;
    quint64 batchJobToken_ = 0;
    QThread* batchThread_ = nullptr;
};

}  // namespace miacode::qml_ui
