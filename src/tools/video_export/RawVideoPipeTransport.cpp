#include "RawVideoPipeTransport.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QUuid>

#include <chrono>
#include <exception>
#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace miacode::video_export::raw_pipe {
namespace {
constexpr qint64 kRawPipeBaselineWidth = 1920;
constexpr qint64 kRawPipeBaselineHeight = 1080;
constexpr int kRawPipeBaselineBufferedFrames = 32;
constexpr int kRawPipeMinBufferedFrames = 8;
constexpr int kRawPipeMaxBufferedFrames = 128;

#ifdef Q_OS_WIN
HANDLE rawVideoPipeHandle(const RawVideoPipe& pipe)
{
    return reinterpret_cast<HANDLE>(pipe.nativeHandle);
}
#endif

bool connectRawVideoPipe(
    RawVideoPipe* pipe,
    const RawVideoPipePlan& plan,
    std::atomic<bool>* abortRequested,
    QString* failureDetail
)
{
    if (pipe == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw video pipe connection");
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();

#ifdef Q_OS_WIN
    DWORD connectMode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    HANDLE handle = rawVideoPipeHandle(*pipe);
    if (handle == INVALID_HANDLE_VALUE
        || !SetNamedPipeHandleState(handle, &connectMode, nullptr, nullptr)) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("SetNamedPipeHandleState(connect) failed error=%1")
                                 .arg(GetLastError());
        }
        return false;
    }

    while (true) {
        if (abortRequested != nullptr && abortRequested->load()) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("raw pipe connection aborted");
            }
            return false;
        }
        if (ConnectNamedPipe(handle, nullptr) != 0) {
            break;
        }
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            break;
        }
        if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("ConnectNamedPipe failed error=%1").arg(error);
            }
            return false;
        }
        if (timer.elapsed() >= plan.connectTimeoutMs) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("ConnectNamedPipe timeout after %1ms")
                                     .arg(plan.connectTimeoutMs);
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(plan.connectPollMs));
    }

    DWORD writeMode = PIPE_READMODE_BYTE | PIPE_WAIT;
    SetNamedPipeHandleState(handle, &writeMode, nullptr, nullptr);
    return true;
#else
    const QByteArray fifoPathBytes = QFile::encodeName(pipe->inputPath);
    while (true) {
        if (abortRequested != nullptr && abortRequested->load()) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("raw pipe connection aborted");
            }
            return false;
        }
        const int fd = ::open(fifoPathBytes.constData(), O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
#ifdef F_SETPIPE_SZ
            const int requestedSize = static_cast<int>(qBound<qint64>(
                64LL * 1024LL,
                plan.requestedBufferBytes,
                static_cast<qint64>(std::numeric_limits<int>::max())));
            const int configuredSize = ::fcntl(fd, F_SETPIPE_SZ, requestedSize);
            if (configuredSize > 0) {
                pipe->configuredBufferBytes = configuredSize;
            }
#endif
            const int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags >= 0) {
                ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
            }
            pipe->fd = fd;
            if (pipe->configuredBufferBytes <= 0) {
                pipe->configuredBufferBytes = plan.requestedBufferBytes;
            }
            return true;
        }
        if (errno != ENXIO && errno != ENOENT) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("open fifo failed path=%1 errno=%2")
                                     .arg(pipe->inputPath)
                                     .arg(errno);
            }
            return false;
        }
        if (timer.elapsed() >= plan.connectTimeoutMs) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("fifo connect timeout after %1ms path=%2")
                                     .arg(plan.connectTimeoutMs)
                                     .arg(pipe->inputPath);
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(plan.connectPollMs));
    }
#endif
}

bool writeAllToRawVideoPipe(
    RawVideoPipe* pipe,
    const char* data,
    qint64 size,
    const RawVideoPipePlan& plan,
    qint64* elapsedNs,
    QString* failureDetail
)
{
    if (elapsedNs != nullptr) {
        *elapsedNs = 0;
    }
    if (pipe == nullptr || data == nullptr || size < 0) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw pipe write input");
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        const qint64 chunkBytes = qMin(plan.writeChunkBytes, size - writtenTotal);
#ifdef Q_OS_WIN
        DWORD written = 0;
        if (!WriteFile(
                rawVideoPipeHandle(*pipe),
                data + writtenTotal,
                static_cast<DWORD>(chunkBytes),
                &written,
                nullptr)) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("WriteFile failed after %1/%2 bytes error=%3")
                                     .arg(writtenTotal)
                                     .arg(size)
                                     .arg(GetLastError());
            }
            return false;
        }
        if (written == 0) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("WriteFile wrote zero bytes after %1/%2 bytes")
                                     .arg(writtenTotal)
                                     .arg(size);
            }
            return false;
        }
        writtenTotal += written;
#else
        const ssize_t written = ::write(pipe->fd, data + writtenTotal, static_cast<size_t>(chunkBytes));
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("write failed after %1/%2 bytes errno=%3")
                                     .arg(writtenTotal)
                                     .arg(size)
                                     .arg(errno);
            }
            return false;
        }
        if (written == 0) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("write wrote zero bytes after %1/%2 bytes")
                                     .arg(writtenTotal)
                                     .arg(size);
            }
            return false;
        }
        writtenTotal += written;
#endif
    }

    if (elapsedNs != nullptr) {
        *elapsedNs = timer.nsecsElapsed();
    }
    return true;
}

void rawVideoPipeWriterMain(RawVideoPipePump* pump)
{
    if (pump == nullptr) {
        return;
    }

    QString failureDetail;
    QElapsedTimer connectTimer;
    connectTimer.start();
    if (!connectRawVideoPipe(&pump->pipe, pump->plan, &pump->abortRequested, &failureDetail)) {
        std::lock_guard<std::mutex> lock(pump->mutex);
        if (!pump->abortRequested.load()) {
            pump->failureDetail = failureDetail;
        }
        pump->writerFinished = true;
        pump->stats.connectElapsedMs = connectTimer.elapsed();
        pump->notFull.notify_all();
        pump->notEmpty.notify_all();
        shutdownRawVideoPipe(&pump->pipe);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pump->mutex);
        pump->stats.connectElapsedMs = connectTimer.elapsed();
    }

    while (true) {
        RawVideoPipePacket packet;
        {
            std::unique_lock<std::mutex> lock(pump->mutex);
            while (pump->queue.empty() && !pump->enqueueClosed && !pump->abortRequested.load()) {
                pump->notEmpty.wait(lock);
            }
            if (pump->abortRequested.load()) {
                break;
            }
            if (pump->queue.empty()) {
                break;
            }
            packet = std::move(pump->queue.front());
            pump->queue.pop_front();
            pump->notFull.notify_all();
        }

        qint64 writeNs = 0;
        if (!writeAllToRawVideoPipe(
                &pump->pipe,
                packet.bytes.constData(),
                packet.bytes.size(),
                pump->plan,
                &writeNs,
                &failureDetail)) {
            std::lock_guard<std::mutex> lock(pump->mutex);
            if (!pump->abortRequested.load()) {
                pump->failureDetail = failureDetail;
            }
            break;
        }

        std::lock_guard<std::mutex> lock(pump->mutex);
        pump->stats.totalPipeWriteNs += writeNs;
        if (writeNs > pump->stats.maxPipeWriteNs) {
            pump->stats.maxPipeWriteNs = writeNs;
            pump->stats.maxPipeWriteFrame = packet.frameIndex;
        }
    }

    shutdownRawVideoPipe(&pump->pipe);
    {
        std::lock_guard<std::mutex> lock(pump->mutex);
        pump->writerFinished = true;
    }
    pump->notFull.notify_all();
    pump->notEmpty.notify_all();
}

}  // namespace

RawVideoPipePlan chooseRawVideoPipePlan(const QSize& frameSize)
{
    RawVideoPipePlan plan;
    const qint64 width = qMax(1, frameSize.width());
    const qint64 height = qMax(1, frameSize.height());
    const qint64 pixelCount = width * height;
    const qint64 baselinePixelCount = kRawPipeBaselineWidth * kRawPipeBaselineHeight;
    plan.frameBytes = width * height * 4LL;
    plan.requestedBufferBytes = qMax(plan.frameBytes, 1LL * 1024LL * 1024LL);
    const double scaledBufferedFrames =
        static_cast<double>(kRawPipeBaselineBufferedFrames) * static_cast<double>(baselinePixelCount)
        / static_cast<double>(qMax<qint64>(1, pixelCount));
    plan.maxBufferedFrames = qBound(
        kRawPipeMinBufferedFrames,
        qRound(scaledBufferedFrames),
        kRawPipeMaxBufferedFrames);
    return plan;
}

QString rawVideoPipeTransportName(RawVideoPipeTransport transport)
{
    switch (transport) {
    case RawVideoPipeTransport::NamedPipe:
        return QStringLiteral("named_pipe");
    case RawVideoPipeTransport::Fifo:
        return QStringLiteral("fifo");
    }
    return QStringLiteral("unknown");
}

bool startRawVideoPipe(
    RawVideoPipe* pipe,
    const QString& tempDirPath,
    const RawVideoPipePlan& plan,
    QString* failureDetail)
{
    if (pipe == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw video pipe");
        }
        return false;
    }

    pipe->requestedBufferBytes = plan.requestedBufferBytes;
    pipe->configuredBufferBytes = 0;

#ifdef Q_OS_WIN
    const QString pipePath = QStringLiteral("\\\\.\\pipe\\miacode-export-%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const DWORD bufferBytes = static_cast<DWORD>(qBound<qint64>(
        64LL * 1024LL,
        plan.requestedBufferBytes,
        static_cast<qint64>(MAXDWORD)));
    HANDLE handle = CreateNamedPipeW(
        reinterpret_cast<LPCWSTR>(pipePath.utf16()),
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        bufferBytes,
        bufferBytes,
        0,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("CreateNamedPipe failed error=%1").arg(GetLastError());
        }
        return false;
    }
    pipe->inputPath = pipePath;
    pipe->configuredBufferBytes = bufferBytes;
    pipe->nativeHandle = reinterpret_cast<qintptr>(handle);
#else
    const QString fifoPath = QDir(tempDirPath).filePath(QStringLiteral("ffmpeg_rawvideo_fifo"));
    QFile::remove(fifoPath);
    const QByteArray fifoPathBytes = QFile::encodeName(fifoPath);
    if (::mkfifo(fifoPathBytes.constData(), 0600) != 0) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("mkfifo failed path=%1 errno=%2").arg(fifoPath).arg(errno);
        }
        return false;
    }
    pipe->inputPath = fifoPath;
    pipe->fifoPath = fifoPath;
#endif
    return true;
}

bool startRawVideoPipePumpThread(RawVideoPipePump* pump, QString* failureDetail)
{
    if (pump == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw pipe pump");
        }
        return false;
    }
    try {
        pump->writerThread = std::thread([pump]() { rawVideoPipeWriterMain(pump); });
        pump->writerThreadStarted = true;
    } catch (const std::exception& ex) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("failed to start raw pipe writer thread: %1")
                                 .arg(QString::fromUtf8(ex.what()));
        }
        return false;
    } catch (...) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("failed to start raw pipe writer thread");
        }
        return false;
    }
    return true;
}

bool enqueueRawVideoFrame(
    RawVideoPipePump* pump,
    QByteArray frameBytes,
    int frameIndex,
    qint64* producerWaitNs,
    int* queuedFramesAfterEnqueue,
    QString* failureDetail)
{
    if (producerWaitNs != nullptr) {
        *producerWaitNs = 0;
    }
    if (queuedFramesAfterEnqueue != nullptr) {
        *queuedFramesAfterEnqueue = 0;
    }
    if (pump == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw pipe pump enqueue");
        }
        return false;
    }

    QElapsedTimer waitTimer;
    bool waited = false;
    int queuedFrames = 0;
    {
        std::unique_lock<std::mutex> lock(pump->mutex);
        while (true) {
            if (!pump->failureDetail.isEmpty()) {
                if (failureDetail != nullptr) {
                    *failureDetail = pump->failureDetail;
                }
                return false;
            }
            if (pump->writerFinished && pump->queue.empty()) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("raw pipe writer finished before enqueue");
                }
                return false;
            }
            if (pump->queue.size() < static_cast<size_t>(pump->plan.maxBufferedFrames)) {
                break;
            }
            if (!waited) {
                waitTimer.start();
                waited = true;
            }
            pump->notFull.wait_for(lock, std::chrono::milliseconds(20));
        }

        RawVideoPipePacket packet;
        packet.bytes = std::move(frameBytes);
        packet.frameIndex = frameIndex;
        pump->queue.emplace_back(std::move(packet));
        queuedFrames = static_cast<int>(pump->queue.size());
        pump->stats.maxQueuedFrames = qMax(pump->stats.maxQueuedFrames, queuedFrames);
        if (waited) {
            const qint64 waitedNs = waitTimer.nsecsElapsed();
            pump->stats.totalProducerWaitNs += waitedNs;
            if (waitedNs > pump->stats.maxProducerWaitNs) {
                pump->stats.maxProducerWaitNs = waitedNs;
                pump->stats.maxProducerWaitFrame = frameIndex;
            }
            if (producerWaitNs != nullptr) {
                *producerWaitNs = waitedNs;
            }
        }
    }

    if (queuedFramesAfterEnqueue != nullptr) {
        *queuedFramesAfterEnqueue = queuedFrames;
    }
    pump->notEmpty.notify_one();
    return true;
}

void shutdownRawVideoPipe(RawVideoPipe* pipe)
{
    if (pipe == nullptr) {
        return;
    }
#ifdef Q_OS_WIN
    HANDLE handle = rawVideoPipeHandle(*pipe);
    if (handle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(handle);
        CloseHandle(handle);
        pipe->nativeHandle = -1;
    }
#else
    if (pipe->fd >= 0) {
        ::close(pipe->fd);
        pipe->fd = -1;
    }
    if (!pipe->fifoPath.isEmpty()) {
        QFile::remove(pipe->fifoPath);
        pipe->fifoPath.clear();
    }
#endif
}

void shutdownRawVideoPipePump(RawVideoPipePump* pump)
{
    if (pump == nullptr) {
        return;
    }
    pump->abortRequested.store(true);
    {
        std::lock_guard<std::mutex> lock(pump->mutex);
        pump->enqueueClosed = true;
    }
    pump->notEmpty.notify_all();
    pump->notFull.notify_all();
    if (pump->writerThreadStarted && pump->writerThread.joinable()) {
        pump->writerThread.join();
        pump->writerThreadStarted = false;
    }
    shutdownRawVideoPipe(&pump->pipe);
}

bool finishRawVideoPipePump(RawVideoPipePump* pump, QString* failureDetail)
{
    if (pump == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid raw pipe pump finalize");
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(pump->mutex);
        pump->enqueueClosed = true;
    }
    pump->notEmpty.notify_all();
    pump->notFull.notify_all();
    if (pump->writerThreadStarted && pump->writerThread.joinable()) {
        pump->writerThread.join();
        pump->writerThreadStarted = false;
    }
    shutdownRawVideoPipe(&pump->pipe);

    std::lock_guard<std::mutex> lock(pump->mutex);
    if (!pump->failureDetail.isEmpty()) {
        if (failureDetail != nullptr) {
            *failureDetail = pump->failureDetail;
        }
        return false;
    }
    return true;
}

}  // namespace miacode::video_export::raw_pipe
