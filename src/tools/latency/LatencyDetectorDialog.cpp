#include "LatencyDetectorDialog.h"

#include "DialogLocalization.h"

#include <algorithm>
#include <climits>
#include <functional>

#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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
#include "UiText.h"
#include "UiTheme.h"

#include "../../third_party/miniaudio/miniaudio.h"

namespace {

constexpr int kWaveformPeakCount = 2048;
constexpr int kAnalysisSampleRate = 24000;
constexpr int kAnalysisWindowSize = 1024;
constexpr int kAnalysisHopSize = 512;
constexpr int kOffsetWindowSize = 512;
constexpr int kOffsetHopSize = 128;
constexpr double kMinimumVisibleSeconds = 4.0;
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

class WaveformOverviewWidget : public QWidget {
public:
    explicit WaveformOverviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(132);
        setMouseTracking(true);
    }

    void setWaveformData(const QVector<float>& peaks, double durationSeconds)
    {
        peaks_ = peaks;
        durationSeconds_ = qMax(0.0, durationSeconds);
        update();
    }

    void setVisibleRange(double startSecond, double durationSeconds)
    {
        visibleStartSecond_ = qMax(0.0, startSecond);
        visibleDurationSeconds_ = qMax(0.001, durationSeconds);
        update();
    }

    void setPlayheadSecond(double second)
    {
        playheadSecond_ = qBound(0.0, second, durationSeconds_);
        update();
    }

    void setBeatGrid(double bpm, double offsetSecond, int barPulseCount)
    {
        bpm_ = bpm;
        offsetSecond_ = offsetSecond;
        barPulseCount_ = qMax(1, barPulseCount);
        update();
    }

    void clearBeatGrid()
    {
        bpm_ = 0.0;
        offsetSecond_ = 0.0;
        update();
    }

    void setSeekCallback(std::function<void(double)> callback)
    {
        seekCallback_ = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        const UiTheme::Colors& c = UiTheme::colors();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), c.windowAltBg);

        const QRectF chartRect = rect().adjusted(8.0, 12.0, -8.0, -18.0);
        painter.setPen(QPen(c.border, 1.0));
        painter.setBrush(c.cardBg);
        painter.drawRoundedRect(chartRect, 8.0, 8.0);

        if (chartRect.width() <= 2.0 || chartRect.height() <= 2.0 || durationSeconds_ <= 0.0) {
            return;
        }

        if (bpm_ > 0.0) {
            const double beatPeriod = 60.0 / bpm_;
            const double firstBeat = offsetSecond_;
            const double rangeEnd = visibleStartSecond_ + visibleDurationSeconds_;
            if (beatPeriod > 0.0) {
                int beatIndex = static_cast<int>(qFloor((visibleStartSecond_ - firstBeat) / beatPeriod));
                if (firstBeat + beatIndex * beatPeriod < visibleStartSecond_) {
                    ++beatIndex;
                }
                for (; ; ++beatIndex) {
                    const double beatSecond = firstBeat + beatIndex * beatPeriod;
                    if (beatSecond > rangeEnd + 1e-6) {
                        break;
                    }
                    if (beatSecond < visibleStartSecond_ - 1e-6) {
                        continue;
                    }
                    const qreal x = secondToX(beatSecond, chartRect);
                    const bool isBarLine = beatIndex >= 0
                        ? (beatIndex % qMax(1, barPulseCount_)) == 0
                        : ((-beatIndex) % qMax(1, barPulseCount_)) == 0;
                    painter.setPen(QPen(isBarLine ? c.timelineCursor : c.timelineGridMinor, isBarLine ? 1.5 : 1.0));
                    painter.drawLine(QPointF(x, chartRect.top() + 2.0), QPointF(x, chartRect.bottom() - 2.0));
                }
            }
        }

        if (!peaks_.isEmpty()) {
            QPainterPath upperPath;
            QPainterPath lowerPath;
            bool started = false;
            for (int i = 0; i < peaks_.size(); ++i) {
                const double second = durationSeconds_ * (static_cast<double>(i) / qMax(1, peaks_.size() - 1));
                if (second < visibleStartSecond_ - 0.01 || second > visibleStartSecond_ + visibleDurationSeconds_ + 0.01) {
                    continue;
                }
                const qreal x = secondToX(second, chartRect);
                const qreal peak = qBound<qreal>(0.0, peaks_.at(i), 1.0);
                const qreal upperY = chartRect.center().y() - peak * chartRect.height() * 0.38;
                const qreal lowerY = chartRect.center().y() + peak * chartRect.height() * 0.38;
                if (!started) {
                    upperPath.moveTo(x, upperY);
                    lowerPath.moveTo(x, lowerY);
                    started = true;
                } else {
                    upperPath.lineTo(x, upperY);
                    lowerPath.lineTo(x, lowerY);
                }
            }
            painter.setPen(QPen(c.timelineWaveStroke, 1.15));
            painter.drawPath(upperPath);
            painter.drawPath(lowerPath);
        }

        painter.setPen(QPen(c.textPrimary, 1.0));
        painter.drawLine(
            QPointF(chartRect.left(), chartRect.center().y()),
            QPointF(chartRect.right(), chartRect.center().y())
        );

        const qreal playheadX = secondToX(playheadSecond_, chartRect);
        painter.setPen(QPen(c.timelinePlayhead, 2.0));
        painter.drawLine(QPointF(playheadX, chartRect.top()), QPointF(playheadX, chartRect.bottom()));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        dragging_ = event->button() == Qt::LeftButton;
        if (dragging_) {
            seekTo(event->position());
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_) {
            seekTo(event->position());
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        Q_UNUSED(event);
        dragging_ = false;
    }

private:
    qreal secondToX(double second, const QRectF& chartRect) const
    {
        const double range = qMax(0.001, visibleDurationSeconds_);
        const double relative = (second - visibleStartSecond_) / range;
        return chartRect.left() + qBound(0.0, relative, 1.0) * chartRect.width();
    }

    double xToSecond(qreal x, const QRectF& chartRect) const
    {
        const qreal width = qMax<qreal>(1.0, chartRect.width());
        const qreal relative = qBound<qreal>(0.0, (x - chartRect.left()) / width, 1.0);
        return qBound(0.0, visibleStartSecond_ + visibleDurationSeconds_ * relative, durationSeconds_);
    }

    void seekTo(const QPointF& position)
    {
        if (!seekCallback_) {
            return;
        }
        const QRectF chartRect = rect().adjusted(8.0, 12.0, -8.0, -18.0);
        seekCallback_(xToSecond(position.x(), chartRect));
    }

    QVector<float> peaks_;
    std::function<void(double)> seekCallback_;
    double durationSeconds_ = 0.0;
    double visibleStartSecond_ = 0.0;
    double visibleDurationSeconds_ = 12.0;
    double playheadSecond_ = 0.0;
    double bpm_ = 0.0;
    double offsetSecond_ = 0.0;
    int barPulseCount_ = 4;
    bool dragging_ = false;
};

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

    const QByteArray pathBytes = QFile::encodeName(trackPath);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, static_cast<ma_uint32>(sampleRate));
    ma_decoder decoder;
    if (ma_decoder_init_file(pathBytes.constData(), &config, &decoder) != MA_SUCCESS) {
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

QVector<float> buildWaveformPeaks(const QVector<float>& samples, int peakCount)
{
    QVector<float> peaks;
    if (samples.isEmpty() || peakCount <= 0) {
        return peaks;
    }

    peaks.fill(0.0f, peakCount);
    for (int i = 0; i < samples.size(); ++i) {
        const int peakIndex = qBound(
            0,
            static_cast<int>((static_cast<qint64>(i) * peakCount) / qMax(1, samples.size())),
            peakCount - 1
        );
        peaks[peakIndex] = qMax(peaks.at(peakIndex), qAbs(samples.at(i)));
    }
    return peaks;
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
    const PreviewAudioSettings& audioSettings,
    QWidget* parent
)
    : QDialog(parent)
    , trackPath_(trackPath)
    , chartPath_(chartPath)
    , audioSettings_(audioSettings)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowTitle(localizedText("BPM&偏移检测", "BPM & Offset Detection"));

    buildUi();
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
    updateBeatOverlay();
}


#include "LatencyDetectorDialog.Ui.cpp"
#include "LatencyDetectorDialog.Playback.cpp"
#include "LatencyDetectorDialog.Analysis.cpp"
