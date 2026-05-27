#pragma once

#include <QPair>
#include <QString>
#include <QVector>

namespace miacode::latency_analysis {

// Default analysis sample rate (mono PCM target for decoding).
constexpr int kAnalysisSampleRate = 24000;

struct DecodedAudio {
    QVector<float> samples;
    int sampleRate = 0;
    double durationSeconds = 0.0;
};

// Output of either onset (energy-flux) or transient (abs-diff) envelope
// builders. `stepSeconds` is the time per sample of `values`.
struct Envelope {
    QVector<float> values;
    double stepSeconds = 0.0;

    bool isEmpty() const { return values.isEmpty() || stepSeconds <= 0.0; }
};

struct BpmDetectionResult {
    double bpm = 0.0;  // 0 if detection failed
    QString meterId = QStringLiteral("4/4");
    double meterPhaseSeconds = 0.0;
    bool meterPhaseValid = false;
    // BPM candidates sorted best-first; first entry is always the selected bpm.
    QVector<QPair<double, double>> candidates;
};

struct OffsetDetectionInputs {
    double bpm = 0.0;
    double offsetAnchorSeconds = 0.0;
    double trackDurationSeconds = 0.0;
    // "auto" / "4/4" / "3/4" / "6/8" / "7/4"  — controls which meter
    // pattern the algorithm scores phases against.
    QString meterId = QStringLiteral("auto");
    // "bar" / "quarter" / "eighth" — snap granularity of the result.
    QString snapMode = QStringLiteral("bar");
    // Optional state carried over from the most recent detectBpm() call.
    // Improves offset detection when the BPM step has already locked the
    // bar phase; safe to leave at defaults.
    double lastDetectedMeterPhase = 0.0;
    bool hasLastDetectedMeterPhase = false;
    QString lastDetectedMeterId = QStringLiteral("4/4");
};

// Decode an audio file to mono float PCM at the requested sample rate.
// Returns an empty DecodedAudio on failure (missing file, decoder error).
DecodedAudio decodeMonoTrack(const QString& trackPath, int sampleRate = kAnalysisSampleRate);

// Energy-flux envelope used as the primary signal for BPM detection.
Envelope buildOnsetEnvelope(const QVector<float>& samples, int sampleRate);

// Abs-diff transient envelope used to refine offset detection.
Envelope buildTransientEnvelope(const QVector<float>& samples, int sampleRate);

// Estimate BPM (and optionally a meter pattern) from the onset envelope.
// `meterIdHint`: "auto" tries multiple meter families; specific ids
// ("4/4", "3/4", "6/8", "7/4") bias the search to that family.
BpmDetectionResult detectBpm(
    const Envelope& onsetEnvelope,
    const QString& meterIdHint = QStringLiteral("auto"));

// Estimate the chart offset (in seconds, signed) using the onset and
// transient envelopes plus contextual hints from the inputs struct.
double detectOffset(
    const Envelope& onsetEnvelope,
    const Envelope& transientEnvelope,
    const OffsetDetectionInputs& inputs);

}  // namespace miacode::latency_analysis
