#include "preview/ipc/PreviewSnapshotRingBuffer.h"

#include "common/DebugLog.h"
#include "preview/ipc/PreviewWorkerProtocol.h"

#include <QUuid>

#include <atomic>
#include <chrono>
#include <cstring>

namespace {

void appendIpcLog(const QString& tag, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/snapshot_ring"),
        QStringLiteral("tag=%1 %2").arg(tag, payload)
    );
}

}  // namespace

namespace miacode::preview::ipc {

PreviewSnapshotRingBuffer::PreviewSnapshotRingBuffer() = default;

PreviewSnapshotRingBuffer::~PreviewSnapshotRingBuffer()
{
    detach();
}

bool PreviewSnapshotRingBuffer::createAsPublisher(const QString& prefix, int slotCount, QString* errorOut)
{
    if (shm_.isAttached()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("ring buffer already attached");
        }
        return false;
    }
    if (slotCount <= 0 || slotCount > 32) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("invalid slot count %1").arg(slotCount);
        }
        return false;
    }

    slotByteSize_ = static_cast<int>(sizeof(PreviewFrameStateSerial));
    slotCount_ = slotCount;
    isPublisher_ = true;

    const QString uuidTail = QUuid::createUuid().toString(QUuid::WithoutBraces);
    key_ = QStringLiteral("%1_%2").arg(prefix.isEmpty() ? QStringLiteral("miacode_preview") : prefix,
                                       uuidTail);
    shm_.setKey(key_);

    const int totalBytes = static_cast<int>(sizeof(PreviewSnapshotRingHeader))
                           + slotByteSize_ * slotCount_;
    if (!shm_.create(totalBytes, QSharedMemory::ReadWrite)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("QSharedMemory::create failed: %1")
                            .arg(shm_.errorString());
        }
        appendIpcLog(QStringLiteral("create_failed"),
                     QStringLiteral("key=%1 bytes=%2 err=%3")
                         .arg(key_)
                         .arg(totalBytes)
                         .arg(shm_.errorString()));
        return false;
    }

    if (!shm_.lock()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("QSharedMemory::lock failed: %1")
                            .arg(shm_.errorString());
        }
        return false;
    }
    auto* header = static_cast<PreviewSnapshotRingHeader*>(shm_.data());
    header->protocolVersion = static_cast<quint32>(kPreviewWorkerProtocolVersion);
    header->layoutVersion = kSerialLayoutVersion;
    header->slotByteSize = static_cast<quint32>(slotByteSize_);
    header->slotCount = static_cast<quint32>(slotCount_);
    header->publishedSequence = 0;
    std::memset(header->reserved, 0, sizeof(header->reserved));
    auto* slotBase = reinterpret_cast<char*>(shm_.data())
                  + sizeof(PreviewSnapshotRingHeader);
    std::memset(slotBase, 0, static_cast<size_t>(slotByteSize_) * slotCount_);
    shm_.unlock();

    appendIpcLog(QStringLiteral("created"),
                 QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3 total_bytes=%4")
                     .arg(key_)
                     .arg(slotByteSize_)
                     .arg(slotCount_)
                     .arg(totalBytes));
    return true;
}

bool PreviewSnapshotRingBuffer::attachAsConsumer(const QString& key, QString* errorOut)
{
    if (shm_.isAttached()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("ring buffer already attached");
        }
        return false;
    }
    key_ = key;
    isPublisher_ = false;
    shm_.setKey(key_);
    if (!shm_.attach(QSharedMemory::ReadOnly)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("QSharedMemory::attach failed: %1")
                            .arg(shm_.errorString());
        }
        appendIpcLog(QStringLiteral("attach_failed"),
                     QStringLiteral("key=%1 err=%2").arg(key_, shm_.errorString()));
        return false;
    }

    const auto* header = static_cast<const PreviewSnapshotRingHeader*>(shm_.constData());
    if (header->protocolVersion != static_cast<quint32>(kPreviewWorkerProtocolVersion)
        || header->layoutVersion != kSerialLayoutVersion) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral(
                "protocol/layout mismatch: ring proto=%1/%2 worker=%3/%4")
                .arg(header->protocolVersion)
                .arg(header->layoutVersion)
                .arg(kPreviewWorkerProtocolVersion)
                .arg(kSerialLayoutVersion);
        }
        shm_.detach();
        return false;
    }

    slotByteSize_ = static_cast<int>(header->slotByteSize);
    slotCount_ = static_cast<int>(header->slotCount);
    if (slotByteSize_ != static_cast<int>(sizeof(PreviewFrameStateSerial))) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral(
                "slot byte size mismatch: ring=%1 worker=%2")
                .arg(slotByteSize_)
                .arg(static_cast<int>(sizeof(PreviewFrameStateSerial)));
        }
        shm_.detach();
        return false;
    }

    appendIpcLog(QStringLiteral("attached"),
                 QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3")
                     .arg(key_).arg(slotByteSize_).arg(slotCount_));
    return true;
}

void PreviewSnapshotRingBuffer::detach()
{
    if (shm_.isAttached()) {
        shm_.detach();
    }
    publishedSequenceLocal_ = 0;
}

bool PreviewSnapshotRingBuffer::publish(PreviewFrameStateSerial& snapshotInOut)
{
    if (!isPublisher_ || !shm_.isAttached()) {
        return false;
    }

    char* base = static_cast<char*>(shm_.data());
    auto* header = reinterpret_cast<PreviewSnapshotRingHeader*>(base);
    char* ringSlots = base + sizeof(PreviewSnapshotRingHeader);

    // Pre-increment the local counter; sequence written into the slot is the
    // sequence at which the slot becomes published.
    const quint64 nextSeq = publishedSequenceLocal_ + 1;
    const int slotIndex = static_cast<int>(nextSeq % static_cast<quint64>(slotCount_));
    char* slotBase = ringSlots + static_cast<size_t>(slotIndex) * slotByteSize_;

    snapshotInOut.sequence = nextSeq;
    // steady_clock is monotonic and ns-precision; matches what the consumer
    // uses for latency = now_ns - publishMonotonicNs. QDateTime is wall-clock
    // and ms-precision — would dominate the latency budget.
    snapshotInOut.publishMonotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count();

    std::memcpy(slotBase, &snapshotInOut, sizeof(PreviewFrameStateSerial));

    // Release fence pairs with the consumer's acquire load on header->publishedSequence.
    std::atomic_thread_fence(std::memory_order_release);

    // Aligned 8-byte store is atomic on x64.
    *reinterpret_cast<volatile quint64*>(&header->publishedSequence) = nextSeq;

    publishedSequenceLocal_ = nextSeq;
    return true;
}

bool PreviewSnapshotRingBuffer::readLatest(PreviewFrameStateSerial* out) const
{
    if (out == nullptr || !shm_.isAttached()) {
        return false;
    }

    const char* base = static_cast<const char*>(shm_.constData());
    const auto* header = reinterpret_cast<const PreviewSnapshotRingHeader*>(base);
    const char* ringSlots = base + sizeof(PreviewSnapshotRingHeader);

    constexpr int kMaxRetries = 8;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        const quint64 seqBefore = *reinterpret_cast<const volatile quint64*>(&header->publishedSequence);
        if (seqBefore == 0) {
            return false;
        }
        std::atomic_thread_fence(std::memory_order_acquire);

        const int slotIndex = static_cast<int>(seqBefore % static_cast<quint64>(slotCount_));
        const char* slotBase = ringSlots + static_cast<size_t>(slotIndex) * slotByteSize_;
        std::memcpy(out, slotBase, sizeof(PreviewFrameStateSerial));

        std::atomic_thread_fence(std::memory_order_acquire);
        const quint64 seqAfter = *reinterpret_cast<const volatile quint64*>(&header->publishedSequence);

        if (seqAfter == seqBefore && seqAfter == out->sequence) {
            // Clean read.
            return true;
        }
        // Writer raced past us mid-copy. With latest-only semantics we
        // retry; up to kMaxRetries which is enormous given typical 16.7 ms
        // write cadence vs sub-microsecond memcpy.
    }

    appendIpcLog(QStringLiteral("read_retry_exhausted"),
                 QStringLiteral("attempts=%1").arg(kMaxRetries));
    return false;
}

bool PreviewSnapshotRingBuffer::isAttached() const
{
    return shm_.isAttached();
}

QString PreviewSnapshotRingBuffer::key() const
{
    return key_;
}

int PreviewSnapshotRingBuffer::slotByteSize() const
{
    return slotByteSize_;
}

int PreviewSnapshotRingBuffer::slotCount() const
{
    return slotCount_;
}

}  // namespace miacode::preview::ipc
