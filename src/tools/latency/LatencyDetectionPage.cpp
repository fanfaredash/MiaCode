#include "LatencyDetectionPage.h"

#include "LatencyAnalysis.h"
#include "LatencySandboxController.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <QtMath>

namespace miacode::latency {

namespace {

constexpr int kEditDebounceMs = 300;
constexpr int kDefaultSfxVolumePercent = 80;
constexpr int kDecimalsBpm = 3;
constexpr int kDecimalsOffset = 3;

QString latencySfxVolumeSettingsKey()
{
    return QStringLiteral("latency/sfxVolumePercent");
}

QString cardStyleSheet()
{
    return QStringLiteral(
        "QFrame#LatencyCard {"
        " background: #FFFFFF;"
        " border: 1px solid #E1E7EF;"
        " border-radius: 6px;"
        "}"
        "QLabel[role=\"cardTitle\"] {"
        " color: #243447;"
        " font-weight: 600;"
        "}"
        "QLabel[role=\"cardHint\"] {"
        " color: #6B7689;"
        "}"
        "QLabel[role=\"detectResult\"] {"
        " color: #1E3A66;"
        "}"
    );
}

}  // namespace

LatencyDetectionPage::LatencyDetectionPage(MainWindow* owner, QWidget* parent)
    : QWidget(parent)
    , owner_(owner)
{
    setObjectName(QStringLiteral("LatencyDetectionPage"));
    if (owner_ != nullptr) {
        sandbox_ = owner_->latencySandboxController();
    }

    bpmDebounceTimer_ = new QTimer(this);
    bpmDebounceTimer_->setSingleShot(true);
    bpmDebounceTimer_->setInterval(kEditDebounceMs);
    connect(bpmDebounceTimer_, &QTimer::timeout, this, &LatencyDetectionPage::commitBpmEdit);

    offsetDebounceTimer_ = new QTimer(this);
    offsetDebounceTimer_->setSingleShot(true);
    offsetDebounceTimer_->setInterval(kEditDebounceMs);
    connect(offsetDebounceTimer_, &QTimer::timeout, this, &LatencyDetectionPage::commitOffsetEdit);

    buildUi();

    if (!sandbox_.isNull()) {
        connect(sandbox_, &LatencySandboxController::auditionStateChanged,
                this, &LatencyDetectionPage::onAuditionStateChanged);
        connect(sandbox_, &LatencySandboxController::playheadAdvanced,
                this, &LatencyDetectionPage::onPlayheadAdvanced);

        const int storedVolume = QSettings().value(
            latencySfxVolumeSettingsKey(), kDefaultSfxVolumePercent).toInt();
        sandbox_->setSfxVolumePercent(storedVolume);
        if (sfxVolumeSlider_ != nullptr) {
            QSignalBlocker blocker(sfxVolumeSlider_);
            sfxVolumeSlider_->setValue(storedVolume);
        }
        if (sfxVolumeValueLabel_ != nullptr) {
            sfxVolumeValueLabel_->setText(QStringLiteral("%1%").arg(storedVolume));
        }
    }
    updateAuditionUi(false);
    updatePositionLabel(0.0);
}

LatencyDetectionPage::~LatencyDetectionPage() = default;

void LatencyDetectionPage::refreshFromDocument()
{
    if (owner_.isNull() || suppressDocumentRefresh_) {
        return;
    }
    const double bpm = documentWholeBpm();
    const double offset = documentOffsetSeconds();
    if (bpmEdit_ != nullptr) {
        QSignalBlocker blocker(bpmEdit_);
        bpmEdit_->setValue(bpm > 0.0 ? bpm : 120.0);
    }
    if (offsetEdit_ != nullptr) {
        QSignalBlocker blocker(offsetEdit_);
        offsetEdit_->setValue(offset);
    }
    if (!sandbox_.isNull()) {
        if (bpm > 0.0) {
            sandbox_->setBpm(bpm);
        }
        sandbox_->setOffsetSeconds(offset);
    }
    updateAutoDetectAvailability();
}

void LatencyDetectionPage::onPageEntered()
{
    if (!sandbox_.isNull()) {
        sandbox_->setOnPage(true);
    }
    refreshFromDocument();
}

void LatencyDetectionPage::onPageLeft()
{
    if (!sandbox_.isNull()) {
        sandbox_->setOnPage(false);  // also stops audition
    }
    clearAudioEnvelopeCache();
}

void LatencyDetectionPage::buildUi()
{
    setStyleSheet(cardStyleSheet());

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll, 1);

    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 24, 28, 24);
    contentLayout->setSpacing(16);
    scroll->setWidget(content);

    const QFont titleFont = miacode::mainwindow::shared::uiAccentFont(13, QFont::DemiBold);
    const QFont hintFont = miacode::mainwindow::shared::uiOutputFont();

    auto makeCard = [&](const QString& titleText) -> QPair<QFrame*, QVBoxLayout*> {
        auto* card = new QFrame(content);
        card->setObjectName(QStringLiteral("LatencyCard"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(10);
        auto* title = new QLabel(titleText, card);
        title->setProperty("role", "cardTitle");
        title->setFont(titleFont);
        layout->addWidget(title);
        return {card, layout};
    };

    // -------- BPM card --------
    auto bpmPair = makeCard(localizedText(QStringLiteral("BPM"), QStringLiteral("BPM")));
    auto* bpmCard = bpmPair.first;
    auto* bpmLayout = bpmPair.second;
    auto* bpmRow = new QHBoxLayout();
    bpmRow->setSpacing(10);
    bpmEdit_ = new QDoubleSpinBox(bpmCard);
    bpmEdit_->setRange(1.0, 999.0);
    bpmEdit_->setDecimals(kDecimalsBpm);
    bpmEdit_->setSingleStep(0.5);
    bpmEdit_->setValue(120.0);
    bpmEdit_->setMinimumWidth(160);
    bpmEdit_->setKeyboardTracking(true);
    bpmEdit_->setAccelerated(true);
    bpmEdit_->setSuffix(QStringLiteral(""));
    connect(bpmEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LatencyDetectionPage::onBpmEditValueChanged);
    bpmRow->addWidget(bpmEdit_);
    detectBpmButton_ = new QPushButton(
        localizedText(QStringLiteral("自动检测 BPM"), QStringLiteral("Auto-detect BPM")), bpmCard);
    detectBpmButton_->setCursor(Qt::PointingHandCursor);
    connect(detectBpmButton_, &QPushButton::clicked, this, &LatencyDetectionPage::onDetectBpmClicked);
    bpmRow->addWidget(detectBpmButton_);
    bpmDetectResultLabel_ = new QLabel(QString(), bpmCard);
    bpmDetectResultLabel_->setProperty("role", "detectResult");
    bpmDetectResultLabel_->setFont(hintFont);
    bpmRow->addWidget(bpmDetectResultLabel_, 1);
    bpmLayout->addLayout(bpmRow);
    auto* bpmHint = new QLabel(
        localizedText(
            QStringLiteral("以拍/分钟为单位，精度 3 位小数。编辑后自动写入谱面。"),
            QStringLiteral("Beats per minute, 3-decimal precision. Edits auto-save to the chart.")),
        bpmCard);
    bpmHint->setProperty("role", "cardHint");
    bpmHint->setFont(hintFont);
    bpmHint->setWordWrap(true);
    bpmLayout->addWidget(bpmHint);
    contentLayout->addWidget(bpmCard);

    // -------- Offset card --------
    auto offsetPair = makeCard(
        localizedText(QStringLiteral("起始偏移 (First / Offset)"), QStringLiteral("Start Offset (First)")));
    auto* offsetCard = offsetPair.first;
    auto* offsetLayout = offsetPair.second;
    auto* offsetRow = new QHBoxLayout();
    offsetRow->setSpacing(10);
    offsetEdit_ = new QDoubleSpinBox(offsetCard);
    offsetEdit_->setRange(-999.0, 999.0);
    offsetEdit_->setDecimals(kDecimalsOffset);
    offsetEdit_->setSingleStep(0.010);
    offsetEdit_->setValue(0.0);
    offsetEdit_->setMinimumWidth(160);
    offsetEdit_->setKeyboardTracking(true);
    offsetEdit_->setAccelerated(true);
    offsetEdit_->setSuffix(localizedText(QStringLiteral(" 秒"), QStringLiteral(" s")));
    connect(offsetEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LatencyDetectionPage::onOffsetEditValueChanged);
    offsetRow->addWidget(offsetEdit_);
    detectOffsetButton_ = new QPushButton(
        localizedText(QStringLiteral("自动检测 Offset"), QStringLiteral("Auto-detect Offset")), offsetCard);
    detectOffsetButton_->setCursor(Qt::PointingHandCursor);
    connect(detectOffsetButton_, &QPushButton::clicked, this, &LatencyDetectionPage::onDetectOffsetClicked);
    offsetRow->addWidget(detectOffsetButton_);
    offsetDetectResultLabel_ = new QLabel(QString(), offsetCard);
    offsetDetectResultLabel_->setProperty("role", "detectResult");
    offsetDetectResultLabel_->setFont(hintFont);
    offsetRow->addWidget(offsetDetectResultLabel_, 1);
    offsetLayout->addLayout(offsetRow);
    auto* offsetHint = new QLabel(
        localizedText(
            QStringLiteral("第一个下拍相对音频起点的偏移（秒）。编辑后自动写入谱面。"),
            QStringLiteral("Time of the first downbeat relative to the audio start (seconds). Auto-saves.")),
        offsetCard);
    offsetHint->setProperty("role", "cardHint");
    offsetHint->setFont(hintFont);
    offsetHint->setWordWrap(true);
    offsetLayout->addWidget(offsetHint);
    contentLayout->addWidget(offsetCard);

    // -------- Audition (sandbox) card --------
    auto auditionPair = makeCard(
        localizedText(QStringLiteral("节奏校准试听"), QStringLiteral("Rhythm Calibration Audition")));
    auto* auditionCard = auditionPair.first;
    auto* auditionLayout = auditionPair.second;

    auto* subdivRow = new QHBoxLayout();
    subdivRow->setSpacing(16);
    auto* subdivLabel = new QLabel(
        localizedText(QStringLiteral("细分:"), QStringLiteral("Subdivision:")), auditionCard);
    subdivLabel->setProperty("role", "cardHint");
    subdivLabel->setFont(hintFont);
    subdivRow->addWidget(subdivLabel);
    subdivision4Radio_ = new QRadioButton(QStringLiteral("4"), auditionCard);
    subdivision8Radio_ = new QRadioButton(QStringLiteral("8"), auditionCard);
    subdivision4Radio_->setChecked(true);
    subdivision4Radio_->setCursor(Qt::PointingHandCursor);
    subdivision8Radio_->setCursor(Qt::PointingHandCursor);
    auto* subdivGroup = new QButtonGroup(this);
    subdivGroup->addButton(subdivision4Radio_, 4);
    subdivGroup->addButton(subdivision8Radio_, 8);
    connect(subdivision4Radio_, &QRadioButton::toggled, this, &LatencyDetectionPage::onSubdivisionToggled);
    connect(subdivision8Radio_, &QRadioButton::toggled, this, &LatencyDetectionPage::onSubdivisionToggled);
    subdivRow->addWidget(subdivision4Radio_);
    subdivRow->addWidget(subdivision8Radio_);
    subdivRow->addStretch(1);
    auditionLayout->addLayout(subdivRow);

    auto* transportRow = new QHBoxLayout();
    transportRow->setSpacing(12);
    auditionButton_ = new QPushButton(
        localizedText(QStringLiteral("▶ 开始试听"), QStringLiteral("▶ Start Audition")), auditionCard);
    auditionButton_->setCursor(Qt::PointingHandCursor);
    auditionButton_->setMinimumWidth(160);
    connect(auditionButton_, &QPushButton::clicked, this, &LatencyDetectionPage::onAuditionButtonClicked);
    transportRow->addWidget(auditionButton_);
    positionLabel_ = new QLabel(QStringLiteral("00:00.000 / 00:00.000"), auditionCard);
    positionLabel_->setFont(miacode::mainwindow::shared::uiMonoFont(10));
    transportRow->addWidget(positionLabel_, 1);
    auditionLayout->addLayout(transportRow);

    auto* volumeRow = new QHBoxLayout();
    volumeRow->setSpacing(10);
    auto* volumeLabel = new QLabel(
        localizedText(QStringLiteral("SFX 音量"), QStringLiteral("SFX Volume")), auditionCard);
    volumeLabel->setProperty("role", "cardHint");
    volumeLabel->setFont(hintFont);
    volumeRow->addWidget(volumeLabel);
    sfxVolumeSlider_ = new QSlider(Qt::Horizontal, auditionCard);
    sfxVolumeSlider_->setRange(0, 100);
    sfxVolumeSlider_->setValue(kDefaultSfxVolumePercent);
    sfxVolumeSlider_->setMinimumWidth(180);
    connect(sfxVolumeSlider_, &QSlider::valueChanged, this, &LatencyDetectionPage::onSfxVolumeChanged);
    volumeRow->addWidget(sfxVolumeSlider_, 1);
    sfxVolumeValueLabel_ = new QLabel(QStringLiteral("%1%").arg(kDefaultSfxVolumePercent), auditionCard);
    sfxVolumeValueLabel_->setMinimumWidth(48);
    sfxVolumeValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sfxVolumeValueLabel_->setFont(hintFont);
    volumeRow->addWidget(sfxVolumeValueLabel_);
    auto* volumeResetButton = new QPushButton(
        localizedText(QStringLiteral("重置"), QStringLiteral("Reset")), auditionCard);
    volumeResetButton->setCursor(Qt::PointingHandCursor);
    connect(volumeResetButton, &QPushButton::clicked, this, [this]() {
        if (sfxVolumeSlider_ != nullptr) {
            sfxVolumeSlider_->setValue(kDefaultSfxVolumePercent);
        }
    });
    volumeRow->addWidget(volumeResetButton);
    auditionLayout->addLayout(volumeRow);

    auto* sfxHint = new QLabel(
        localizedText(
            QStringLiteral("该音量仅作用于本页面试听，不影响其它播放设置。"),
            QStringLiteral("This volume applies only to the audition on this page; other audio settings are untouched.")),
        auditionCard);
    sfxHint->setProperty("role", "cardHint");
    sfxHint->setFont(hintFont);
    sfxHint->setWordWrap(true);
    auditionLayout->addWidget(sfxHint);

    sandboxHintLabel_ = new QLabel(
        localizedText(
            QStringLiteral("ⓘ 试听期间播放的是临时测试谱面，不会修改你的谱面内容。"),
            QStringLiteral("ⓘ The audition plays a temporary test chart — your real chart is never modified.")),
        auditionCard);
    sandboxHintLabel_->setProperty("role", "cardHint");
    sandboxHintLabel_->setFont(hintFont);
    sandboxHintLabel_->setWordWrap(true);
    auditionLayout->addWidget(sandboxHintLabel_);

    contentLayout->addWidget(auditionCard);
    contentLayout->addStretch(1);
}

void LatencyDetectionPage::onBpmEditValueChanged(double value)
{
    Q_UNUSED(value);
    if (bpmDebounceTimer_ != nullptr) {
        bpmDebounceTimer_->start();
    }
}

void LatencyDetectionPage::onOffsetEditValueChanged(double value)
{
    Q_UNUSED(value);
    if (offsetDebounceTimer_ != nullptr) {
        offsetDebounceTimer_->start();
    }
}

void LatencyDetectionPage::commitBpmEdit()
{
    if (owner_.isNull() || bpmEdit_ == nullptr) {
        return;
    }
    const double value = bpmEdit_->value();
    if (!(value > 0.0)) {
        return;
    }
    suppressDocumentRefresh_ = true;
    owner_->applyLatencyDetectorBpm(value);
    suppressDocumentRefresh_ = false;
    if (!sandbox_.isNull()) {
        sandbox_->setBpm(value);
    }
}

void LatencyDetectionPage::commitOffsetEdit()
{
    if (owner_.isNull() || offsetEdit_ == nullptr) {
        return;
    }
    const double value = offsetEdit_->value();
    suppressDocumentRefresh_ = true;
    owner_->applyLatencyDetectorOffset(value);
    suppressDocumentRefresh_ = false;
    if (!sandbox_.isNull()) {
        sandbox_->setOffsetSeconds(value);
    }
}

void LatencyDetectionPage::onSubdivisionToggled()
{
    if (sandbox_.isNull()) {
        return;
    }
    const int subdivision = (subdivision8Radio_ != nullptr && subdivision8Radio_->isChecked()) ? 8 : 4;
    sandbox_->setSubdivision(subdivision);
}

void LatencyDetectionPage::onSfxVolumeChanged(int value)
{
    if (sfxVolumeValueLabel_ != nullptr) {
        sfxVolumeValueLabel_->setText(QStringLiteral("%1%").arg(value));
    }
    if (!sandbox_.isNull()) {
        sandbox_->setSfxVolumePercent(value);
    }
    QSettings().setValue(latencySfxVolumeSettingsKey(), value);
}

void LatencyDetectionPage::onAuditionButtonClicked()
{
    if (sandbox_.isNull()) {
        return;
    }
    sandbox_->toggleAudition();
}

void LatencyDetectionPage::onDetectBpmClicked()
{
    if (owner_.isNull()) {
        return;
    }
    const QString trackPath = currentTrackPath();
    if (trackPath.isEmpty()) {
        if (bpmDetectResultLabel_ != nullptr) {
            bpmDetectResultLabel_->setText(localizedText(
                QStringLiteral("缺少歌曲音频"), QStringLiteral("Track audio missing")));
        }
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ensureAudioEnvelopeReady();
    const auto result = latency_analysis::detectBpm(cachedOnsetEnvelope_);
    QApplication::restoreOverrideCursor();
    if (!(result.bpm > 0.0)) {
        if (bpmDetectResultLabel_ != nullptr) {
            bpmDetectResultLabel_->setText(localizedText(
                QStringLiteral("未检测到 BPM"), QStringLiteral("BPM not detected")));
        }
        return;
    }
    lastDetectedMeterId_ = result.meterId;
    lastDetectedMeterPhase_ = result.meterPhaseSeconds;
    hasLastDetectedMeterPhase_ = result.meterPhaseValid;
    if (bpmEdit_ != nullptr) {
        QSignalBlocker blocker(bpmEdit_);
        bpmEdit_->setValue(result.bpm);
    }
    commitBpmEdit();
    if (bpmDetectResultLabel_ != nullptr) {
        bpmDetectResultLabel_->setText(localizedText(
            QStringLiteral("检测结果: %1"),
            QStringLiteral("Detected: %1")).arg(result.bpm, 0, 'f', kDecimalsBpm));
    }
}

void LatencyDetectionPage::onDetectOffsetClicked()
{
    if (owner_.isNull()) {
        return;
    }
    const QString trackPath = currentTrackPath();
    if (trackPath.isEmpty()) {
        if (offsetDetectResultLabel_ != nullptr) {
            offsetDetectResultLabel_->setText(localizedText(
                QStringLiteral("缺少歌曲音频"), QStringLiteral("Track audio missing")));
        }
        return;
    }
    const double bpm = bpmEdit_ != nullptr ? bpmEdit_->value() : 0.0;
    if (!(bpm > 0.0)) {
        if (offsetDetectResultLabel_ != nullptr) {
            offsetDetectResultLabel_->setText(localizedText(
                QStringLiteral("先设置/检测 BPM"), QStringLiteral("Set or detect BPM first")));
        }
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ensureAudioEnvelopeReady();
    latency_analysis::OffsetDetectionInputs inputs;
    inputs.bpm = bpm;
    inputs.offsetAnchorSeconds = offsetEdit_ != nullptr ? offsetEdit_->value() : 0.0;
    inputs.trackDurationSeconds = cachedAudioDurationSeconds_;
    inputs.meterId = QStringLiteral("auto");
    inputs.snapMode = QStringLiteral("bar");
    inputs.lastDetectedMeterPhase = lastDetectedMeterPhase_;
    inputs.hasLastDetectedMeterPhase = hasLastDetectedMeterPhase_;
    inputs.lastDetectedMeterId = lastDetectedMeterId_;
    const double offset = latency_analysis::detectOffset(
        cachedOnsetEnvelope_, cachedTransientEnvelope_, inputs);
    QApplication::restoreOverrideCursor();
    if (offsetEdit_ != nullptr) {
        QSignalBlocker blocker(offsetEdit_);
        offsetEdit_->setValue(offset);
    }
    commitOffsetEdit();
    if (offsetDetectResultLabel_ != nullptr) {
        offsetDetectResultLabel_->setText(localizedText(
            QStringLiteral("检测结果: %1 秒"),
            QStringLiteral("Detected: %1 s")).arg(offset, 0, 'f', kDecimalsOffset));
    }
}

void LatencyDetectionPage::onAuditionStateChanged(bool running)
{
    updateAuditionUi(running);
    if (!running) {
        updatePositionLabel(0.0);
    }
}

void LatencyDetectionPage::onPlayheadAdvanced(double seconds)
{
    updatePositionLabel(seconds);
}

void LatencyDetectionPage::updateAuditionUi(bool running)
{
    if (auditionButton_ != nullptr) {
        auditionButton_->setText(running
            ? localizedText(QStringLiteral("■ 停止试听"), QStringLiteral("■ Stop Audition"))
            : localizedText(QStringLiteral("▶ 开始试听"), QStringLiteral("▶ Start Audition")));
    }
}

void LatencyDetectionPage::updatePositionLabel(double seconds)
{
    if (positionLabel_ == nullptr) {
        return;
    }
    const double total = cachedAudioDurationSeconds_ > 0.0
        ? cachedAudioDurationSeconds_
        : (owner_.isNull() ? 0.0 : owner_->state_.previewTrackDurationSeconds_);
    positionLabel_->setText(QStringLiteral("%1 / %2")
        .arg(formatPosition(qMax(0.0, seconds)))
        .arg(formatPosition(qMax(0.0, total))));
}

void LatencyDetectionPage::updateAutoDetectAvailability()
{
    const bool hasAudio = !currentTrackPath().isEmpty();
    if (detectBpmButton_ != nullptr) {
        detectBpmButton_->setEnabled(hasAudio);
        detectBpmButton_->setToolTip(hasAudio
            ? QString()
            : localizedText(
                QStringLiteral("需要先加载歌曲音频"),
                QStringLiteral("Requires a loaded track audio file")));
    }
    if (detectOffsetButton_ != nullptr) {
        detectOffsetButton_->setEnabled(hasAudio);
        detectOffsetButton_->setToolTip(hasAudio
            ? QString()
            : localizedText(
                QStringLiteral("需要先加载歌曲音频"),
                QStringLiteral("Requires a loaded track audio file")));
    }
}

void LatencyDetectionPage::ensureAudioEnvelopeReady()
{
    const QString trackPath = currentTrackPath();
    if (trackPath.isEmpty()) {
        clearAudioEnvelopeCache();
        return;
    }
    if (trackPath == cachedAudioPath_
        && !cachedOnsetEnvelope_.isEmpty()
        && !cachedTransientEnvelope_.isEmpty()) {
        return;
    }
    const auto decoded = latency_analysis::decodeMonoTrack(trackPath);
    if (decoded.samples.isEmpty() || decoded.sampleRate <= 0) {
        clearAudioEnvelopeCache();
        return;
    }
    cachedAudioPath_ = trackPath;
    cachedAudioDurationSeconds_ = decoded.durationSeconds;
    cachedOnsetEnvelope_ = latency_analysis::buildOnsetEnvelope(decoded.samples, decoded.sampleRate);
    cachedTransientEnvelope_ = latency_analysis::buildTransientEnvelope(decoded.samples, decoded.sampleRate);
}

void LatencyDetectionPage::clearAudioEnvelopeCache()
{
    cachedAudioPath_.clear();
    cachedOnsetEnvelope_ = latency_analysis::Envelope();
    cachedTransientEnvelope_ = latency_analysis::Envelope();
    cachedAudioDurationSeconds_ = 0.0;
}

QString LatencyDetectionPage::formatPosition(double seconds) const
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 secs = (totalMs / 1000) % 60;
    const qint64 millis = totalMs % 1000;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString LatencyDetectionPage::localizedText(const QString& zh, const QString& en) const
{
    return UiText::isChineseUi() ? zh : en;
}

QString LatencyDetectionPage::currentTrackPath() const
{
    if (owner_.isNull()) {
        return QString();
    }
    return owner_->state_.lastTrackPath_;
}

double LatencyDetectionPage::documentOffsetSeconds() const
{
    if (owner_.isNull()) {
        return 0.0;
    }
    bool ok = false;
    const double value = owner_->state_.document_.first.trimmed().toDouble(&ok);
    return ok ? value : 0.0;
}

double LatencyDetectionPage::documentWholeBpm() const
{
    if (owner_.isNull()) {
        return 0.0;
    }
    bool ok = false;
    const double value = owner_->parsedWholeBpm(&ok);
    return ok ? value : 0.0;
}

}  // namespace miacode::latency
