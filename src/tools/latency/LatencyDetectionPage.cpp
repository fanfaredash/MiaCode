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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <QtMath>

namespace miacode::latency {

namespace {

constexpr int kEditDebounceMs = 300;
constexpr int kDefaultSfxVolumePercent = 50;
constexpr int kDecimalsBpm = 3;
constexpr int kDecimalsOffset = 3;

QString latencySfxVolumeSettingsKey()
{
    return QStringLiteral("latency/sfxVolumePercent");
}

// The BPM/offset spin boxes and the SFX slider live in a scroll area, so a
// mouse wheel over them used to nudge the value (easy to do by accident while
// scrolling the page). Ignoring the wheel event lets it bubble up to the
// scroll area instead, so the controls only change via click/keyboard.
class NoWheelDoubleSpinBox final : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelSlider final : public QSlider
{
public:
    using QSlider::QSlider;

protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

class NoWheelSpinBox final : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;

protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

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

    clockCountDebounceTimer_ = new QTimer(this);
    clockCountDebounceTimer_->setSingleShot(true);
    clockCountDebounceTimer_->setInterval(kEditDebounceMs);
    connect(clockCountDebounceTimer_, &QTimer::timeout, this, &LatencyDetectionPage::commitClockCountEdit);

    // NOTE (FAILED ATTEMPT, reverted): we tried firing a one-shot "tap" SFX
    // while dragging the SFX-volume slider so the user could hear the level.
    // It could not be made to stay silent during playback — the tap still
    // sounded while the chart was playing despite guarding on qtPreviewPlaying_.
    // Primary suspicion: somewhere an extra play/audition event is being
    // scheduled (the guard is bypassed or a stale timer fires), so the feature
    // was removed. onSfxVolumeChanged now only adjusts the volume.

    buildUi();

    // Ctrl+S on this page is handled by an application-level event filter (see
    // eventFilter), NOT by a page QShortcut. A page QShortcut on the same key as
    // the global Save QAction is "ambiguous" and Qt then fires neither.
    //
    // NOTE (FAILED ATTEMPT, reverted): page-level Ctrl+Z / Ctrl+Y for the
    // BPM/offset history was also attempted here but never worked reliably and
    // has been removed — see the comment on eventFilter(). The page no longer
    // intercepts undo/redo; those keys fall through to the global handler.
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);

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

void LatencyDetectionPage::applyThemeStyles()
{
    setStyleSheet(UiTheme::latencyDetectionPageStyleSheet());
}

void LatencyDetectionPage::refreshFromDocument()
{
    if (owner_.isNull() || suppressDocumentRefresh_) {
        return;
    }
    const double bpm = documentWholeBpm();
    const double offset = documentOffsetSeconds();
    const int clockCount = documentClockCount();
    if (bpmEdit_ != nullptr) {
        QSignalBlocker blocker(bpmEdit_);
        bpmEdit_->setValue(bpm > 0.0 ? bpm : 120.0);
    }
    if (offsetEdit_ != nullptr) {
        QSignalBlocker blocker(offsetEdit_);
        offsetEdit_->setValue(offset);
    }
    if (clockCountEdit_ != nullptr) {
        QSignalBlocker blocker(clockCountEdit_);
        clockCountEdit_->setValue(clockCount > 0 ? clockCount : 4);
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
    // Sync BPM/Offset into the sandbox first so the test chart it installs on
    // entry is built from the document's values rather than stale defaults.
    refreshFromDocument();
    if (!sandbox_.isNull()) {
        sandbox_->setOnPage(true);
    }
    // Take focus so the page-local Save/Undo/Redo shortcuts are live
    // immediately (otherwise the sidebar list keeps focus on entry).
    setFocus(Qt::OtherFocusReason);
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
    applyThemeStyles();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Return bar (L-A migration): this page lost its sidebar item and is now
    // reached from the metadata page's "延迟与偏移校准" entry card (or the
    // Tools menu), so it carries its own way back. switchToMetadataField()
    // runs the full leave semantics (onPageLeft teardown, bottom-tab OFF).
    auto* backBar = new QWidget(this);
    auto* backBarLayout = new QHBoxLayout(backBar);
    backBarLayout->setContentsMargins(22, 12, 28, 0);
    backBarLayout->setSpacing(0);
    auto* backButton = new QPushButton(
        localizedText(QStringLiteral("← 返回谱面信息"), QStringLiteral("← Back to Chart Info")), backBar);
    backButton->setObjectName(QStringLiteral("LatencyBackButton"));
    backButton->setCursor(Qt::PointingHandCursor);
    backButton->setFlat(true);
    backButton->setFocusPolicy(Qt::NoFocus);
    connect(backButton, &QPushButton::clicked, this, [this]() {
        if (!owner_.isNull()) {
            owner_->switchToMetadataField();
        }
    });
    backBarLayout->addWidget(backButton, 0, Qt::AlignLeft);
    backBarLayout->addStretch(1);
    // Media-tools launcher lives in the page header (it opens a dialog and is a
    // page-level action, not a per-parameter control). Keeping it off the Offset
    // row lets every parameter row stay the same shape: label / spin / detect /
    // result.
    auto* mediaToolsButton = new QPushButton(
        localizedText(QStringLiteral("音频/视频处理"), QStringLiteral("Audio/Video Processing")), backBar);
    mediaToolsButton->setObjectName(QStringLiteral("LatencyMediaToolsButton"));
    mediaToolsButton->setIcon(
        miacode::mainwindow::shared::makeSettingsGearIcon(UiTheme::colors().accent));
    mediaToolsButton->setIconSize(QSize(16, 16));
    mediaToolsButton->setCursor(Qt::PointingHandCursor);
    mediaToolsButton->setToolTip(localizedText(
        QStringLiteral("打开音频/视频处理工具：采样率转换 / 视频压缩 / 开头静音 / 开头黑幕。"),
        QStringLiteral("Open audio/video tools: sample-rate convert / compress video / prepend silence / prepend black.")));
    connect(mediaToolsButton, &QPushButton::clicked, this, [this]() {
        if (!owner_.isNull()) {
            owner_->onMediaProcessingTools();
        }
    });
    backBarLayout->addWidget(mediaToolsButton, 0, Qt::AlignRight);
    outer->addWidget(backBar, 0);

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

    // -------- Chart parameters card (BPM / Offset / clock_count) --------
    // One card, three symmetric rows laid out in a 4-column grid
    // (label | spin box | auto-detect | result) so the columns line up
    // vertically. Merging the former BPM / Offset / clock_count cards into a
    // single card keeps the new clock_count control on the same page without
    // scrolling; the media-tools launcher moved to the back bar so each row has
    // the identical shape.
    auto paramPair = makeCard(
        localizedText(QStringLiteral("谱面参数"), QStringLiteral("Chart Parameters")));
    auto* paramCard = paramPair.first;
    auto* paramLayout = paramPair.second;
    auto* paramGrid = new QGridLayout();
    paramGrid->setHorizontalSpacing(10);
    paramGrid->setVerticalSpacing(10);
    paramGrid->setColumnStretch(3, 1);  // result column soaks up the slack

    auto makeRowLabel = [&](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, paramCard);
        label->setProperty("role", "cardHint");
        label->setFont(hintFont);
        return label;
    };

    // Detection-result labels are secondary; render them a touch smaller so the
    // "检测结果: …" text fits the narrowed result column without clipping.
    QFont resultFont = hintFont;
    resultFont.setPointSize(qMax(8, hintFont.pointSize() - 2));

    // Row 0: BPM
    paramGrid->addWidget(makeRowLabel(QStringLiteral("BPM")), 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    bpmEdit_ = new NoWheelDoubleSpinBox(paramCard);
    bpmEdit_->setRange(1.0, 999.0);
    bpmEdit_->setDecimals(kDecimalsBpm);
    bpmEdit_->setSingleStep(0.5);
    bpmEdit_->setValue(120.0);
    bpmEdit_->setMinimumWidth(110);
    // keyboardTracking off: valueChanged fires only on a settled value (Enter /
    // focus-out / step button), so a half-typed value (e.g. "8" while typing
    // "0.893") is never applied to the document or preview.
    bpmEdit_->setKeyboardTracking(false);
    bpmEdit_->setAccelerated(true);
    connect(bpmEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LatencyDetectionPage::onBpmEditValueChanged);
    paramGrid->addWidget(bpmEdit_, 0, 1, Qt::AlignVCenter);
    detectBpmButton_ = new QPushButton(
        localizedText(QStringLiteral("自动检测"), QStringLiteral("Auto-detect")), paramCard);
    detectBpmButton_->setCursor(Qt::PointingHandCursor);
    connect(detectBpmButton_, &QPushButton::clicked, this, &LatencyDetectionPage::onDetectBpmClicked);
    paramGrid->addWidget(detectBpmButton_, 0, 2, Qt::AlignVCenter);
    bpmDetectResultLabel_ = new QLabel(QString(), paramCard);
    bpmDetectResultLabel_->setProperty("role", "detectResult");
    bpmDetectResultLabel_->setFont(resultFont);
    paramGrid->addWidget(bpmDetectResultLabel_, 0, 3, Qt::AlignLeft | Qt::AlignVCenter);

    // Row 1: Offset (&first). The auto-detect Offset entry point is shown here
    // again (it had been hidden); its wiring was always intact.
    paramGrid->addWidget(
        makeRowLabel(localizedText(QStringLiteral("偏移"), QStringLiteral("Offset"))),
        1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    offsetEdit_ = new NoWheelDoubleSpinBox(paramCard);
    offsetEdit_->setRange(-999.0, 999.0);
    offsetEdit_->setDecimals(kDecimalsOffset);
    offsetEdit_->setSingleStep(0.010);
    offsetEdit_->setValue(0.0);
    offsetEdit_->setMinimumWidth(110);
    // keyboardTracking off: see the BPM field above — avoids applying an
    // intermediate value such as "893" while the user is typing "0.893".
    offsetEdit_->setKeyboardTracking(false);
    offsetEdit_->setAccelerated(true);
    offsetEdit_->setSuffix(localizedText(QStringLiteral(" 秒"), QStringLiteral(" s")));
    connect(offsetEdit_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &LatencyDetectionPage::onOffsetEditValueChanged);
    paramGrid->addWidget(offsetEdit_, 1, 1, Qt::AlignVCenter);
    detectOffsetButton_ = new QPushButton(
        localizedText(QStringLiteral("自动检测"), QStringLiteral("Auto-detect")), paramCard);
    detectOffsetButton_->setCursor(Qt::PointingHandCursor);
    connect(detectOffsetButton_, &QPushButton::clicked, this, &LatencyDetectionPage::onDetectOffsetClicked);
    paramGrid->addWidget(detectOffsetButton_, 1, 2, Qt::AlignVCenter);
    offsetDetectResultLabel_ = new QLabel(QString(), paramCard);
    offsetDetectResultLabel_->setProperty("role", "detectResult");
    offsetDetectResultLabel_->setFont(resultFont);
    paramGrid->addWidget(offsetDetectResultLabel_, 1, 3, Qt::AlignLeft | Qt::AlignVCenter);

    // Row 2: clock_count (export count-in beats; a plain manual field)
    paramGrid->addWidget(
        makeRowLabel(QStringLiteral("clock_count")),
        2, 0, Qt::AlignLeft | Qt::AlignVCenter);
    clockCountEdit_ = new NoWheelSpinBox(paramCard);
    clockCountEdit_->setRange(1, 64);
    clockCountEdit_->setSingleStep(1);
    clockCountEdit_->setValue(4);
    clockCountEdit_->setMinimumWidth(110);
    clockCountEdit_->setKeyboardTracking(false);
    clockCountEdit_->setAccelerated(true);
    connect(clockCountEdit_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        if (clockCountDebounceTimer_ != nullptr) {
            clockCountDebounceTimer_->start();
        }
    });
    paramGrid->addWidget(clockCountEdit_, 2, 1, Qt::AlignVCenter);
    // clock_count is a plain manual field: no auto-detect button, no hint, and
    // BPM detection no longer writes it.

    paramLayout->addLayout(paramGrid);
    contentLayout->addWidget(paramCard);

    // -------- Audition (sandbox) card --------
    auto auditionPair = makeCard(
        localizedText(QStringLiteral("节奏校准试听"), QStringLiteral("Rhythm Calibration Audition")));
    auto* auditionCard = auditionPair.first;
    auto* auditionLayout = auditionPair.second;

    auto* subdivRow = new QHBoxLayout();
    subdivRow->setSpacing(16);
    auto* subdivLabel = new QLabel(
        localizedText(QStringLiteral("分音:"), QStringLiteral("Subdivision:")), auditionCard);
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
    sfxVolumeSlider_ = new NoWheelSlider(Qt::Horizontal, auditionCard);
    sfxVolumeSlider_->setRange(0, 100);
    sfxVolumeSlider_->setValue(kDefaultSfxVolumePercent);
    sfxVolumeSlider_->setMinimumWidth(120);
    connect(sfxVolumeSlider_, &QSlider::valueChanged, this, &LatencyDetectionPage::onSfxVolumeChanged);
    volumeRow->addWidget(sfxVolumeSlider_, 1);
    sfxVolumeValueLabel_ = new QLabel(QStringLiteral("%1%").arg(kDefaultSfxVolumePercent), auditionCard);
    sfxVolumeValueLabel_->setMinimumWidth(48);
    sfxVolumeValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sfxVolumeValueLabel_->setFont(hintFont);
    volumeRow->addWidget(sfxVolumeValueLabel_);
    auditionLayout->addLayout(volumeRow);

    // Reset sits on its own row, right-aligned, so it never collides with the
    // slider / percent readout on a narrow page.
    auto* volumeResetRow = new QHBoxLayout();
    volumeResetRow->setSpacing(10);
    volumeResetRow->addStretch(1);
    auto* volumeResetButton = new QPushButton(
        localizedText(QStringLiteral("重置音量"), QStringLiteral("Reset volume")), auditionCard);
    volumeResetButton->setCursor(Qt::PointingHandCursor);
    connect(volumeResetButton, &QPushButton::clicked, this, [this]() {
        if (sfxVolumeSlider_ != nullptr) {
            sfxVolumeSlider_->setValue(kDefaultSfxVolumePercent);
        }
    });
    volumeResetRow->addWidget(volumeResetButton);
    auditionLayout->addLayout(volumeResetRow);

    contentLayout->addWidget(auditionCard);
    contentLayout->addStretch(1);
}

void LatencyDetectionPage::onBpmEditValueChanged(double value)
{
    // Immediate live preview. keyboardTracking is off, so this only fires on a
    // settled value; the document write + undo entry are coalesced by the
    // debounce timer (and so are rapid step-button clicks).
    if (value > 0.0 && !sandbox_.isNull()) {
        sandbox_->setBpm(value);
    }
    if (bpmDebounceTimer_ != nullptr) {
        bpmDebounceTimer_->start();
    }
}

void LatencyDetectionPage::onOffsetEditValueChanged(double value)
{
    if (!sandbox_.isNull()) {
        sandbox_->setOffsetSeconds(value);
    }
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

void LatencyDetectionPage::commitClockCountEdit()
{
    if (owner_.isNull() || clockCountEdit_ == nullptr) {
        return;
    }
    const int value = clockCountEdit_->value();
    if (value <= 0) {
        return;
    }
    suppressDocumentRefresh_ = true;
    owner_->applyLatencyDetectorClockCount(value);
    suppressDocumentRefresh_ = false;
}

bool LatencyDetectionPage::eventFilter(QObject* watched, QEvent* event)
{
    // Application-level filter (installed on qApp) that routes Ctrl+S on this
    // page to our own save. We intercept here, before Qt's shortcut machinery,
    // so we don't collide with the global Save QAction (which would otherwise be
    // "ambiguous" → fire nothing). Scoped to: this page visible AND the key went
    // to a widget inside it (never hijacks other pages, dialogs, or the sidebar).
    //
    // NOTE (FAILED ATTEMPT, reverted): this filter also handled Ctrl+Z / Ctrl+Y
    // to drive a page-local BPM/offset undo history. That never worked reliably
    // (the keys did not consistently reach the page-local handler), so the
    // undo/redo branches and the param-history were removed. Ctrl+Z/Y now fall
    // through to the global Undo/Redo handler as on any other page.
    const QEvent::Type type = event->type();
    if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
        if (isVisible()) {
            auto* target = qobject_cast<QWidget*>(watched);
            if (target != nullptr && (target == this || isAncestorOf(target))) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->matches(QKeySequence::Save)) {
                    // Accept the ShortcutOverride so the global Save QAction does
                    // not also fire; act on the KeyPress.
                    if (type == QEvent::KeyPress && !owner_.isNull()) {
                        owner_->onSaveFile();
                    }
                    event->accept();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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
    // NOTE: no audition "tap" is played here on purpose — see the FAILED ATTEMPT
    // note in the constructor. The tap could not be kept silent during playback
    // (suspected stray play/audition event), so the slider only sets the volume.
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
    // Don't reset the position label here: pause keeps the playhead in place.
    // The controller emits playheadAdvanced() with the correct value (the
    // freeze point on pause, 0 on stop), which drives updatePositionLabel().
}

void LatencyDetectionPage::onPlayheadAdvanced(double seconds)
{
    updatePositionLabel(seconds);
}

void LatencyDetectionPage::updateAuditionUi(bool running)
{
    if (auditionButton_ != nullptr) {
        // Behaves like the main Play/Pause button: shows Pause while running,
        // reverts to the play label when paused or stopped.
        auditionButton_->setText(running
            ? localizedText(QStringLiteral("⏸ 暂停"), QStringLiteral("⏸ Pause"))
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

int LatencyDetectionPage::documentClockCount() const
{
    if (owner_.isNull()) {
        return 4;
    }
    return owner_->parsedClockCount();
}

}  // namespace miacode::latency
