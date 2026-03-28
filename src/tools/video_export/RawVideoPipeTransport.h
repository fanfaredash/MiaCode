#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace miacode::video_export::raw_pipe {

struct RawVideoPipePlan {
    qint64 frameBytes = 0;
    qint64 requestedBufferBytes = 0;
    int maxBufferedFrames = 32;
    qint64 writeChunkBytes = 1LL * 1024LL * 1024LL;
    int connectTimeoutMs = 10000;
    int connectPollMs = 20;
};

enum class RawVideoPipeTransport {
    NamedPipe,
    Fifo,
};

struct RawVideoPipe {
    RawVideoPipeTransport transport =
#ifdef Q_OS_WIN
        RawVideoPipeTransport::NamedPipe;
#else
        RawVideoPipeTransport::Fifo;
#endif
    QString inputPath;
    qint64 requestedBufferBytes = 0;
    qint64 configuredBufferBytes = 0;
#ifdef Q_OS_WIN
    qintptr nativeHandle = -1;
#else
    int fd = -1;
    QString fifoPath;
#endif
};

struct RawVideoPipePacket {
    QByteArray bytes;
    int frameIndex = -1;
};

struct RawVideoPipeStats {
    int maxQueuedFrames = 0;
    qint64 totalProducerWaitNs = 0;
    qint64 maxProducerWaitNs = 0;
    int maxProducerWaitFrame = -1;
    qint64 totalPipeWriteNs = 0;
    qint64 maxPipeWriteNs = 0;
    int maxPipeWriteFrame = -1;
    qint64 connectElapsedMs = 0;
};

struct RawVideoPipePump {
    RawVideoPipe pipe;
    RawVideoPipePlan plan;
    std::mutex mutex;
    std::condition_variable notEmpty;
    std::condition_variable notFull;
    std::deque<RawVideoPipePacket> queue;
    std::thread writerThread;
    bool writerThreadStarted = false;
    bool enqueueClosed = false;
    bool writerFinished = false;
    std::atomic<bool> abortRequested = false;
    QString failureDetail;
    RawVideoPipeStats stats;
};

RawVideoPipePlan chooseRawVideoPipePlan(const QSize& frameSize);
QString rawVideoPipeTransportName(RawVideoPipeTransport transport);
bool startRawVideoPipe(
    RawVideoPipe* pipe,
    const QString& tempDirPath,
    const RawVideoPipePlan& plan,
    QString* failureDetail = nullptr
);
bool startRawVideoPipePumpThread(RawVideoPipePump* pump, QString* failureDetail = nullptr);
bool enqueueRawVideoFrame(
    RawVideoPipePump* pump,
    QByteArray frameBytes,
    int frameIndex,
    qint64* producerWaitNs = nullptr,
    int* queuedFramesAfterEnqueue = nullptr,
    QString* failureDetail = nullptr
);
bool finishRawVideoPipePump(RawVideoPipePump* pump, QString* failureDetail = nullptr);
void shutdownRawVideoPipe(RawVideoPipe* pipe);
void shutdownRawVideoPipePump(RawVideoPipePump* pump);

}  // namespace miacode::video_export::raw_pipe
