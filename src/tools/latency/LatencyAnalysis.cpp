#include "LatencyAnalysis.h"

#include <QtMath>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

#include "common/MiniaudioFileAccess.h"

namespace miacode::latency_analysis {

namespace {

constexpr int kAnalysisWindowSize = 1024;
constexpr int kAnalysisHopSize = 512;
constexpr int kOffsetWindowSize = 512;
constexpr int kOffsetHopSize = 128;
constexpr double kMinDetectBpm = 50.0;
constexpr double kMaxDetectBpm = 300.0;
constexpr double kOffsetPhasePenalty = 0.06;
constexpr double kOffsetSnapThreshold = 0.90;

struct MeterPattern {
    const char* id = "";
    const char* label = "";
    QVector<double> accentWeights;
    QVector<double> candidateMultipliers;
};

const QVector<MeterPattern>& meterPatterns()
{
    static const QVector<MeterPattern> patterns{
        {"4/4", "4/4", {1.00, 0.45, 0.30, 0.20}, {0.5, 1.0, 2.0}},
        {"3/4", "3/4", {1.00, 0.46, 0.30}, {0.5, 1.0, 2.0}},
        {"6/8", "6/8", {1.00, 0.52, 0.38, 0.84, 0.50, 0.36}, {0.5, 1.0, 2.0, 3.0}},
        {"7/4", "7/4", {1.00, 0.44, 0.34, 0.82, 0.42, 0.32, 0.26}, {0.5, 1.0, 2.0}},
        {"auto", "Auto Detect", {1.00, 0.45, 0.30, 0.20}, {0.5, 1.0, 2.0, 3.0}},
    };
    return patterns;
}

const MeterPattern* meterPatternById(const QString& id)
{
    for (const MeterPattern& pattern : meterPatterns()) {
        if (id == QLatin1String(pattern.id)) {
            return &pattern;
        }
    }
    return nullptr;
}

QVector<const MeterPattern*> candidatePatternsForId(const QString& id)
{
    QVector<const MeterPattern*> result;
    if (id == QLatin1String("auto")) {
        for (const MeterPattern& pattern : meterPatterns()) {
            if (QString::fromLatin1(pattern.id) == QLatin1String("auto")) {
                continue;
            }
            result.append(&pattern);
        }
        return result;
    }
    if (const MeterPattern* pattern = meterPatternById(id); pattern != nullptr) {
        result.append(pattern);
    }
    return result;
}

double correlationAtLag(const QVector<float>& envelope, int lag)
{
    if (lag <= 0 || lag >= envelope.size()) {
        return 0.0;
    }
    double score = 0.0;
    for (int i = lag; i < envelope.size(); ++i) {
        score += static_cast<double>(envelope.at(i)) * static_cast<double>(envelope.at(i - lag));
    }
    return score / static_cast<double>(envelope.size() - lag);
}

double sampleEnvelopeLinear(const QVector<float>& values, double index)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    if (index <= 0.0) {
        return values.first();
    }
    if (index >= static_cast<double>(values.size() - 1)) {
        return values.last();
    }
    const int lo = static_cast<int>(qFloor(index));
    const int hi = qMin(lo + 1, values.size() - 1);
    const double t = index - static_cast<double>(lo);
    return (1.0 - t) * values.at(lo) + t * values.at(hi);
}

struct TempoAlignmentResult {
    double bpm = 0.0;
    double score = 0.0;
    double phase = 0.0;
    const MeterPattern* pattern = nullptr;
};

TempoAlignmentResult bestTempoAlignmentNear(
    const QVector<float>& envelope,
    double stepSeconds,
    double centerBpm,
    double rangeBpm,
    const MeterPattern& pattern
)
{
    TempoAlignmentResult best;
    if (envelope.isEmpty() || stepSeconds <= 0.0 || centerBpm <= 0.0) {
        return best;
    }

    auto evaluateBpm = [&](double bpm) {
        const double beatPeriod = 60.0 / bpm;
        const double barPeriod = beatPeriod * qMax(1, pattern.accentWeights.size());
        if (beatPeriod <= 0.0 || barPeriod <= 0.0) {
            return;
        }
        const double phaseStep = qMin(0.010, beatPeriod / 24.0);
        double bestPhaseScore = 0.0;
        double bestPhase = 0.0;
        for (double phase = 0.0; phase < barPeriod; phase += phaseStep) {
            double total = 0.0;
            int count = 0;
            for (double barStart = phase; barStart < envelope.size() * stepSeconds; barStart += barPeriod) {
                for (int pulseIndex = 0; pulseIndex < pattern.accentWeights.size(); ++pulseIndex) {
                    total += sampleEnvelopeLinear(
                        envelope,
                        (barStart + beatPeriod * static_cast<double>(pulseIndex)) / stepSeconds
                    ) * pattern.accentWeights.at(pulseIndex);
                }
                ++count;
            }
            if (count <= 0) {
                continue;
            }
            const double normalized = total / static_cast<double>(count);
            if (normalized > bestPhaseScore) {
                bestPhaseScore = normalized;
                bestPhase = phase;
            }
        }
        if (bestPhaseScore > best.score) {
            best.bpm = bpm;
            best.score = bestPhaseScore;
            best.phase = bestPhase;
            best.pattern = &pattern;
        }
    };

    auto scanBpmRange = [&](double minBpm, double maxBpm, double stepBpm) {
        if (stepBpm <= 0.0) {
            return;
        }
        double bpm = std::ceil(minBpm / stepBpm) * stepBpm;
        if (bpm < minBpm - 1e-9) {
            bpm += stepBpm;
        }
        for (; bpm <= maxBpm + 1e-6; bpm += stepBpm) {
            evaluateBpm(bpm);
        }
    };

    const double minBpm = qMax(kMinDetectBpm, centerBpm - rangeBpm);
    const double maxBpm = qMin(kMaxDetectBpm, centerBpm + rangeBpm);
    scanBpmRange(minBpm, maxBpm, 0.05);

    if (best.bpm > 0.0) {
        const double refineMinBpm = qMax(minBpm, best.bpm - 0.12);
        const double refineMaxBpm = qMin(maxBpm, best.bpm + 0.12);
        scanBpmRange(refineMinBpm, refineMaxBpm, 0.05);
    }
    return best;
}

void cacheBpmCandidates(
    double selectedBpm,
    const QVector<TempoAlignmentResult>& sourceCandidates,
    QVector<QPair<double, double>>* out)
{
    if (out == nullptr) {
        return;
    }
    QVector<QPair<double, double>> ranked;
    ranked.reserve(sourceCandidates.size());
    for (const TempoAlignmentResult& candidate : sourceCandidates) {
        if (candidate.bpm <= 0.0 || candidate.score <= 0.0) {
            continue;
        }
        ranked.append({candidate.bpm, candidate.score});
    }
    std::sort(ranked.begin(), ranked.end(), [](const QPair<double, double>& lhs, const QPair<double, double>& rhs) {
        if (!qFuzzyCompare(lhs.second + 1.0, rhs.second + 1.0)) {
            return lhs.second > rhs.second;
        }
        return lhs.first > rhs.first;
    });

    QVector<QPair<double, double>> unique;
    for (const auto& entry : ranked) {
        bool duplicate = false;
        for (const auto& existing : unique) {
            if (qAbs(existing.first - entry.first) <= 0.05) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique.append(entry);
        }
        if (unique.size() >= 12) {
            break;
        }
    }

    double selectedScore = 0.0;
    for (const auto& entry : unique) {
        if (qAbs(entry.first - selectedBpm) <= 0.05) {
            selectedScore = qMax(selectedScore, entry.second);
        }
    }

    QVector<QPair<double, double>> finalList;
    if (selectedBpm > 0.0) {
        finalList.append({selectedBpm, selectedScore});
    }
    for (const auto& entry : unique) {
        if (selectedBpm > 0.0 && qAbs(entry.first - selectedBpm) <= 0.05) {
            continue;
        }
        finalList.append(entry);
        if (finalList.size() >= 8) {
            break;
        }
    }
    *out = finalList;
}

}  // namespace

DecodedAudio decodeMonoTrack(const QString& trackPath, int sampleRate)
{
    DecodedAudio decoded;
    if (trackPath.isEmpty() || sampleRate <= 0) {
        return decoded;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, static_cast<ma_uint32>(sampleRate));
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(trackPath, &config, &decoder) != MA_SUCCESS) {
        return decoded;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && totalFrames > 0) {
        decoded.samples.reserve(static_cast<int>(qMin<ma_uint64>(totalFrames, static_cast<ma_uint64>(INT_MAX))));
    }

    QVector<float> buffer(4096, 0.0f);
    while (true) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buffer.data(), static_cast<ma_uint64>(buffer.size()), &framesRead) != MA_SUCCESS
            || framesRead == 0) {
            break;
        }
        const int oldSize = decoded.samples.size();
        decoded.samples.resize(oldSize + static_cast<int>(framesRead));
        std::copy_n(buffer.constBegin(), static_cast<int>(framesRead), decoded.samples.begin() + oldSize);
    }
    ma_decoder_uninit(&decoder);

    decoded.sampleRate = sampleRate;
    if (sampleRate > 0) {
        decoded.durationSeconds = static_cast<double>(decoded.samples.size()) / static_cast<double>(sampleRate);
    }
    return decoded;
}

Envelope buildOnsetEnvelope(const QVector<float>& samples, int sampleRate)
{
    Envelope envelope;
    if (samples.isEmpty() || sampleRate <= 0) {
        return envelope;
    }
    envelope.stepSeconds = static_cast<double>(kAnalysisHopSize) / static_cast<double>(sampleRate);

    double smoothedEnergy = 0.0;
    double maxValue = 0.0;
    for (int start = 0; start < samples.size(); start += kAnalysisHopSize) {
        const int end = qMin(start + kAnalysisWindowSize, samples.size());
        if (end <= start) {
            break;
        }
        double absSum = 0.0;
        double squareSum = 0.0;
        for (int i = start; i < end; ++i) {
            const double sample = samples.at(i);
            absSum += qAbs(sample);
            squareSum += sample * sample;
        }
        const double frameCount = static_cast<double>(end - start);
        const double rms = std::sqrt(squareSum / frameCount);
        const double meanAbs = absSum / frameCount;
        const double energy = 0.60 * rms + 0.40 * meanAbs;
        const double onset = qMax(0.0, energy - smoothedEnergy);
        envelope.values.append(static_cast<float>(onset));
        maxValue = qMax(maxValue, onset);
        smoothedEnergy = 0.88 * smoothedEnergy + 0.12 * energy;
    }

    if (maxValue <= 1e-6) {
        return envelope;
    }
    for (float& value : envelope.values) {
        value = static_cast<float>(value / maxValue);
    }
    return envelope;
}

Envelope buildTransientEnvelope(const QVector<float>& samples, int sampleRate)
{
    Envelope envelope;
    if (samples.isEmpty() || sampleRate <= 0) {
        return envelope;
    }
    envelope.stepSeconds = static_cast<double>(kOffsetHopSize) / static_cast<double>(sampleRate);

    double previousSample = 0.0;
    double maxValue = 0.0;
    for (int start = 0; start < samples.size(); start += kOffsetHopSize) {
        const int end = qMin(start + kOffsetWindowSize, samples.size());
        if (end <= start) {
            break;
        }
        double diffSum = 0.0;
        double prevAbs = qAbs(previousSample);
        for (int i = start; i < end; ++i) {
            const double currentAbs = qAbs(samples.at(i));
            diffSum += qAbs(currentAbs - prevAbs);
            prevAbs = currentAbs;
        }
        previousSample = samples.at(end - 1);
        const double frameCount = static_cast<double>(end - start);
        const double transient = diffSum / frameCount;
        envelope.values.append(static_cast<float>(transient));
        maxValue = qMax(maxValue, transient);
    }

    if (maxValue <= 1e-6) {
        return envelope;
    }
    for (float& value : envelope.values) {
        value = static_cast<float>(value / maxValue);
    }
    return envelope;
}

BpmDetectionResult detectBpm(const Envelope& onsetEnvelope, const QString& meterIdHint)
{
    BpmDetectionResult result;
    const QVector<float>& envelope = onsetEnvelope.values;
    const double stepSeconds = onsetEnvelope.stepSeconds;
    if (envelope.size() < 8 || stepSeconds <= 0.0) {
        return result;
    }

    double coarseBpm = 0.0;
    double coarseScore = 0.0;
    for (double bpm = kMinDetectBpm; bpm <= kMaxDetectBpm; bpm += 0.25) {
        const int lag = qRound((60.0 / bpm) / stepSeconds);
        if (lag <= 1 || lag >= envelope.size()) {
            continue;
        }
        double score = correlationAtLag(envelope, lag);
        score += 0.20 * correlationAtLag(envelope, lag * 2);
        score += 0.10 * correlationAtLag(envelope, qMax(1, lag / 2));
        if (score > coarseScore) {
            coarseScore = score;
            coarseBpm = bpm;
        }
    }
    if (coarseBpm <= 0.0) {
        return result;
    }

    const MeterPattern* straightPattern = meterPatternById(QStringLiteral("4/4"));
    if (straightPattern == nullptr) {
        return result;
    }

    QVector<TempoAlignmentResult> tempoCandidates;
    TempoAlignmentResult bestTempo;
    auto considerTempoCandidate = [&](const TempoAlignmentResult& candidate) {
        if (candidate.bpm <= 0.0 || candidate.pattern == nullptr) {
            return;
        }
        tempoCandidates.append(candidate);
        if (candidate.score > bestTempo.score) {
            bestTempo = candidate;
        }
    };

    considerTempoCandidate(bestTempoAlignmentNear(envelope, stepSeconds, coarseBpm, 6.0, *straightPattern));
    static const QVector<double> kTempoMultipliers{0.5, 1.0, 1.5, 2.0, 3.0};
    for (double multiplier : kTempoMultipliers) {
        if (qFuzzyCompare(multiplier, 1.0)) {
            continue;
        }
        const double candidateCenter = coarseBpm * multiplier;
        if (candidateCenter < kMinDetectBpm || candidateCenter > kMaxDetectBpm) {
            continue;
        }
        const double searchRange = multiplier > 2.1 ? 8.0 : (multiplier < 0.75 ? 4.0 : 6.0);
        considerTempoCandidate(bestTempoAlignmentNear(
            envelope,
            stepSeconds,
            candidateCenter,
            searchRange,
            *straightPattern
        ));
    }

    auto preferHigherAlias = [&](double minRatio, double maxRatio, double scoreThreshold, double minTargetBpm) {
        TempoAlignmentResult replacement = bestTempo;
        for (const TempoAlignmentResult& candidate : tempoCandidates) {
            if (candidate.bpm <= bestTempo.bpm || candidate.bpm < minTargetBpm) {
                continue;
            }
            const double ratio = candidate.bpm / qMax(1.0, bestTempo.bpm);
            if (ratio < minRatio || ratio > maxRatio) {
                continue;
            }
            if (candidate.score < bestTempo.score * scoreThreshold) {
                continue;
            }
            if (candidate.bpm > replacement.bpm || (qFuzzyCompare(candidate.bpm, replacement.bpm) && candidate.score > replacement.score)) {
                replacement = candidate;
            }
        }
        bestTempo = replacement;
    };

    auto preferTopBpmCandidate = [&](double minTargetBpm, double scoreThreshold, double minRatio) {
        TempoAlignmentResult replacement = bestTempo;
        for (const TempoAlignmentResult& candidate : tempoCandidates) {
            if (candidate.bpm < minTargetBpm) {
                continue;
            }
            if (candidate.score < bestTempo.score * scoreThreshold) {
                continue;
            }
            const double ratio = candidate.bpm / qMax(1.0, bestTempo.bpm);
            if (ratio < minRatio) {
                continue;
            }
            if (candidate.bpm > replacement.bpm || (qFuzzyCompare(candidate.bpm, replacement.bpm) && candidate.score > replacement.score)) {
                replacement = candidate;
            }
        }
        bestTempo = replacement;
    };

    const QString meterId = meterIdHint.isEmpty() ? QStringLiteral("auto") : meterIdHint;

    if (meterId == QLatin1String("3/4") || meterId == QLatin1String("6/8")) {
        if (const MeterPattern* meterPattern = meterPatternById(meterId); meterPattern != nullptr) {
            const TempoAlignmentResult meterBase = bestTempoAlignmentNear(
                envelope,
                stepSeconds,
                coarseBpm,
                6.0,
                *meterPattern
            );
            if (meterBase.bpm > 0.0) {
                tempoCandidates.append(meterBase);
                if (meterBase.score > bestTempo.score) {
                    bestTempo = meterBase;
                }
            }

            const double tripleCenter = coarseBpm * 3.0;
            const bool allowTripleAlias =
                meterId != QLatin1String("6/8")
                || coarseBpm < 80.0;
            if (allowTripleAlias && tripleCenter >= kMinDetectBpm && tripleCenter <= kMaxDetectBpm) {
                const TempoAlignmentResult meterTriple = bestTempoAlignmentNear(
                    envelope,
                    stepSeconds,
                    tripleCenter,
                    8.0,
                    *meterPattern
                );
                if (meterTriple.bpm > 0.0) {
                    tempoCandidates.append(meterTriple);
                }
                const double minTargetBpm = meterId == QLatin1String("3/4") ? 150.0 : 140.0;
                const double scoreThreshold = meterId == QLatin1String("3/4") ? 0.48 : 0.45;
                if (meterTriple.bpm >= minTargetBpm
                    && meterBase.bpm > 0.0
                    && meterTriple.score >= meterBase.score * scoreThreshold) {
                    bestTempo = meterTriple;
                }
            }
        }
    }

    if (bestTempo.bpm < 110.0) {
        preferHigherAlias(1.5, 3.1, 0.80, 0.0);
    } else if (bestTempo.bpm < 170.0) {
        preferHigherAlias(1.5, 2.1, 0.92, 180.0);
    }

    if (meterId == QLatin1String("3/4") && bestTempo.bpm < 90.0) {
        preferHigherAlias(2.5, 3.15, 0.72, 150.0);
        preferTopBpmCandidate(150.0, 0.52, 2.0);
    }
    if (meterId == QLatin1String("6/8") && bestTempo.bpm < 130.0) {
        if (bestTempo.bpm < 80.0) {
            preferHigherAlias(2.5, 3.15, 0.70, 140.0);
            preferTopBpmCandidate(140.0, 0.50, 1.4);
        } else {
            preferHigherAlias(1.45, 2.05, 0.60, 140.0);
            preferTopBpmCandidate(140.0, 0.45, 1.45);
            if (const MeterPattern* sixEight = meterPatternById(QStringLiteral("6/8")); sixEight != nullptr) {
                const TempoAlignmentResult doubled = bestTempoAlignmentNear(
                    envelope,
                    stepSeconds,
                    bestTempo.bpm * 2.0,
                    6.0,
                    *sixEight
                );
                if (doubled.bpm >= 140.0 && doubled.score >= bestTempo.score * 0.40) {
                    bestTempo = doubled;
                }
            }
        }
    }
    if (meterId == QLatin1String("7/4") && bestTempo.bpm < 100.0) {
        preferHigherAlias(1.9, 3.2, 0.55, 130.0);
        preferTopBpmCandidate(130.0, 0.55, 2.0);
    }
    if ((meterId == QLatin1String("4/4") || meterId == QLatin1String("auto")) && bestTempo.bpm < 170.0) {
        preferHigherAlias(1.8, 2.1, 0.88, 260.0);
    }

    const QVector<const MeterPattern*> patterns = candidatePatternsForId(meterId);
    TempoAlignmentResult bestMeterAligned;
    for (const MeterPattern* pattern : patterns) {
        if (pattern == nullptr) {
            continue;
        }
        TempoAlignmentResult candidate = bestTempoAlignmentNear(
            envelope,
            stepSeconds,
            bestTempo.bpm,
            1.0,
            *pattern
        );
        if (candidate.score > bestMeterAligned.score) {
            bestMeterAligned = candidate;
        }
    }

    if (bestMeterAligned.pattern != nullptr) {
        if (meterId == QLatin1String("7/4")) {
            const double integerTarget = qRound(bestMeterAligned.bpm);
            if (qAbs(bestMeterAligned.bpm - integerTarget) <= 0.12) {
                TempoAlignmentResult integerCandidate = bestTempoAlignmentNear(
                    envelope,
                    stepSeconds,
                    integerTarget,
                    0.12,
                    *bestMeterAligned.pattern
                );
                if (integerCandidate.pattern != nullptr
                    && qAbs(integerCandidate.bpm - integerTarget) <= 0.051
                    && integerCandidate.score >= bestMeterAligned.score * 0.94) {
                    bestMeterAligned = integerCandidate;
                }
            }
        }
        result.bpm = bestMeterAligned.bpm;
        result.meterId = QString::fromLatin1(bestMeterAligned.pattern->id);
        result.meterPhaseSeconds = bestMeterAligned.phase;
        result.meterPhaseValid = true;
        QVector<TempoAlignmentResult> rankingSource = tempoCandidates;
        rankingSource.append(bestTempo);
        rankingSource.append(bestMeterAligned);
        cacheBpmCandidates(bestMeterAligned.bpm, rankingSource, &result.candidates);
        return result;
    }

    result.bpm = bestTempo.bpm;
    result.meterId = QStringLiteral("4/4");
    result.meterPhaseSeconds = 0.0;
    result.meterPhaseValid = false;
    QVector<TempoAlignmentResult> rankingSource = tempoCandidates;
    rankingSource.append(bestTempo);
    cacheBpmCandidates(bestTempo.bpm, rankingSource, &result.candidates);
    return result;
}

double detectOffset(
    const Envelope& onsetEnvelope,
    const Envelope& transientEnvelope,
    const OffsetDetectionInputs& inputs)
{
    const QVector<float>& onset = onsetEnvelope.values;
    const QVector<float>& offsetEnv = transientEnvelope.values;
    const double onsetStepSeconds = onsetEnvelope.stepSeconds;
    const double offsetStepSeconds = transientEnvelope.stepSeconds;
    const double bpm = inputs.bpm;

    if ((onset.isEmpty() && offsetEnv.isEmpty()) || bpm <= 0.0) {
        return 0.0;
    }

    const double beatPeriod = 60.0 / bpm;
    if (beatPeriod <= 0.0) {
        return 0.0;
    }

    QString effectiveMeterId = inputs.meterId;
    if (effectiveMeterId == QLatin1String("auto")) {
        effectiveMeterId = inputs.lastDetectedMeterId.isEmpty()
            ? QStringLiteral("4/4")
            : inputs.lastDetectedMeterId;
    }
    const MeterPattern* meterPattern = meterPatternById(effectiveMeterId);
    const int barPulseCount = meterPattern != nullptr
        ? qMax(1, meterPattern->accentWeights.size())
        : 4;
    const double barPeriod = beatPeriod * static_cast<double>(barPulseCount);
    const bool hasMeterPhaseHint = inputs.hasLastDetectedMeterPhase
        && effectiveMeterId == inputs.lastDetectedMeterId;
    const double meterBeatPhase = hasMeterPhaseHint
        ? std::fmod(std::fmod(inputs.lastDetectedMeterPhase, beatPeriod) + beatPeriod, beatPeriod)
        : 0.0;
    const double offsetAnchor = inputs.offsetAnchorSeconds;
    const double nearZeroEpsilon = qMax(0.0005, onsetStepSeconds * 0.20);
    const double trackDurationSeconds = inputs.trackDurationSeconds;

    const QString snapModeId = inputs.snapMode.isEmpty() ? QStringLiteral("bar") : inputs.snapMode;
    double translationPeriod = barPeriod;
    if (snapModeId == QLatin1String("quarter")) {
        translationPeriod = beatPeriod;
    } else if (snapModeId == QLatin1String("eighth")) {
        translationPeriod = beatPeriod * 0.5;
    }
    if (translationPeriod <= 0.0) {
        translationPeriod = beatPeriod;
    }

    auto foldToNearest = [](double baseValue, double period, double anchor) {
        if (period <= 0.0) {
            return baseValue;
        }
        const double stepCount = static_cast<double>(qRound64((anchor - baseValue) / period));
        return baseValue + stepCount * period;
    };

    auto phaseDistance = [](double lhs, double rhs, double period) {
        if (period <= 0.0) {
            return qAbs(lhs - rhs);
        }
        double delta = std::fmod(lhs - rhs, period);
        if (delta < 0.0) {
            delta += period;
        }
        return qMin(delta, period - delta);
    };

    auto normalizePhase = [beatPeriod](double phase) {
        double normalized = std::fmod(phase, beatPeriod);
        if (normalized < 0.0) {
            normalized += beatPeriod;
        }
        return normalized;
    };

    auto normalizeToSymmetricRange = [](double value, double period) {
        if (period <= 0.0) {
            return value;
        }
        double normalized = std::fmod(value, period);
        if (normalized < 0.0) {
            normalized += period;
        }
        if (normalized > period * 0.5) {
            normalized -= period;
        }
        return normalized;
    };

    auto scorePhase = [&](double phaseSecond) {
        const double normalizedPhase =
            phaseSecond > beatPeriod * 0.5
            ? phaseSecond - beatPeriod
            : phaseSecond;
        double score = 0.0;
        int sampleCount = 0;
        for (double second = phaseSecond; second < trackDurationSeconds; second += beatPeriod) {
            if (!offsetEnv.isEmpty() && offsetStepSeconds > 0.0) {
                score += 0.75 * sampleEnvelopeLinear(offsetEnv, second / offsetStepSeconds);
            }
            if (!onset.isEmpty() && onsetStepSeconds > 0.0) {
                score += 0.25 * sampleEnvelopeLinear(onset, second / onsetStepSeconds);
            }
            ++sampleCount;
        }
        if (sampleCount <= 0) {
            return -1.0;
        }
        score /= static_cast<double>(sampleCount);
        score -= qAbs(normalizedPhase) * kOffsetPhasePenalty;
        if (hasMeterPhaseHint && snapModeId == QLatin1String("bar")) {
            score -= phaseDistance(phaseSecond, meterBeatPhase, beatPeriod) * 0.14;
        }
        return score;
    };

    double bestPhaseSecond = 0.0;
    double bestScore = -1.0;
    for (double phaseSecond = 0.0; phaseSecond < beatPeriod; phaseSecond += 0.001) {
        const double score = scorePhase(phaseSecond);
        if (score > bestScore) {
            bestScore = score;
            bestPhaseSecond = phaseSecond;
        }
    }

    const double fineStart = qMax(0.0, bestPhaseSecond - 0.004);
    const double fineEnd = qMin(beatPeriod, bestPhaseSecond + 0.004);
    for (double phaseSecond = fineStart; phaseSecond <= fineEnd + 1e-9; phaseSecond += 0.00025) {
        const double score = scorePhase(phaseSecond);
        if (score > bestScore) {
            bestScore = score;
            bestPhaseSecond = phaseSecond;
        }
    }

    QVector<double> snapCandidates{0.0, bestPhaseSecond};
    const int maxSnapSteps = qCeil((beatPeriod * 0.5) / (1.0 / 30.0)) + 2;
    for (int stepIndex = -maxSnapSteps; stepIndex <= maxSnapSteps; ++stepIndex) {
        double candidate = static_cast<double>(stepIndex) / 30.0;
        while (candidate < 0.0) {
            candidate += beatPeriod;
        }
        while (candidate >= beatPeriod) {
            candidate -= beatPeriod;
        }
        snapCandidates.append(candidate);
    }
    if (snapModeId == QLatin1String("eighth")) {
        snapCandidates.append(beatPeriod * 0.5);
    }
    if (hasMeterPhaseHint) {
        snapCandidates.append(meterBeatPhase);
        snapCandidates.append(normalizePhase(meterBeatPhase + beatPeriod * 0.5));
    }

    double preferredPhase = bestPhaseSecond;
    if (snapModeId == QLatin1String("bar")) {
        if (qAbs(offsetAnchor) > nearZeroEpsilon) {
            preferredPhase = normalizePhase(offsetAnchor);
        } else if (hasMeterPhaseHint) {
            preferredPhase = meterBeatPhase;
        }
    }

    double snappedPhaseSecond = bestPhaseSecond;
    for (double candidate : snapCandidates) {
        const double candidatePhase = normalizePhase(candidate);
        const double candidateScore = scorePhase(candidatePhase);
        if (candidateScore < bestScore * kOffsetSnapThreshold) {
            continue;
        }

        const double currentDistance = phaseDistance(snappedPhaseSecond, preferredPhase, beatPeriod);
        const double candidateDistance = phaseDistance(candidatePhase, preferredPhase, beatPeriod);
        if (candidateDistance + 1e-9 < currentDistance) {
            snappedPhaseSecond = candidatePhase;
        } else if (qAbs(candidateDistance - currentDistance) <= 1e-9
            && candidateScore > scorePhase(snappedPhaseSecond) + 1e-9) {
            snappedPhaseSecond = candidatePhase;
        }
    }

    const double canonicalPhase = normalizePhase(snappedPhaseSecond);
    const double baseAnchor = snapModeId == QLatin1String("bar") ? offsetAnchor : 0.0;
    double baseOffset = foldToNearest(canonicalPhase, beatPeriod, baseAnchor);
    if (hasMeterPhaseHint && snapModeId == QLatin1String("bar")) {
        const double meterAnchored = foldToNearest(canonicalPhase, beatPeriod, inputs.lastDetectedMeterPhase);
        if (qAbs(meterAnchored - offsetAnchor) <= qAbs(baseOffset - offsetAnchor) + 1e-9
            || qAbs(offsetAnchor) <= nearZeroEpsilon) {
            baseOffset = meterAnchored;
        }
    }

    double resolvedOffset = 0.0;
    if (snapModeId == QLatin1String("bar")) {
        resolvedOffset = foldToNearest(baseOffset, translationPeriod, offsetAnchor);
        if (hasMeterPhaseHint
            && qAbs(offsetAnchor) <= nearZeroEpsilon
            && qAbs(resolvedOffset) <= nearZeroEpsilon
            && translationPeriod > nearZeroEpsilon) {
            resolvedOffset -= translationPeriod;
        }
    } else {
        resolvedOffset = normalizeToSymmetricRange(baseOffset, translationPeriod);
    }

    if (qAbs(resolvedOffset) <= 1e-4) {
        resolvedOffset = 0.0;
    }
    return resolvedOffset;
}

}  // namespace miacode::latency_analysis
