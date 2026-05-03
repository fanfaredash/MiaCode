#pragma once

#include <QByteArray>
#include <QImage>
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
    // The writer thread reads `bytes.constData()` / `bytes.size()`. When `bytes`
    // is constructed via `QByteArray::fromRawData` (the zero-copy producer path),
    // `frameOwner` keeps the underlying pixel buffer alive: the packet holds a
    // QImage refcount on the QSG-readback frame, so the bytes remain valid until
    // the packet is popped and destroyed on the writer thread. When `bytes` owns
    // its storage (the rare repack path used for non-packed strides), `frameOwner`
    // stays null.
    QByteArray bytes;
    QImage frameOwner;
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

// Zero-copy enqueue. The pixel data is referenced via `QByteArray::fromRawData`
// pointing into `frame`, and the QImage itself is stored in the packet to keep
// the buffer alive until the writer thread pops the packet. Only valid when
// `frame` has packed RGBA8888 data (`bytesPerLine == width * 4`). Caller pre-
// validates that condition; fall back to the QByteArray overload when the
// stride is non-packed and a repack is required. Eliminates a per-frame
// heap allocation + memcpy of the entire frame (~8 MB for 1080p) — at 60 fps
// that is ~480 MB/s of churn the producer thread no longer pays.
//
// Caveat: VideoExportController routes frame 0 through the QByteArray
// overload as a workaround for a not-yet-identified corruption that affects
// only the very first exported frame when this overload is used (a 250×26 px
// black region matching the playfield outline's top arc). Frames 1+ are
// safe. See the comment block in VideoExportController's processReadyFrame
// for the full investigation. Once the root cause is found, the carve-out
// can be removed and this overload used unconditionally.
bool enqueueRawVideoFrame(
    RawVideoPipePump* pump,
    QImage frame,
    int frameIndex,
    qint64* producerWaitNs = nullptr,
    int* queuedFramesAfterEnqueue = nullptr,
    QString* failureDetail = nullptr
);
bool finishRawVideoPipePump(RawVideoPipePump* pump, QString* failureDetail = nullptr);
void shutdownRawVideoPipe(RawVideoPipe* pipe);
void shutdownRawVideoPipePump(RawVideoPipePump* pump);

}  // namespace miacode::video_export::raw_pipe
