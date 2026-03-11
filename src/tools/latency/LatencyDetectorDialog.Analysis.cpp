double LatencyDetectorDialog::parsedBpm(bool* ok) const
{
    bool localOk = false;
    const double value = bpmEdit_->text().trimmed().toDouble(&localOk);
    if (ok != nullptr) {
        *ok = localOk && value > 0.0;
    }
    return (localOk && value > 0.0) ? value : 0.0;
}

double LatencyDetectorDialog::parsedOffset(bool* ok) const
{
    bool localOk = false;
    const QString text = offsetEdit_->text().trimmed();
    const double value = text.isEmpty() ? 0.0 : text.toDouble(&localOk);
    if (ok != nullptr) {
        *ok = text.isEmpty() ? true : localOk;
    }
    return (text.isEmpty() || localOk) ? value : 0.0;
}

double LatencyDetectorDialog::detectBpm()
{
    lastDetectedBpm_ = 0.0;
    lastDetectedBpmCandidates_.clear();
    if (onsetEnvelope_.size() < 8 || onsetStepSeconds_ <= 0.0) {
        return 0.0;
    }

    double coarseBpm = 0.0;
    double coarseScore = 0.0;
    for (double bpm = kMinDetectBpm; bpm <= kMaxDetectBpm; bpm += 0.25) {
        const int lag = qRound((60.0 / bpm) / onsetStepSeconds_);
        if (lag <= 1 || lag >= onsetEnvelope_.size()) {
            continue;
        }
        double score = correlationAtLag(onsetEnvelope_, lag);
        score += 0.20 * correlationAtLag(onsetEnvelope_, lag * 2);
        score += 0.10 * correlationAtLag(onsetEnvelope_, qMax(1, lag / 2));
        if (score > coarseScore) {
            coarseScore = score;
            coarseBpm = bpm;
        }
    }
    if (coarseBpm <= 0.0) {
        return 0.0;
    }

    const MeterPattern* straightPattern = meterPatternById(QStringLiteral("4/4"));
    if (straightPattern == nullptr) {
        return 0.0;
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

    considerTempoCandidate(bestTempoAlignmentNear(onsetEnvelope_, onsetStepSeconds_, coarseBpm, 6.0, *straightPattern));
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
            onsetEnvelope_,
            onsetStepSeconds_,
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

    const QString meterId = meterCombo_ != nullptr ? meterCombo_->currentData().toString() : QStringLiteral("4/4");

    if (meterId == QLatin1String("3/4") || meterId == QLatin1String("6/8")) {
        if (const MeterPattern* meterPattern = meterPatternById(meterId); meterPattern != nullptr) {
            const TempoAlignmentResult meterBase = bestTempoAlignmentNear(
                onsetEnvelope_,
                onsetStepSeconds_,
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
                    onsetEnvelope_,
                    onsetStepSeconds_,
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
                    onsetEnvelope_,
                    onsetStepSeconds_,
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
        // 7/4 often aliases to a lower harmonic; prefer a stable higher harmonic family
        // candidate (x2 or x3) if its score remains reasonably close.
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
            onsetEnvelope_,
            onsetStepSeconds_,
            bestTempo.bpm,
            1.0,
            *pattern
        );
        if (candidate.score > bestMeterAligned.score) {
            bestMeterAligned = candidate;
        }
    }

    auto cacheBpmCandidates = [this](double selectedBpm, const QVector<TempoAlignmentResult>& sourceCandidates) {
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

        lastDetectedBpm_ = selectedBpm;
        lastDetectedBpmCandidates_ = finalList;
    };

    if (bestMeterAligned.pattern != nullptr) {
        if (meterId == QLatin1String("7/4")) {
            const double integerTarget = qRound(bestMeterAligned.bpm);
            if (qAbs(bestMeterAligned.bpm - integerTarget) <= 0.12) {
                TempoAlignmentResult integerCandidate = bestTempoAlignmentNear(
                    onsetEnvelope_,
                    onsetStepSeconds_,
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
        lastDetectedMeterId_ = QString::fromLatin1(bestMeterAligned.pattern->id);
        detectedMeterPhaseSecond_ = bestMeterAligned.phase;
        detectedMeterPhaseValid_ = true;
        QVector<TempoAlignmentResult> rankingSource = tempoCandidates;
        rankingSource.append(bestTempo);
        rankingSource.append(bestMeterAligned);
        cacheBpmCandidates(bestMeterAligned.bpm, rankingSource);
        return bestMeterAligned.bpm;
    }

    lastDetectedMeterId_ = QStringLiteral("4/4");
    detectedMeterPhaseSecond_ = 0.0;
    detectedMeterPhaseValid_ = false;
    QVector<TempoAlignmentResult> rankingSource = tempoCandidates;
    rankingSource.append(bestTempo);
    cacheBpmCandidates(bestTempo.bpm, rankingSource);
    return bestTempo.bpm;
}

double LatencyDetectorDialog::detectOffset(double bpm) const
{
    if ((onsetEnvelope_.isEmpty() && offsetEnvelope_.isEmpty()) || bpm <= 0.0) {
        return 0.0;
    }

    const double beatPeriod = 60.0 / bpm;
    auto scorePhase = [&](double phaseSecond) {
        if (beatPeriod <= 0.0) {
            return -1.0;
        }
        const double normalizedPhase =
            phaseSecond > beatPeriod * 0.5
            ? phaseSecond - beatPeriod
            : phaseSecond;
        double score = 0.0;
        int sampleCount = 0;
        for (double second = phaseSecond; second < trackDurationSeconds_; second += beatPeriod) {
            if (!offsetEnvelope_.isEmpty() && offsetStepSeconds_ > 0.0) {
                score += 0.75 * sampleEnvelopeLinear(offsetEnvelope_, second / offsetStepSeconds_);
            }
            if (!onsetEnvelope_.isEmpty() && onsetStepSeconds_ > 0.0) {
                score += 0.25 * sampleEnvelopeLinear(onsetEnvelope_, second / onsetStepSeconds_);
            }
            ++sampleCount;
        }
        if (sampleCount <= 0) {
            return -1.0;
        }
        score /= static_cast<double>(sampleCount);
        score -= qAbs(normalizedPhase) * kOffsetPhasePenalty;
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

    double snappedPhaseSecond = bestPhaseSecond;
    for (double candidate : snapCandidates) {
        const double candidateScore = scorePhase(candidate);
        if (candidateScore < bestScore * kOffsetSnapThreshold) {
            continue;
        }
        double normalizedCandidate = std::fmod(candidate, beatPeriod);
        if (normalizedCandidate < 0.0) {
            normalizedCandidate += beatPeriod;
        }
        if (normalizedCandidate > beatPeriod * 0.5) {
            normalizedCandidate -= beatPeriod;
        }

        double normalizedCurrent = std::fmod(snappedPhaseSecond, beatPeriod);
        if (normalizedCurrent < 0.0) {
            normalizedCurrent += beatPeriod;
        }
        if (normalizedCurrent > beatPeriod * 0.5) {
            normalizedCurrent -= beatPeriod;
        }

        const double bestNormalized = bestPhaseSecond > beatPeriod * 0.5 ? bestPhaseSecond - beatPeriod : bestPhaseSecond;
        if (qAbs(normalizedCandidate) < qAbs(normalizedCurrent) - 1e-9
            || (qAbs(qAbs(normalizedCandidate) - qAbs(normalizedCurrent)) <= 1e-9
                && qAbs(normalizedCandidate - bestNormalized) < qAbs(normalizedCurrent - bestNormalized))) {
            snappedPhaseSecond = candidate;
        }
    }

    double normalizedOffset = std::fmod(snappedPhaseSecond, beatPeriod);
    if (normalizedOffset < 0.0) {
        normalizedOffset += beatPeriod;
    }
    if (normalizedOffset > beatPeriod * 0.5) {
        normalizedOffset -= beatPeriod;
    }
    if (qAbs(normalizedOffset) <= onsetStepSeconds_ * 0.5) {
        normalizedOffset = 0.0;
    }
    return normalizedOffset;
}

QString LatencyDetectorDialog::formatTimestamp(double second) const
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(second * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 seconds = (totalMs / 1000) % 60;
    const qint64 millis = totalMs % 1000;
    return QString("%1:%2:%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString LatencyDetectorDialog::localizedText(const QString& zh, const QString& en) const
{
    return UiText::isChineseUi() ? zh : en;
}
