#pragma once

#include "PvBatchCompressionScanner.h"

#include <QObject>

#include <atomic>

namespace miacode::media {

class PvBatchCompressionWorker : public QObject {
    Q_OBJECT

public:
    PvBatchCompressionWorker(QList<PvCompressionJob> jobs, std::atomic_bool* cancelRequested);

public slots:
    void run();

signals:
    void rowStatus(int row, const QString& status);
    void progress(int completed);
    void summary(const QString& message);
    void finished(int succeeded, int failed, bool canceled, const QString& fatalError);

private:
    bool isCanceled() const;

    QList<PvCompressionJob> jobs_;
    std::atomic_bool* cancelRequested_ = nullptr;
};

QString resolvePvCompressionFfmpegExecutable();

}  // namespace miacode::media
