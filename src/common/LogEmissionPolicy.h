#pragma once

#include <QGlobal.h>
#include <QString>

#include <optional>

namespace miacode::diagnostics {

// Edge-triggered state for a diagnostic owner. The owner decides when to reset
// its lifecycle; this type only compares immutable values and performs no I/O.
template <typename Key>
class EdgeLogGate
{
public:
    bool shouldEmit(const Key& key)
    {
        if (!last_.has_value() || *last_ != key) {
            last_ = key;
            return true;
        }
        return false;
    }

    void reset() noexcept { last_.reset(); }
    bool hasValue() const noexcept { return last_.has_value(); }

private:
    std::optional<Key> last_;
};

struct StageRateKey {
    double rate = 1.0;
    int mediaKind = 0;

    friend bool operator==(const StageRateKey& left, const StageRateKey& right) noexcept
    {
        return left.rate == right.rate && left.mediaKind == right.mediaKind;
    }
};

enum class PlaybackRateLogKind {
    Ordinary,
    Deferred,
    Flushed,
    Error,
};

// Only successful, ordinary rate changes are candidates for edge suppression.
// Deferred, flushed, and error diagnostics describe distinct backend states and
// must remain visible even when their rate and media kind match the last value.
class PlaybackRateLogGate
{
public:
    bool shouldEmit(PlaybackRateLogKind kind, const StageRateKey& key)
    {
        return kind != PlaybackRateLogKind::Ordinary || ordinaryRateGate_.shouldEmit(key);
    }

    void reset() noexcept { ordinaryRateGate_.reset(); }

private:
    EdgeLogGate<StageRateKey> ordinaryRateGate_;
};

struct RebuildSummary {
    qint64 windowStartMs = -1;
    int rebuildCount = 0;
    qint64 totalElapsedMs = 0;
    qint64 maxElapsedMs = 0;
    QString lastLines;
};

struct RebuildDecision {
    std::optional<RebuildSummary> summary;
    bool emitIndividual = false;
    qint64 individualElapsedMs = 0;
    QString individualLastLines;
};

// Aggregates ordinary sub-2ms rebuilds for one second. Slow/error rebuilds
// bypass the aggregate after flushing it, so expensive or abnormal work stays
// visible as an individual row.
class RebuildWindow
{
public:
    RebuildDecision observe(
        qint64 nowMs,
        qint64 elapsedMs,
        const QString& lastLines,
        bool error = false)
    {
        RebuildDecision decision;
        if (hasPending() && nowMs - windowStartMs_ >= kFlushIntervalMs) {
            decision.summary = flush();
        }

        if (error || elapsedMs >= kSlowRebuildMs) {
            if (!decision.summary.has_value() && hasPending()) {
                decision.summary = flush();
            }
            decision.emitIndividual = true;
            decision.individualElapsedMs = elapsedMs;
            decision.individualLastLines = lastLines;
            return decision;
        }

        if (!hasPending()) {
            windowStartMs_ = nowMs;
        }
        ++rebuildCount_;
        totalElapsedMs_ += elapsedMs;
        maxElapsedMs_ = qMax(maxElapsedMs_, elapsedMs);
        lastLines_ = lastLines;
        return decision;
    }

    std::optional<RebuildSummary> flushForDestruction() { return flushIfPending(); }

private:
    static constexpr qint64 kFlushIntervalMs = 1000;
    static constexpr qint64 kSlowRebuildMs = 2;

    bool hasPending() const noexcept { return rebuildCount_ != 0; }

    std::optional<RebuildSummary> flushIfPending()
    {
        if (!hasPending()) {
            return std::nullopt;
        }
        return flush();
    }

    RebuildSummary flush()
    {
        RebuildSummary summary;
        summary.windowStartMs = windowStartMs_;
        summary.rebuildCount = rebuildCount_;
        summary.totalElapsedMs = totalElapsedMs_;
        summary.maxElapsedMs = maxElapsedMs_;
        summary.lastLines = lastLines_;
        windowStartMs_ = -1;
        rebuildCount_ = 0;
        totalElapsedMs_ = 0;
        maxElapsedMs_ = 0;
        lastLines_.clear();
        return summary;
    }

    qint64 windowStartMs_ = -1;
    int rebuildCount_ = 0;
    qint64 totalElapsedMs_ = 0;
    qint64 maxElapsedMs_ = 0;
    QString lastLines_;
};

}  // namespace miacode::diagnostics
