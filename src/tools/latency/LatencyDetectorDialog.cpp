#include "LatencyDetectorDialog.h"

#include "DialogLocalization.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <limits>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QDoubleValidator>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QFontMetrics>
#include <QMenu>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <QtMath>

#include "QtPreviewSfxRuntime.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/MiniaudioFileAccess.h"
#include "common/WaveformCache.h"

namespace {

constexpr int kAnalysisSampleRate = 24000;
constexpr int kAnalysisWindowSize = 1024;
constexpr int kAnalysisHopSize = 512;
constexpr int kOffsetWindowSize = 512;
constexpr int kOffsetHopSize = 128;
constexpr double kOffsetReplayDelayMs = 120.0;
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

bool appendDistinctSecond(QVector<double>* seconds, double second)
{
    if (seconds == nullptr) {
        return false;
    }
    if (!seconds->isEmpty() && qAbs(seconds->constLast() - second) <= 1e-6) {
        return false;
    }
    seconds->append(second);
    return true;
}

TimelineRenderSnapshot buildLatencyTimelineSnapshot(
    double durationSeconds,
    double bpm,
    double offsetSecond,
    int barPulseCount)
{
    TimelineRenderSnapshot snapshot;
    snapshot.durationSeconds = qMax(0.0, durationSeconds);
    snapshot.minimumSecond = 0.0;
    snapshot.maximumSecond = snapshot.durationSeconds;

    TimelineRenderLine line;
    line.lineId = 1;
    line.lineNumber = 1;
    line.startPosition = 0;
    line.startSecond = 0.0;
    line.endSecond = snapshot.durationSeconds;

    if (bpm > 0.0) {
        const double beatPeriod = 60.0 / bpm;
        const int safeBarPulseCount = qMax(1, barPulseCount);
        if (beatPeriod > 0.0) {
            const double renderMinSecond = -0.5;
            qint64 beatIndex = static_cast<qint64>(qFloor((renderMinSecond - offsetSecond) / beatPeriod));
            while (offsetSecond + static_cast<double>(beatIndex) * beatPeriod < renderMinSecond - 1e-6) {
                ++beatIndex;
            }
            int sourceCol = 1;
            for (;; ++beatIndex) {
                const double beatSecond = offsetSecond + static_cast<double>(beatIndex) * beatPeriod;
                if (beatSecond > snapshot.durationSeconds + 1e-6) {
                    break;
                }

                const bool isBarLine = beatIndex >= 0
                    ? ((beatIndex % safeBarPulseCount) == 0)
                    : (((-beatIndex) % safeBarPulseCount) == 0);
                if (isBarLine) {
                    appendDistinctSecond(&snapshot.measureLineSeconds, beatSecond);
                } else {
                    TimelineRenderBeat beat;
                    beat.secondOffset = beatSecond;
                    beat.sourceCol = sourceCol;
                    beat.subdivisionBeats = safeBarPulseCount;
                    beat.subdivisionIndex = safeBarPulseCount > 0
                        ? static_cast<int>((beatIndex % safeBarPulseCount + safeBarPulseCount) % safeBarPulseCount)
                        : 0;
                    line.beats.append(beat);
                }
                ++sourceCol;
            }
        }
    }

    snapshot.lines.append(line);
    snapshot.noteVisualEndPrefixMaxWithSlideTracks.append(-std::numeric_limits<double>::infinity());
    snapshot.noteVisualEndPrefixMaxWithoutSlideTracks.append(-std::numeric_limits<double>::infinity());
    return snapshot;
}

enum class LatencyShortcutAction {
    None,
    TogglePlayPause,
    StopOrPlay,
    Slower,
    Faster,
};

LatencyShortcutAction matchLatencyShortcut(const QKeyEvent* event)
{
    if (event == nullptr) {
        return LatencyShortcutAction::None;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_Space) {
        return LatencyShortcutAction::TogglePlayPause;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        return LatencyShortcutAction::StopOrPlay;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_X) {
        return LatencyShortcutAction::TogglePlayPause;
    }
    if (modifiers == Qt::ControlModifier && event->key() == Qt::Key_O) {
        return LatencyShortcutAction::Slower;
    }
    if (modifiers == Qt::ControlModifier && event->key() == Qt::Key_P) {
        return LatencyShortcutAction::Faster;
    }
    return LatencyShortcutAction::None;
}

const QList<QPair<double, QString>>& latencyPlaybackSpeedOptions()
{
    static const QList<QPair<double, QString>> kOptions{
        {0.25, QStringLiteral("0.25x")},
        {0.50, QStringLiteral("0.5x")},
        {0.75, QStringLiteral("0.75x")},
        {1.00, QStringLiteral("1x")},
        {1.25, QStringLiteral("1.25x")},
        {1.50, QStringLiteral("1.5x")},
        {2.00, QStringLiteral("2x")},
    };
    return kOptions;
}

const QVector<double>& latencyTimelineZoomPresets()
{
    static const QVector<double> kZoomPresets{0.25, 0.5, 0.75, 1.0, 1.5, 2.0};
    return kZoomPresets;
}

int closestLatencyPlaybackSpeedIndex(double rate)
{
    const auto& options = latencyPlaybackSpeedOptions();
    if (options.isEmpty()) {
        return 0;
    }
    int bestIndex = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int index = 0; index < options.size(); ++index) {
        const double distance = qAbs(options.at(index).first - rate);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }
    return bestIndex;
}

struct DecodedAudio {
    QVector<float> samples;
    int sampleRate = 0;
    double durationSeconds = 0.0;
};

DecodedAudio decodeMonoTrack(const QString& trackPath, int sampleRate)
{
    DecodedAudio decoded;
    if (trackPath.isEmpty()) {
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

QVector<float> buildOnsetEnvelope(const QVector<float>& samples, int sampleRate, double* stepSecondsOut)
{
    QVector<float> envelope;
    if (stepSecondsOut != nullptr) {
        *stepSecondsOut = 0.0;
    }
    if (samples.isEmpty() || sampleRate <= 0) {
        return envelope;
    }

    if (stepSecondsOut != nullptr) {
        *stepSecondsOut = static_cast<double>(kAnalysisHopSize) / static_cast<double>(sampleRate);
    }

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
        envelope.append(static_cast<float>(onset));
        maxValue = qMax(maxValue, onset);
        smoothedEnergy = 0.88 * smoothedEnergy + 0.12 * energy;
    }

    if (maxValue <= 1e-6) {
        return envelope;
    }
    for (float& value : envelope) {
        value = static_cast<float>(value / maxValue);
    }
    return envelope;
}

QVector<float> buildTransientEnvelope(const QVector<float>& samples, int sampleRate, double* stepSecondsOut)
{
    QVector<float> envelope;
    if (stepSecondsOut != nullptr) {
        *stepSecondsOut = 0.0;
    }
    if (samples.isEmpty() || sampleRate <= 0) {
        return envelope;
    }

    if (stepSecondsOut != nullptr) {
        *stepSecondsOut = static_cast<double>(kOffsetHopSize) / static_cast<double>(sampleRate);
    }

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
        envelope.append(static_cast<float>(transient));
        maxValue = qMax(maxValue, transient);
    }

    if (maxValue <= 1e-6) {
        return envelope;
    }
    for (float& value : envelope) {
        value = static_cast<float>(value / maxValue);
    }
    return envelope;
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

}  // namespace

LatencyDetectorDialog::LatencyDetectorDialog(
    const QString& trackPath,
    const QString& chartPath,
    miacode::waveform::WaveformCacheService* waveformCacheService,
    const PreviewAudioSettings& audioSettings,
    QWidget* parent
)
    : QDialog(parent)
    , trackPath_(trackPath)
    , chartPath_(chartPath)
    , audioSettings_(audioSettings)
    , waveformCacheService_(waveformCacheService)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    UiDialogs::configureDialogPreviewShortcuts(this, UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls);
    setWindowTitle(localizedText("BPM&偏移检测", "BPM & Offset Detection"));

    buildUi();
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        app->installEventFilter(this);
    }
    loadAudioAnalysis();
    updateBeatOverlay();
    updateVisibleRange(true);
    updatePlaybackUi();
    layout()->activate();
    const QSize initialSize = sizeHint();
    setMinimumSize(initialSize);
    resize(initialSize);
}

LatencyDetectorDialog::~LatencyDetectorDialog()
{
    pausePlayback();
    if (sfxRuntime_ != nullptr) {
        sfxRuntime_->stopAll();
    }
}

QString LatencyDetectorDialog::trackPath() const
{
    return trackPath_;
}

void LatencyDetectorDialog::setBpm(double bpm)
{
    updateBpmEdit(bpm, false);
}

void LatencyDetectorDialog::setMeterId(const QString& meterId)
{
    if (meterCombo_ == nullptr) {
        return;
    }
    const QString normalized = meterId.trimmed();
    int index = meterCombo_->findData(normalized);
    if (index < 0) {
        index = meterCombo_->findData(QStringLiteral("auto"));
    }
    if (index < 0) {
        index = 0;
    }
    if (meterCombo_->currentIndex() == index) {
        return;
    }
    QSignalBlocker blocker(meterCombo_);
    meterCombo_->setCurrentIndex(index);
    updateBeatOverlay();
}

void LatencyDetectorDialog::setOffsetSeconds(double seconds)
{
    updateOffsetEdit(seconds, false);
}

void LatencyDetectorDialog::commitPendingValues()
{
    // Drain the session into the parent in a single batch on Save.
    // Open the gate temporarily so the existing emit sites fire once;
    // updateBpmEdit / applyOffsetValue normally skip the emit because
    // sessionEmitSuppressed_ is true throughout the session.
    sessionEmitSuppressed_ = false;
    bool bpmOk = false;
    const double bpm = parsedBpm(&bpmOk);
    if (bpmOk && bpm > 0.0) {
        emit bpmChanged(bpm);
    }
    bool offsetOk = false;
    const double offset = parsedOffset(&offsetOk);
    if (offsetOk) {
        emit offsetChanged(offset);
    }
    if (meterCombo_ != nullptr) {
        emit meterIdChanged(meterCombo_->currentData().toString());
    }
    sessionEmitSuppressed_ = true;
}

bool LatencyDetectorDialog::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    if (event == nullptr || !UiDialogs::dialogOwnsPreviewShortcutScope(this)) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (matchLatencyShortcut(keyEvent) != LatencyShortcutAction::None) {
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        const LatencyShortcutAction action = matchLatencyShortcut(keyEvent);
        if (action == LatencyShortcutAction::None) {
            return QDialog::eventFilter(watched, event);
        }
        if (keyEvent->isAutoRepeat()) {
            event->accept();
            return true;
        }
        switch (action) {
        case LatencyShortcutAction::TogglePlayPause:
            togglePlayback();
            break;
        case LatencyShortcutAction::StopOrPlay:
            handleStopOrPlayShortcut();
            break;
        case LatencyShortcutAction::Slower:
            stepPlaybackRate(-1);
            break;
        case LatencyShortcutAction::Faster:
            stepPlaybackRate(1);
            break;
        case LatencyShortcutAction::None:
            break;
        }
        event->accept();
        return true;
    }

    if (event->type() == QEvent::KeyRelease) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (matchLatencyShortcut(keyEvent) != LatencyShortcutAction::None) {
            event->accept();
            return true;
        }
    }

    return QDialog::eventFilter(watched, event);
}


#include "LatencyDetectorDialog.Ui.cpp"
#include "LatencyDetectorDialog.Playback.cpp"
#include "LatencyDetectorDialog.Analysis.cpp"
