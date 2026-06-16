#pragma once

#include "NetClient.h"

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>

namespace miacode::net {

struct NetBatchDownloadRequest {
    QList<NetDownloadJob> jobs;
    QString outputDirectory;
    bool createZip = false;
};

struct NetBatchResourceSpec {
    QString path;
    QString fileName;
    QString label;
};

struct NetBatchResourceStats {
    QString label;
    qint64 bytes = 0;
    qint64 elapsedMs = 0;
};

class NetBatchDownloadWorker : public QObject {
    Q_OBJECT

public:
    NetBatchDownloadWorker(NetBatchDownloadRequest request, std::atomic_bool* cancelRequested);

public slots:
    void run();

signals:
    void rowStatus(int row, const QString& status);
    void progress(int completed);
    void summary(const QString& message);
    void log(const QString& message);
    void finished(int succeeded, int failed, bool paused, bool canceled);

private:
    bool isCanceled() const;
    bool downloadResourceToFile(
        NetClient& client,
        int row,
        const QString& chartId,
        const NetBatchResourceSpec& resource,
        const QString& outputPath,
        QString* errorMessage,
        bool* paused,
        QList<NetBatchResourceStats>* resourceStats);
    void emitChartBottleneck(const QString& title, const QList<NetBatchResourceStats>& resourceStats);

    NetBatchDownloadRequest request_;
    std::atomic_bool* cancelRequested_ = nullptr;
};

}  // namespace miacode::net
