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

// Tunable weights for the envelope builders and the offset scorer. Every
// field defaults to the value the algorithm shipped with, so passing a
// default-constructed DetectionTuning reproduces the production behavior
// exactly — the GUI callers rely on this. The batch-test dev tool
// (`latency_offset_batch`) exposes these as CLI flags so the onset/transient
// mix and the phase penalties can be swept without recompiling.
struct DetectionTuning {
    // --- Onset (energy-flux) envelope, buildOnsetEnvelope() ---
    // Per-frame energy = rmsWeight*RMS + meanAbsWeight*meanAbs.
    double onsetRmsWeight = 0.60;
    double onsetMeanAbsWeight = 0.40;
    // IIR baseline used to half-wave-rectify the flux:
    //   baseline = decay*baseline + (1-decay)*energy.
    double onsetBaselineDecay = 0.88;

    // --- Offset scoring, detectOffset() ---
    // Per-beat phase score = transientWeight*transientEnv + onsetWeight*onsetEnv.
    double offsetTransientWeight = 0.75;
    double offsetOnsetWeight = 0.25;
    // Score -= phasePenalty * |phase|  (biases toward small offsets). Raised
    // from 0.06 to 0.14 after Phase-0 corpus tuning (docs/OFFSET_DETECTION_
    // BASELINE_v1_ZH.md): the only safe single-knob win — pulls the +24ms
    // soft-onset cluster (which sits on first≈0 charts) back toward zero.
    double offsetPhasePenalty = 0.14;
    // Score -= meterPenalty * dist-to-meter-phase  (bar snap mode only).
    double offsetMeterPenalty = 0.14;
    // Phase 1 — rising-edge onset emphasis. Blends the transient envelope with
    // its (smoothed) positive slope before phase scoring, so the detector locks
    // onto the attack's rising edge rather than its later amplitude peak. This
    // is the signal-adaptive fix for the ~+24ms soft-onset group delay: for
    // sharp onsets the slope peaks at the same place (no shift, protects the
    // already-accurate charts); for soft onsets it leads the peak. 0 reproduces
    // the original envelope exactly. Default 0.5 from Phase-1 corpus tuning:
    // near-optimal on BOTH the tuning set (<local-chart-root> 64.9%) and the held-out set
    // (<local-test-set> 60.0%); the tuning-set peak at 0.6 was dropped as it regressed
    // on the holdout (overfit). Pure slope (1.0) is noisy and collapses.
    double offsetEdgeWeight = 0.5;
    // A snap candidate is only accepted when its score is at least
    // snapThreshold * bestScore. Set > 1.0 to disable snapping entirely
    // (keeps the raw fine-search phase) — useful for measuring unbiased
    // detection accuracy.
    double offsetSnapThreshold = 0.90;
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
// `tuning` controls the energy mix + baseline decay; defaults reproduce the
// production envelope.
Envelope buildOnsetEnvelope(
    const QVector<float>& samples,
    int sampleRate,
    const DetectionTuning& tuning = DetectionTuning());

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
    const OffsetDetectionInputs& inputs,
    const DetectionTuning& tuning = DetectionTuning());

}  // namespace miacode::latency_analysis
