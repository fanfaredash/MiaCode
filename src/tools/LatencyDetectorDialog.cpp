#include "LatencyDetectorDialog.h"

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
constexpr double kOffsetPhasePenalty = 0.18;
constexpr double kOffsetSnapThreshold = 0.73;

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

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), QColor("#F7F9FC"));

        const QRectF chartRect = rect().adjusted(8.0, 12.0, -8.0, -18.0);
        painter.setPen(QPen(QColor("#D7DEE8"), 1.0));
        painter.setBrush(QColor("#FFFFFF"));
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
                    painter.setPen(QPen(isBarLine ? QColor("#7CA7D8") : QColor("#D5E2F2"), isBarLine ? 1.5 : 1.0));
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
            painter.setPen(QPen(QColor("#59718F"), 1.15));
            painter.drawPath(upperPath);
            painter.drawPath(lowerPath);
        }

        painter.setPen(QPen(QColor("#203449"), 1.0));
        painter.drawLine(
            QPointF(chartRect.left(), chartRect.center().y()),
            QPointF(chartRect.right(), chartRect.center().y())
        );

        const qreal playheadX = secondToX(playheadSecond_, chartRect);
        painter.setPen(QPen(QColor("#E0564A"), 2.0));
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
    setWindowTitle(localizedText("BPM&&偏移检测", "BPM && Offset Detection"));

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

void LatencyDetectorDialog::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    auto* bpmRow = new QHBoxLayout();
    bpmRow->setSpacing(8);
    auto* meterLabel = new QLabel(localizedText("拍号：", "Meter:"), this);
    meterCombo_ = new QComboBox(this);
    meterCombo_->setFixedWidth(88);
    for (const MeterPattern& pattern : meterPatterns()) {
        const QString meterId = QString::fromLatin1(pattern.id);
        QString label = QString::fromLatin1(pattern.label);
        if (meterId == QLatin1String("auto")) {
            label = localizedText("自动检测", "Auto Detect");
        }
        meterCombo_->addItem(label, meterId);
    }
    meterCombo_->setCurrentIndex(0);
    auto* bpmLabel = new QLabel(localizedText("BPM：", "BPM:"), this);
    bpmEdit_ = new QLineEdit(this);
    bpmEdit_->setValidator(new QDoubleValidator(1.0, 400.0, 3, bpmEdit_));
    bpmEdit_->setPlaceholderText("180.000");
    bpmEdit_->setFixedWidth(74);
    detectBpmButton_ = new QPushButton(localizedText("检测BPM", "Detect BPM"), this);
    bpmHelpButton_ = new QPushButton(localizedText("BPM测得不准？", "BPM inaccurate?"), this);
    bpmHelpButton_->setMinimumWidth(120);
    bpmRow->addWidget(meterLabel);
    bpmRow->addWidget(meterCombo_);
    bpmRow->addSpacing(6);
    bpmRow->addWidget(bpmLabel);
    bpmRow->addWidget(bpmEdit_);
    bpmRow->addWidget(detectBpmButton_);
    bpmRow->addWidget(bpmHelpButton_);
    bpmRow->addStretch(1);
    rootLayout->addLayout(bpmRow);

    auto* offsetRow = new QHBoxLayout();
    offsetRow->setSpacing(6);
    auto* offsetLabel = new QLabel(localizedText("偏移：", "Offset:"), this);
    offsetEdit_ = new QLineEdit(this);
    offsetEdit_->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, offsetEdit_));
    offsetEdit_->setAlignment(Qt::AlignCenter);
    offsetEdit_->setFixedWidth(74);
    detectOffsetButton_ = new QPushButton(localizedText("检测偏移", "Detect Offset"), this);

    const auto addAdjustButton = [this, offsetRow](const QString& text, double delta) {
        auto* button = new QPushButton(text, this);
        button->setFixedWidth(24);
        connect(button, &QPushButton::clicked, this, [this, delta]() {
            const double nextOffset = parsedOffset() + delta;
            updateOffsetEdit(nextOffset, true);
            restartPlaybackAfterOffsetChange();
        });
        offsetRow->addWidget(button);
    };

    offsetRow->addWidget(offsetLabel);
    addAdjustButton("<<", -0.010);
    addAdjustButton("<", -0.001);
    offsetRow->addWidget(offsetEdit_);
    addAdjustButton(">", 0.001);
    addAdjustButton(">>", 0.010);
    offsetRow->addWidget(detectOffsetButton_);
    offsetRow->addStretch(1);
    rootLayout->addLayout(offsetRow);

    waveformView_ = new WaveformOverviewWidget(this);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setSeekCallback([this](double second) {
        seekToSecond(second, true);
    });
    rootLayout->addWidget(waveformView_, 1);
    playbackSlider_ = new QSlider(Qt::Horizontal, this);
    playbackSlider_->setRange(0, 0);
    playbackSlider_->setSingleStep(10);
    playbackSlider_->setPageStep(300);
    rootLayout->addWidget(playbackSlider_);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(8);
    playPauseButton_ = new QPushButton(this);
    stopButton_ = new QPushButton(localizedText("停止", "Stop"), this);
    speedButton_ = new QToolButton(this);
    speedButton_->setPopupMode(QToolButton::InstantPopup);
    speedButton_->setText("1x");
    auto* speedMenu = new QMenu(speedButton_);
    const QList<QPair<double, QString>> speedOptions{
        {0.25, "0.25x"},
        {0.50, "0.5x"},
        {0.75, "0.75x"},
        {1.00, "1x"},
        {1.25, "1.25x"},
        {1.50, "1.5x"},
        {2.00, "2x"},
    };
    for (const auto& speedOption : speedOptions) {
        QAction* action = speedMenu->addAction(speedOption.second);
        action->setCheckable(true);
        action->setChecked(qFuzzyCompare(speedOption.first, 1.0));
        connect(action, &QAction::triggered, this, [this, speedMenu, speed = speedOption.first, label = speedOption.second]() {
            for (QAction* entry : speedMenu->actions()) {
                entry->setChecked(false);
            }
            if (QAction* action = qobject_cast<QAction*>(sender()); action != nullptr) {
                action->setChecked(true);
            }
            playbackRate_ = speed;
            speedButton_->setText(label);
            if (sfxRuntime_ != nullptr) {
                sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
            }
            if (playing_) {
                startPlayback();
            }
        });
    }
    speedButton_->setMenu(speedMenu);

    zoomOutButton_ = new QToolButton(this);
    zoomOutButton_->setText("-");
    zoomInButton_ = new QToolButton(this);
    zoomInButton_->setText("+");
    sfxVolumeSlider_ = new QSlider(Qt::Horizontal, this);
    sfxVolumeSlider_->setRange(0, 100);
    sfxVolumeSlider_->setValue(25);
    sfxVolumeSlider_->setFixedWidth(110);
    sfxVolumeValueLabel_ = new QLabel("25%", this);
    sfxVolumeValueLabel_->setMinimumWidth(42);
    playbackTimeLabel_ = new QLabel(this);
    playbackTimeLabel_->setMinimumWidth(170);
    playbackTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    controlsRow->addWidget(playPauseButton_);
    controlsRow->addWidget(stopButton_);
    controlsRow->addWidget(speedButton_);
    controlsRow->addSpacing(10);
    controlsRow->addWidget(new QLabel(localizedText("缩放", "Zoom"), this));
    controlsRow->addWidget(zoomOutButton_);
    controlsRow->addWidget(zoomInButton_);
    controlsRow->addSpacing(10);
    controlsRow->addWidget(new QLabel(localizedText("效果音", "SFX"), this));
    controlsRow->addWidget(sfxVolumeSlider_);
    controlsRow->addWidget(sfxVolumeValueLabel_);
    controlsRow->addStretch(1);
    controlsRow->addWidget(playbackTimeLabel_);
    rootLayout->addLayout(controlsRow);

    sfxRuntime_ = new QtPreviewSfxRuntime(this);
    sfxRuntime_->setChartPath(chartPath_);
    sfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    sfxRuntime_->reloadAssets(audioSettings_);

    playbackTimer_ = new QTimer(this);
    playbackTimer_->setTimerType(Qt::PreciseTimer);
    playbackTimer_->setInterval(8);
    connect(playbackTimer_, &QTimer::timeout, this, &LatencyDetectorDialog::onPlaybackTick);

    offsetReplayTimer_ = new QTimer(this);
    offsetReplayTimer_->setSingleShot(true);
    connect(offsetReplayTimer_, &QTimer::timeout, this, [this]() {
        startPlayback();
    });

    connect(playPauseButton_, &QPushButton::clicked, this, &LatencyDetectorDialog::togglePlayback);
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        pausePlayback();
        seekToSecond(0.0, true);
    });
    connect(detectBpmButton_, &QPushButton::clicked, this, [this]() {
        const double bpm = detectBpm();
        if (bpm > 0.0) {
            updateBpmEdit(bpm, true);
        }
    });
    connect(bpmHelpButton_, &QPushButton::clicked, this, &LatencyDetectorDialog::showBpmHelpDialog);
    connect(detectOffsetButton_, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double bpm = parsedBpm(&ok);
        if (!ok || bpm <= 0.0) {
            return;
        }
        const double offset = detectOffset(bpm);
        updateOffsetEdit(offset, true);
        restartPlaybackAfterOffsetChange();
    });
    connect(offsetEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyOffsetEdit();
    });
    connect(bpmEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyBpmEdit();
    });
    connect(meterCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateBeatOverlay();
        if (meterCombo_ != nullptr) {
            emit meterIdChanged(meterCombo_->currentData().toString());
        }
    });
    connect(zoomOutButton_, &QToolButton::clicked, this, [this]() {
        if (zoomLevel_ < 1) {
            ++zoomLevel_;
            applyZoomLevel(true);
        }
    });
    connect(zoomInButton_, &QToolButton::clicked, this, [this]() {
        if (zoomLevel_ > -1) {
            --zoomLevel_;
            applyZoomLevel(true);
        }
    });
    connect(playbackSlider_, &QSlider::sliderPressed, this, [this]() {
        playbackSliderDragging_ = true;
    });
    connect(playbackSlider_, &QSlider::sliderReleased, this, [this]() {
        playbackSliderDragging_ = false;
        seekToSecond(static_cast<double>(playbackSlider_->value()) / 1000.0, false);
    });
    connect(playbackSlider_, &QSlider::sliderMoved, this, [this](int value) {
        seekToSecond(static_cast<double>(value) / 1000.0, false);
    });
    connect(sfxVolumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        beatSfxVolume_ = qBound(0.0, static_cast<double>(value) * 0.04, 4.0);
        sfxVolumeValueLabel_->setText(QString("%1%").arg(value));
    });

    updateOffsetEdit(0.0, false);
}

void LatencyDetectorDialog::loadAudioAnalysis()
{
    const DecodedAudio decoded = decodeMonoTrack(trackPath_, kAnalysisSampleRate);
    decodedSamples_ = decoded.samples;
    trackDurationSeconds_ = decoded.durationSeconds;
    waveformPeaks_ = buildWaveformPeaks(decodedSamples_, kWaveformPeakCount);
    onsetEnvelope_ = buildOnsetEnvelope(decodedSamples_, kAnalysisSampleRate, &onsetStepSeconds_);
    offsetEnvelope_ = buildTransientEnvelope(decodedSamples_, kAnalysisSampleRate, &offsetStepSeconds_);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setWaveformData(waveformPeaks_, trackDurationSeconds_);
    detectBpmButton_->setEnabled(!onsetEnvelope_.isEmpty());
    visibleDurationSeconds_ = qMin(qMax(trackDurationSeconds_, kMinimumVisibleSeconds), 12.0);
    baseVisibleDurationSeconds_ = visibleDurationSeconds_;
    zoomLevel_ = 0;
}

void LatencyDetectorDialog::updatePlaybackUi()
{
    playPauseButton_->setText(playing_
        ? localizedText("暂停", "Pause")
        : localizedText("播放", "Play"));
    if (stopButton_ != nullptr) {
        stopButton_->setEnabled(playing_ || playheadSecond_ > 0.001);
    }
    if (playbackSlider_ != nullptr) {
        const int maxMs = qMax(0, qRound(trackDurationSeconds_ * 1000.0));
        if (playbackSlider_->maximum() != maxMs) {
            playbackSlider_->setRange(0, maxMs);
        }
        if (!playbackSliderDragging_) {
            const bool blocked = playbackSlider_->blockSignals(true);
            playbackSlider_->setValue(qBound(0, qRound(playheadSecond_ * 1000.0), maxMs));
            playbackSlider_->blockSignals(blocked);
        }
    }
    playbackTimeLabel_->setText(QString("%1 / %2").arg(formatTimestamp(playheadSecond_), formatTimestamp(trackDurationSeconds_)));
    detectOffsetButton_->setEnabled(parsedBpm() > 0.0);
    zoomOutButton_->setEnabled(zoomLevel_ < 1);
    zoomInButton_->setEnabled(zoomLevel_ > -1);
}

void LatencyDetectorDialog::updateBeatOverlay()
{
    bool bpmOk = false;
    const double bpm = parsedBpm(&bpmOk);
    bool offsetOk = false;
    const double offset = parsedOffset(&offsetOk);
    const QString selectedMeterId = meterCombo_ != nullptr ? meterCombo_->currentData().toString() : QStringLiteral("4/4");
    QString meterId = selectedMeterId;
    if (meterId == QLatin1String("auto")) {
        meterId = lastDetectedMeterId_;
    }
    const MeterPattern* pattern = meterPatternById(meterId);
    pendingBeatBpm_ = bpmOk ? bpm : 0.0;
    pendingBeatOffset_ = offsetOk ? offset : 0.0;
    pendingBeatBarPulseCount_ = pattern != nullptr ? qMax(1, pattern->accentWeights.size()) : 4;
    pendingBeatUseUniformAccent_ = selectedMeterId == QLatin1String("auto");
    pendingBeatAccentAnchorIndex_ = 0;
    pendingBeatAccentWeights_.clear();
    if (pendingBeatUseUniformAccent_) {
        pendingBeatAccentWeights_.append(1.0);
    } else if (pattern != nullptr && !pattern->accentWeights.isEmpty()) {
        const double maxWeight = *std::max_element(pattern->accentWeights.constBegin(), pattern->accentWeights.constEnd());
        const double safeMaxWeight = maxWeight > 1e-6 ? maxWeight : 1.0;
        for (double weight : pattern->accentWeights) {
            pendingBeatAccentWeights_.append(qBound(0.0, weight / safeMaxWeight, 1.0));
        }
    } else {
        pendingBeatAccentWeights_.append(1.0);
    }
    if (!pendingBeatUseUniformAccent_
        && pendingBeatBpm_ > 0.0
        && !pendingBeatAccentWeights_.isEmpty()
        && detectedMeterPhaseValid_
        && meterId == lastDetectedMeterId_) {
        const double beatPeriod = 60.0 / pendingBeatBpm_;
        if (beatPeriod > 0.0) {
            const double anchorBeats = (detectedMeterPhaseSecond_ - pendingBeatOffset_) / beatPeriod;
            pendingBeatAccentAnchorIndex_ = qRound(anchorBeats);
        }
    }
    if (pendingBeatBpm_ > 0.0) {
        static_cast<WaveformOverviewWidget*>(waveformView_)->setBeatGrid(
            pendingBeatBpm_,
            pendingBeatOffset_,
            pendingBeatBarPulseCount_
        );
    } else {
        static_cast<WaveformOverviewWidget*>(waveformView_)->clearBeatGrid();
    }
    updatePlaybackUi();
}

void LatencyDetectorDialog::smoothFollowPlayhead(bool forceCenter)
{
    if (trackDurationSeconds_ <= 0.0) {
        visibleStartSecond_ = 0.0;
        return;
    }
    const double maxStart = qMax(0.0, trackDurationSeconds_ - visibleDurationSeconds_);
    const double targetStart = qBound(0.0, playheadSecond_ - visibleDurationSeconds_ * 0.35, maxStart);
    if (forceCenter) {
        visibleStartSecond_ = targetStart;
        return;
    }

    const double leftBoundary = visibleStartSecond_ + visibleDurationSeconds_ * 0.15;
    const double rightBoundary = visibleStartSecond_ + visibleDurationSeconds_ * 0.85;
    if (playheadSecond_ < leftBoundary || playheadSecond_ > rightBoundary) {
        const double center = visibleStartSecond_ + visibleDurationSeconds_ * 0.5;
        const double normalizedDistance = qBound(
            0.0,
            qAbs(playheadSecond_ - center) / qMax(0.001, visibleDurationSeconds_ * 0.5),
            1.0
        );
        const double blend = 0.18 + 0.42 * normalizedDistance;
        visibleStartSecond_ = visibleStartSecond_ + (targetStart - visibleStartSecond_) * blend;
    }
    visibleStartSecond_ = qBound(0.0, visibleStartSecond_, maxStart);
}

void LatencyDetectorDialog::applyZoomLevel(bool centerOnPlayhead)
{
    const double base = qMax(kMinimumVisibleSeconds, baseVisibleDurationSeconds_);
    double factor = 1.0;
    if (zoomLevel_ > 0) {
        factor = 1.5;
    } else if (zoomLevel_ < 0) {
        factor = 1.0 / 1.5;
    }
    visibleDurationSeconds_ = base * factor;
    updateVisibleRange(centerOnPlayhead);
}

void LatencyDetectorDialog::updateVisibleRange(bool centerOnPlayhead)
{
    if (trackDurationSeconds_ <= 0.0) {
        visibleStartSecond_ = 0.0;
        visibleDurationSeconds_ = kMinimumVisibleSeconds;
    } else {
        visibleDurationSeconds_ = qBound(kMinimumVisibleSeconds, visibleDurationSeconds_, qMax(trackDurationSeconds_, kMinimumVisibleSeconds));
        if (centerOnPlayhead) {
            visibleStartSecond_ = playheadSecond_ - visibleDurationSeconds_ * 0.35;
        }
        const double maxStart = qMax(0.0, trackDurationSeconds_ - visibleDurationSeconds_);
        visibleStartSecond_ = qBound(0.0, visibleStartSecond_, maxStart);
    }
    static_cast<WaveformOverviewWidget*>(waveformView_)->setVisibleRange(visibleStartSecond_, visibleDurationSeconds_);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setPlayheadSecond(playheadSecond_);
    updatePlaybackUi();
}

void LatencyDetectorDialog::updateBpmEdit(double bpm, bool notify)
{
    const double normalized = qIsFinite(bpm) && bpm > 0.0 ? bpm : 0.0;
    if (normalized <= 0.0) {
        bpmEdit_->clear();
    } else {
        bpmEdit_->setText(QString::number(normalized, 'f', 3));
    }
    updateBeatOverlay();
    if (notify && normalized > 0.0) {
        emit bpmChanged(normalized);
    }
}

void LatencyDetectorDialog::updateOffsetEdit(double seconds, bool notify)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    offsetEdit_->setText(QString::number(normalized, 'f', 3));
    updateBeatOverlay();
    if (notify) {
        emit offsetChanged(normalized);
    }
}

void LatencyDetectorDialog::applyOffsetEdit()
{
    updateOffsetEdit(parsedOffset(), true);
    restartPlaybackAfterOffsetChange();
}

void LatencyDetectorDialog::applyBpmEdit()
{
    bool ok = false;
    const double bpm = parsedBpm(&ok);
    if (!ok || bpm <= 0.0) {
        updateBpmEdit(0.0, false);
    } else {
        updateBpmEdit(bpm, true);
    }
}

void LatencyDetectorDialog::showBpmHelpDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(localizedText("BPM检测说明", "BPM Detection Notes"));
    dialog.resize(460, 210);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(6);

    auto* infoEdit = new QPlainTextEdit(&dialog);
    infoEdit->setReadOnly(true);
    infoEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    infoEdit->setMinimumHeight(120);

    QPair<double, double> alt1{0.0, 0.0};
    QPair<double, double> alt2{0.0, 0.0};
    int altCount = 0;
    for (const auto& entry : lastDetectedBpmCandidates_) {
        if (lastDetectedBpm_ > 0.0 && qAbs(entry.first - lastDetectedBpm_) <= 0.05) {
            continue;
        }
        if (altCount == 0) {
            alt1 = entry;
        } else if (altCount == 1) {
            alt2 = entry;
        }
        ++altCount;
        if (altCount >= 2) {
            break;
        }
    }

    const QString latest = lastDetectedBpm_ > 0.0
        ? QString::number(lastDetectedBpm_, 'f', 3)
        : QStringLiteral("--");
    const QString altBpm1 = alt1.first > 0.0 ? QString::number(alt1.first, 'f', 3) : QStringLiteral("--");
    const QString altBpm2 = alt2.first > 0.0 ? QString::number(alt2.first, 'f', 3) : QStringLiteral("--");
    const double altScore1 = alt1.first > 0.0 ? alt1.second : 0.0;
    const double altScore2 = alt2.first > 0.0 ? alt2.second : 0.0;

    QString text = localizedText(
        "在测试前请先设置拍号。\n"
        "如果拍号复杂或未知，请选择自动检测。\n"
        "因乐曲本身存在变速或音频瞬态不明显，可能出现误差。\n"
        "\n"
        "最近一次检测结果：" + latest + " BPM\n"
        + QString("备选1：%1 BPM\n").arg(altBpm1)
        + QString("备选2：%1 BPM\n").arg(altBpm2),
        "Set meter before testing. \n"
        "Choose Auto when meter is complex or unknown.\n"
        "BPM detection may be inaccurate due to tempo changes or weak transients.\n"
        "\n"
        "Latest result: " + latest + " BPM\n"
        + QString("Alternative 1: %1 BPM\n").arg(altBpm1)
        + QString("Alternative 2: %1 BPM\n").arg(altBpm2)
    );

    infoEdit->setPlainText(text);
    rootLayout->addWidget(infoEdit, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);
    dialog.exec();
}

void LatencyDetectorDialog::seekToSecond(double second, bool centerView)
{
    playheadSecond_ = qBound(0.0, second, trackDurationSeconds_);
    if (sfxRuntime_ != nullptr) {
        if (playing_) {
            sfxRuntime_->startBackgroundTrack(playheadSecond_);
            playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
        } else {
            sfxRuntime_->pauseBackgroundTrack();
            sfxRuntime_->startBackgroundTrack(playheadSecond_);
            sfxRuntime_->pauseBackgroundTrack();
            playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
        }
    }
    smoothFollowPlayhead(centerView);
    lastBeatAuditionSecond_ = playheadSecond_;
    updateVisibleRange(false);
}

void LatencyDetectorDialog::togglePlayback()
{
    if (playing_) {
        pausePlayback();
    } else {
        startPlayback();
    }
}

void LatencyDetectorDialog::startPlayback()
{
    if (sfxRuntime_ == nullptr) {
        return;
    }
    sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    sfxRuntime_->startBackgroundTrack(playheadSecond_);
    playing_ = true;
    playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
    lastBeatAuditionSecond_ = playheadSecond_;
    playbackTimer_->start();
    updatePlaybackUi();
}

void LatencyDetectorDialog::pausePlayback()
{
    playing_ = false;
    if (sfxRuntime_ != nullptr) {
        sfxRuntime_->pauseBackgroundTrack();
    }
    if (playbackTimer_ != nullptr) {
        playbackTimer_->stop();
    }
    updatePlaybackUi();
}

void LatencyDetectorDialog::restartPlaybackAfterOffsetChange()
{
    updateBeatOverlay();
    if (!playing_) {
        return;
    }
    pausePlayback();
    offsetReplayTimer_->start(static_cast<int>(kOffsetReplayDelayMs));
}

void LatencyDetectorDialog::onPlaybackTick()
{
    if (sfxRuntime_ == nullptr) {
        return;
    }
    const double previousSecond = playheadSecond_;
    playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
    if (pendingBeatBpm_ > 0.0) {
        triggerBeatAudition(previousSecond, playheadSecond_);
    }
    if (playheadSecond_ >= trackDurationSeconds_ - 0.02) {
        pausePlayback();
        playheadSecond_ = trackDurationSeconds_;
    }
    smoothFollowPlayhead(false);
    updateVisibleRange(false);
}

void LatencyDetectorDialog::triggerBeatAudition(double fromSecond, double toSecond)
{
    if (sfxRuntime_ == nullptr || pendingBeatBpm_ <= 0.0 || toSecond + 1e-6 < fromSecond) {
        return;
    }
    const double beatPeriod = 60.0 / pendingBeatBpm_;
    if (beatPeriod <= 0.0) {
        return;
    }

    const double epsilon = 1e-5;
    const int beatIndex = static_cast<int>(qFloor((toSecond - pendingBeatOffset_) / beatPeriod));
    const double beatSecond = pendingBeatOffset_ + beatIndex * beatPeriod;
    if (beatSecond < fromSecond - epsilon || beatSecond > toSecond + epsilon) {
        return;
    }
    if (beatSecond <= lastBeatAuditionSecond_ + epsilon) {
        return;
    }

    double gain = 1.0;
    if (!pendingBeatUseUniformAccent_ && !pendingBeatAccentWeights_.isEmpty()) {
        const int accentCount = pendingBeatAccentWeights_.size();
        int accentIndex = (beatIndex - pendingBeatAccentAnchorIndex_) % accentCount;
        if (accentIndex < 0) {
            accentIndex += accentCount;
        }
        gain = pendingBeatAccentWeights_.at(accentIndex);
    }
    gain = qBound(0.0, (0.72 + 0.28 * gain) * beatSfxVolume_, 4.0);
    sfxRuntime_->audition("answer", gain);
    lastBeatAuditionSecond_ = beatSecond;
}

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
