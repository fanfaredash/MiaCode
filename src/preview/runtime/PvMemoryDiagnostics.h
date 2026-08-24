#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

#include <optional>

// Pure accounting for preview-video memory diagnostics. The host supplies only
// scalar observations it has already obtained; this component owns neither
// media objects, conversion work, timers, nor logging.
namespace miacode::preview::pv_memory {

// The host owns scheduling; these scalar values identify the two approved
// post-clear checkpoint callbacks and keep duplicate delivery idempotent.
inline constexpr qint64 kPostClear3SecondsMs = 3000;
inline constexpr qint64 kPostClear15SecondsMs = 15000;

struct ProcessMemorySample {
    qint64 residentBytes = -1;
    qint64 footprintBytes = -1;
    qint64 internalBytes = -1;
    qint64 compressedBytes = -1;
};

struct MediaFacts {
    bool mediaVisible = false;
    QString playbackState = QStringLiteral("unknown");
    QString mediaStatus = QStringLiteral("unknown");
    qint64 positionMs = -1;
    bool videoOutputAttached = false;
    bool videoSinkAttached = false;
};

struct Observation {
    qint64 elapsedMs = 0;
    MediaFacts media;
    ProcessMemorySample process;
};

struct FrameMetadata {
    qint64 width = -1;
    qint64 height = -1;
    QString pixelFormat = QStringLiteral("unknown");
};

struct ImageConversionFact {
    bool attempted = false;
    bool succeeded = false;
    qint64 elapsedMs = -1;
    qint64 resultBytes = -1;
};

enum class MediaKind {
    None,
    Image,
    Video,
};

enum class BoundaryReason {
    Play,
    Pause,
    EndOfMedia,
    OutputAttach,
    OutputDetach,
    PlayerDestroyBefore,
    PlayerDestroyAfter,
};

enum class RecordAction {
    Boundary,
    Sample,
    Summary,
};

struct Record {
    RecordAction action = RecordAction::Boundary;
    QString reason;
    quint64 sourceId = 0;
    quint64 clearEpoch = 0;
    bool snapshot = false;
    bool noMediaAfterClear = false;
    qint64 sourceElapsedMs = 0;
    MediaFacts media;
    quint64 frameCount = 0;
    FrameMetadata frame;
    quint64 toImageAttempts = 0;
    quint64 toImageSuccesses = 0;
    quint64 toImageNulls = 0;
    qint64 toImageTotalMs = 0;
    qint64 toImageMaxMs = 0;
    qint64 toImageLastBytes = -1;
    qint64 toImagePeakBytes = 0;
    qint64 toImageOutputBytesEstimate = 0;
    ProcessMemorySample process;
    ProcessMemorySample firstProcess;
    ProcessMemorySample lastProcess;
    ProcessMemorySample peakProcess;
};

class Diagnostics final {
public:
    QVector<Record> beginSource(MediaKind kind, const Observation& observation);
    QVector<Record> observeFrame(
        const Observation& observation,
        const FrameMetadata& frame,
        const ImageConversionFact& conversion);
    std::optional<Record> boundary(BoundaryReason reason, const Observation& observation);
    std::optional<Record> sample(const Observation& observation);
    QVector<Record> clear(const Observation& observation);
    QVector<Record> lateNoMedia(const ProcessMemorySample& process);
    QVector<Record> postClearCheckpoint(
        quint64 clearEpoch,
        qint64 delayMs,
        const ProcessMemorySample& process);
    QVector<Record> recover(
        MediaKind replacementKind,
        const Observation& oldObservation,
        const Observation& replacementObservation);
    std::optional<Record> destroy(const Observation& observation);

    [[nodiscard]] bool hasCurrentSource() const;
    [[nodiscard]] static QString formatPayload(const Record& record);

private:
    struct SourceState {
        quint64 sourceId = 0;
        qint64 startElapsedMs = 0;
        qint64 lastElapsedMs = 0;
        MediaFacts media;
        quint64 frameCount = 0;
        FrameMetadata frame;
        quint64 toImageAttempts = 0;
        quint64 toImageSuccesses = 0;
        quint64 toImageNulls = 0;
        qint64 toImageTotalMs = 0;
        qint64 toImageMaxMs = 0;
        qint64 toImageLastBytes = -1;
        qint64 toImagePeakBytes = 0;
        qint64 toImageOutputBytesEstimate = 0;
        ProcessMemorySample firstProcess;
        ProcessMemorySample lastProcess;
        ProcessMemorySample peakProcess;
    };

    struct ClearSnapshot {
        SourceState state;
        quint64 clearEpoch = 0;
        bool checkpointsCanceled = false;
        bool checkpoint3sEmitted = false;
        bool checkpoint15sEmitted = false;
        bool noMediaObserved = false;
    };

    static void update(SourceState* state, const Observation& observation);
    static QString boundaryReasonName(BoundaryReason reason);
    static Record makeRecord(
        const SourceState& state,
        RecordAction action,
        const QString& reason,
        const ProcessMemorySample& currentProcess,
        quint64 clearEpoch = 0,
        bool snapshot = false,
        bool noMediaAfterClear = false);
    static Record finalizeClearSnapshot(const ClearSnapshot& snapshot);

    quint64 nextSourceId_ = 1;
    quint64 clearEpoch_ = 0;
    std::optional<SourceState> current_;
    std::optional<ClearSnapshot> pendingClear_;
};

}  // namespace miacode::preview::pv_memory
