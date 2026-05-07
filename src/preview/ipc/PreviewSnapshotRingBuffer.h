#pragma once

// Single-writer single-reader latest-only snapshot ring buffer over shared
// memory. The editor process is the writer (producing one snapshot per
// audio tick / vsync); the preview worker process is the reader (consumes
// the most recent snapshot at its own render cadence). When the writer
// outpaces the reader, intermediate snapshots are dropped — visually
// equivalent to legitimate frame skips.
//
// Layout: a small fixed header followed by `slotCount` slots of
// `slotByteSize` bytes each. The header carries a monotonically increasing
// `publishedSequence`; slot `seq % slotCount` holds the most recent fully-
// published payload. Readers do a "sequence parity" check (read the seq,
// memcpy the slot, re-read the seq; if they differ, the write was concurrent
// → retry).
//
// Cross-process atomicity:
//   - Aligned 8-byte loads/stores are atomic on x64.
//   - `std::atomic_thread_fence(std::memory_order_seq_cst)` between the
//     payload memcpy and the sequence update gives release/acquire ordering.
//
// Uses QSharedMemory for allocation + mapping; the key is published in the
// JSON "attach" command, so the worker maps the same region.
//
// See docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md
// section 6 (Phase 1).

#include "preview/ipc/PreviewFrameStateSerial.h"

#include <QByteArray>
#include <QSharedMemory>
#include <QString>

#include <cstdint>
#include <memory>

namespace miacode::preview::ipc {

inline constexpr int kSnapshotRingDefaultSlots = 4;

struct PreviewSnapshotRingHeader
{
    quint32 protocolVersion;   // == kPreviewWorkerProtocolVersion
    quint32 layoutVersion;     // == kSerialLayoutVersion
    quint32 slotByteSize;      // == sizeof(PreviewFrameStateSerial)
    quint32 slotCount;         // typically kSnapshotRingDefaultSlots
    quint64 publishedSequence; // monotonically increasing; 0 == no payload yet
    quint64 reserved[5];       // pad to 64 bytes for cache-line alignment
};

static_assert(sizeof(PreviewSnapshotRingHeader) == 64,
              "ring header must be cache-line sized for cross-process atomic access");

class PreviewSnapshotRingBuffer
{
public:
    PreviewSnapshotRingBuffer();
    ~PreviewSnapshotRingBuffer();

    PreviewSnapshotRingBuffer(const PreviewSnapshotRingBuffer&) = delete;
    PreviewSnapshotRingBuffer& operator=(const PreviewSnapshotRingBuffer&) = delete;

    // Editor-side: create + map the shared region with `slotCount` slots.
    // Generates a unique key prefixed with `prefix` and exposes it via key().
    // Returns false on allocation failure with details in errorOut. Must be
    // called once before publish().
    bool createAsPublisher(const QString& prefix, int slotCount, QString* errorOut);

    // Worker-side: attach to an already-created shared region by key.
    // Validates protocol/layout versions; returns false if mismatched (the
    // worker should then exit and let the supervisor respawn).
    bool attachAsConsumer(const QString& key, QString* errorOut);

    // Detach from the shared region. Idempotent.
    void detach();

    // Editor-side: publish a new snapshot. Sets snapshot.sequence and
    // snapshot.publishMonotonicNs as part of the publish; the caller fills
    // every other field. Cheap (single memcpy of sizeof(PreviewFrameStateSerial)
    // plus an 8-byte release store). Returns false if not attached.
    bool publish(PreviewFrameStateSerial& snapshotInOut);

    // Worker-side: try to read the most recent snapshot into `out`. Returns
    // true on success, false if (a) no snapshot has been published yet
    // (publishedSequence == 0) or (b) the consumer is not attached. Tolerates
    // concurrent writers via sequence-parity retry — bounded retry count is
    // logged via debug_log if the writer is racing pathologically.
    bool readLatest(PreviewFrameStateSerial* out) const;

    // True iff createAsPublisher / attachAsConsumer succeeded and the
    // shared memory mapping is currently active.
    bool isAttached() const;

    QString key() const;
    int slotByteSize() const;
    int slotCount() const;

private:
    QSharedMemory shm_;
    QString key_;
    int slotByteSize_ = 0;
    int slotCount_ = 0;
    bool isPublisher_ = false;
    quint64 publishedSequenceLocal_ = 0;
};

}  // namespace miacode::preview::ipc
