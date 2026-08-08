#pragma once

#include "NetBatchUploadScanner.h"

#include <QObject>
#include <QString>

#include <atomic>

namespace miacode::net {

struct NetBatchUploadRequest {
    QList<NetUploadJob> jobs;
    QString username;
    QString password;
};

class NetBatchUploadWorker : public QObject {
    Q_OBJECT

public:
    NetBatchUploadWorker(NetBatchUploadRequest request, std::atomic_bool* cancelRequested);

public slots:
    void run();

signals:
    void rowStatus(int row, const QString& status);
    void rowOutcome(int row, bool succeeded);
    void failureDetail(int row, const QString& summary, const QString& details);
    void progress(int completed);
    void summary(const QString& message);
    void finished(int succeeded, int failed, bool canceled, const QString& fatalError);

private:
    bool isCanceled() const;

    NetBatchUploadRequest request_;
    std::atomic_bool* cancelRequested_ = nullptr;
};

}  // namespace miacode::net
