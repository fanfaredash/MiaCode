#include "VideoExportDialog.h"

#include "PreviewCanvas.h"
#include "UiText.h"
#include "common/PreviewInteractionConfig.h"
#include "common/VideoExportConfig.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPolygonF>
#include <QProgressDialog>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace {

constexpr int kPreviewSliderScale = 1000;
constexpr int kPreviewTickIntervalMs = 33;
constexpr int kFormLabelWidth = 52;
constexpr int kRangeLabelWidth = 36;
constexpr int kFormRowSpacing = 3;
constexpr int kTimelineHorizontalInset = 12;
constexpr int kDialogMinWidth = 560;
constexpr int kSetButtonLeftGap = 12;
constexpr qreal kPreviewSeekInitialStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekInitialStepSeconds);
constexpr qreal kPreviewSeekMaxStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekMaxStepSeconds);
constexpr qreal kPreviewSeekLinearAccelerationSecondsPerMs =
    static_cast<qreal>(miacode::preview_interaction::kSeekLinearAccelerationSecondsPerMs);

struct ResolutionPreset {
    int width = 1024;
    int height = 1024;
    const char* label = "1024x1024 (1:1)";
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {512, 512, "512x512 (1:1)"},
    {768, 768, "768x768 (1:1)"},
    {1024, 1024, "1024x1024 (1:1)"},
    {640, 480, "640x480 (4:3)"},
    {800, 600, "800x600 (4:3)"},
    {1024, 768, "1024x768 (4:3)"},
    {1280, 960, "1280x960 (4:3)"},
    {960, 540, "960x540 (16:9)"},
    {1280, 720, "1280x720 (16:9)"},
    {1600, 900, "1600x900 (16:9)"},
    {1920, 1080, "1920x1080 (16:9)"},
};

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

int secondToSliderValue(double second)
{
    return qMax(0, qRound(second * kPreviewSliderScale));
}

double sliderValueToSecond(int sliderValue)
{
    return qMax(0.0, static_cast<double>(sliderValue) / kPreviewSliderScale);
}

QIcon makePreviewPlayIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(5.0, 3.0),
        QPointF(15.5, 10.0),
        QPointF(5.0, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makePreviewStopIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(4.5, 4.5, 11.0, 11.0), 1.8, 1.8);
    return QIcon(pixmap);
}

QIcon makePreviewPauseIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(5.0, 3.0, 3.5, 14.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(11.5, 3.0, 3.5, 14.0), 1.2, 1.2);
    return QIcon(pixmap);
}

class TimestampSpinBox : public QDoubleSpinBox
{
public:
    explicit TimestampSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setDecimals(3);
        setSingleStep(0.05);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignCenter);
        setKeyboardTracking(false);
        setFixedWidth(92);
    }

protected:
    QString textFromValue(double value) const override
    {
        const qint64 totalMs = qMax<qint64>(0, qRound64(value * 1000.0));
        const qint64 minutes = totalMs / 60000;
        const qint64 sec = (totalMs / 1000) % 60;
        const qint64 ms = totalMs % 1000;
        return QStringLiteral("%1:%2:%3")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0'))
            .arg(ms, 3, 10, QChar('0'));
    }

    double valueFromText(const QString& text) const override
    {
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3}):(\\d{2}):(\\d{3})\\s*$"));
        const QRegularExpressionMatch match = re.match(text);
        if (!match.hasMatch()) {
            return 0.0;
        }
        bool minOk = false;
        bool secOk = false;
        bool msOk = false;
        const int minutes = match.captured(1).toInt(&minOk);
        const int sec = match.captured(2).toInt(&secOk);
        const int ms = match.captured(3).toInt(&msOk);
        if (!minOk || !secOk || !msOk || sec < 0 || sec > 59 || ms < 0 || ms > 999) {
            return 0.0;
        }
        return static_cast<double>(minutes) * 60.0 + static_cast<double>(sec) + static_cast<double>(ms) / 1000.0;
    }

    QValidator::State validate(QString& text, int& pos) const override
    {
        Q_UNUSED(pos);
        static const QRegularExpression partial(QStringLiteral("^\\s*\\d{0,3}(:\\d{0,2}(:\\d{0,3})?)?\\s*$"));
        static const QRegularExpression full(QStringLiteral("^\\s*\\d{1,3}:\\d{2}:\\d{3}\\s*$"));
        if (full.match(text).hasMatch()) {
            return QValidator::Acceptable;
        }
        if (partial.match(text).hasMatch()) {
            return QValidator::Intermediate;
        }
        return QValidator::Invalid;
    }
};

}  // namespace

VideoExportDialog::VideoExportDialog(
    const VideoExportTask& baseTask,
    PreviewCanvas* sourceCanvas,
    SeekPreviewCallback seekPreviewCallback,
    PlayPreviewCallback playPreviewCallback,
    PausePreviewCallback pausePreviewCallback,
    IsPreviewPlayingCallback isPreviewPlayingCallback,
    CurrentPreviewSecondCallback currentPreviewSecondCallback,
    PreviewAspectRatioCallback previewAspectRatioCallback,
    QWidget* parent
)
    : QDialog(parent)
    , baseTask_(baseTask)
    , sourceCanvas_(sourceCanvas)
    , seekPreviewCallback_(std::move(seekPreviewCallback))
    , playPreviewCallback_(std::move(playPreviewCallback))
    , pausePreviewCallback_(std::move(pausePreviewCallback))
    , isPreviewPlayingCallback_(std::move(isPreviewPlayingCallback))
    , currentPreviewSecondCallback_(std::move(currentPreviewSecondCallback))
    , previewAspectRatioCallback_(std::move(previewAspectRatioCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")));
    setModal(true);
    setMinimumWidth(kDialogMinWidth);
    resize(680, 360);
    setStyleSheet(
        QStringLiteral(
            "QToolButton#RangePreviewControlButton {"
            " color: #223042;"
            " padding: 3px 5px;"
            " border: 1px solid #D8E0EA;"
            " border-radius: 6px;"
            " background: transparent;"
            "}"
            "QToolButton#RangePreviewControlButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
            "QToolButton#RangePreviewControlButton:pressed { background: #E8F1FB; }"
            "QToolButton#RangePreviewControlButton:disabled { color: #9AA6B5; border-color: #E0E7EF; }"
        )
    );

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(8);
    rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    auto* outputRow = new QWidget(this);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(kFormRowSpacing);
    auto* outputLabel = new QLabel(l10n(QStringLiteral("Output"), QStringLiteral("输出")), outputRow);
    outputLabel->setFixedWidth(kFormLabelWidth);
    outputPathEdit_ = new QLineEdit(outputRow);
    outputPathEdit_->setText(baseTask_.outputPath);
    auto* browseButton = new QPushButton(l10n(QStringLiteral("Browse..."), QStringLiteral("浏览...")), outputRow);
    const int rightAlignedButtonWidth = browseButton->sizeHint().width();
    browseButton->setFixedWidth(rightAlignedButtonWidth);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputLayout->addWidget(outputLabel, 0);
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton, 0);
    rootLayout->addWidget(outputRow, 0);

    auto* resolutionRow = new QWidget(this);
    auto* resolutionLayout = new QHBoxLayout(resolutionRow);
    resolutionLayout->setContentsMargins(0, 0, 0, 0);
    resolutionLayout->setSpacing(kFormRowSpacing);
    auto* resolutionLabel = new QLabel(l10n(QStringLiteral("Resolution"), QStringLiteral("分辨率")), resolutionRow);
    resolutionLabel->setFixedWidth(kFormLabelWidth);
    resolutionCombo_ = new QComboBox(resolutionRow);
    for (const ResolutionPreset& preset : kResolutionPresets) {
        resolutionCombo_->addItem(QString::fromLatin1(preset.label), QSize(preset.width, preset.height));
    }
    int currentPresetIndex = -1;
    for (int i = 0; i < resolutionCombo_->count(); ++i) {
        const QSize size = resolutionCombo_->itemData(i).toSize();
        if (size.width() == qMax(1, baseTask_.outputWidth)
            && size.height() == qMax(1, baseTask_.outputHeight)) {
            currentPresetIndex = i;
            break;
        }
    }
    resolutionCombo_->setCurrentIndex(currentPresetIndex >= 0 ? currentPresetIndex : 2);
    resolutionCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    resolutionCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    resolutionLayout->addWidget(resolutionLabel, 0);
    resolutionLayout->addWidget(resolutionCombo_, 1);
    auto* resolutionRightPlaceholder = new QWidget(resolutionRow);
    resolutionRightPlaceholder->setFixedWidth(rightAlignedButtonWidth);
    resolutionLayout->addWidget(resolutionRightPlaceholder, 0);
    rootLayout->addWidget(resolutionRow, 0);

    rangeContent_ = new QWidget(this);
    auto* rangeLayout = new QVBoxLayout(rangeContent_);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(6);

    startSecondSpin_ = new TimestampSpinBox(rangeContent_);
    startSecondSpin_->setRange(0.0, totalDurationSeconds_);
    endSecondSpin_ = new TimestampSpinBox(rangeContent_);
    endSecondSpin_->setRange(0.0, totalDurationSeconds_);

    const double defaultStart = qBound(0.0, baseTask_.exportStartSeconds, totalDurationSeconds_);
    const double defaultEnd = qBound(
        defaultStart,
        defaultStart + qMax(0.0, baseTask_.contentDurationSeconds),
        totalDurationSeconds_
    );
    startSecondSpin_->setValue(defaultStart);
    endSecondSpin_->setValue(defaultEnd);

    auto* startRow = new QWidget(rangeContent_);
    auto* startRowLayout = new QHBoxLayout(startRow);
    startRowLayout->setContentsMargins(0, 0, 0, 0);
    startRowLayout->setSpacing(kSetButtonLeftGap);
    auto* startLabel = new QLabel(l10n(QStringLiteral("Start"), QStringLiteral("起始")), startRow);
    startLabel->setFixedWidth(kRangeLabelWidth);
    startLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* setStartButton = new QPushButton(l10n(QStringLiteral("Set"), QStringLiteral("设定")), startRow);
    setStartButton->setFixedWidth(54);
    startRowLayout->addWidget(startLabel, 0);
    startRowLayout->addWidget(startSecondSpin_, 0, Qt::AlignLeft);
    startRowLayout->addWidget(setStartButton, 0, Qt::AlignLeft);
    startRowLayout->addStretch(1);
    rangeLayout->addWidget(startRow, 0);

    auto* endRow = new QWidget(rangeContent_);
    auto* endRowLayout = new QHBoxLayout(endRow);
    endRowLayout->setContentsMargins(0, 0, 0, 0);
    endRowLayout->setSpacing(kSetButtonLeftGap);
    auto* endLabel = new QLabel(l10n(QStringLiteral("End"), QStringLiteral("结束")), endRow);
    endLabel->setFixedWidth(kRangeLabelWidth);
    endLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* setEndButton = new QPushButton(l10n(QStringLiteral("Set"), QStringLiteral("设定")), endRow);
    setEndButton->setFixedWidth(54);
    endRowLayout->addWidget(endLabel, 0);
    endRowLayout->addWidget(endSecondSpin_, 0, Qt::AlignLeft);
    endRowLayout->addWidget(setEndButton, 0, Qt::AlignLeft);
    endRowLayout->addStretch(1);
    rangeLayout->addWidget(endRow, 0);

    const int rangeControlWidth =
        kRangeLabelWidth
        + kSetButtonLeftGap
        + startSecondSpin_->width()
        + kSetButtonLeftGap
        + setStartButton->width();

    previewSlider_ = new QSlider(Qt::Horizontal, rangeContent_);
    previewSlider_->setRange(0, secondToSliderValue(totalDurationSeconds_));
    previewCursorSecond_ = defaultStart;
    previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    previewSlider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* previewSliderRow = new QWidget(rangeContent_);
    auto* previewSliderLayout = new QHBoxLayout(previewSliderRow);
    const int sliderRightInset = kTimelineHorizontalInset + rightAlignedButtonWidth + kFormRowSpacing;
    previewSliderLayout->setContentsMargins(kTimelineHorizontalInset, 2, sliderRightInset, 0);
    previewSliderLayout->setSpacing(0);
    previewSliderLayout->addWidget(previewSlider_, 1);
    rangeLayout->addWidget(previewSliderRow, 0);

    previewTimeLabel_ = new QLabel(rangeContent_);
    previewTimeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    previewTimeLabel_->setFixedWidth(rangeControlWidth);
    auto* previewTimeRow = new QWidget(rangeContent_);
    auto* previewTimeLayout = new QHBoxLayout(previewTimeRow);
    previewTimeLayout->setContentsMargins(kTimelineHorizontalInset, 0, 0, 0);
    previewTimeLayout->setSpacing(0);
    previewTimeLayout->addWidget(previewTimeLabel_, 0, Qt::AlignLeft);
    previewTimeLayout->addStretch(1);
    rangeLayout->addWidget(previewTimeRow, 0);

    auto* previewButtonsRow = new QWidget(rangeContent_);
    auto* previewButtonsLayout = new QHBoxLayout(previewButtonsRow);
    previewButtonsLayout->setContentsMargins(kTimelineHorizontalInset, 2, 0, 2);
    previewButtonsLayout->setSpacing(6);
    stopPreviewButton_ = new QToolButton(previewButtonsRow);
    stopPreviewButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    stopPreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopPreviewButton_->setIconSize(QSize(16, 16));
    stopPreviewButton_->setFixedSize(QSize(32, 26));
    stopPreviewButton_->setIcon(makePreviewStopIcon(QColor("#2B3C4E")));
    stopPreviewButton_->setToolTip(l10n(QStringLiteral("Stop"), QStringLiteral("停止")));
    stopPreviewButton_->setAutoRaise(false);
    previewRangeButton_ = new QToolButton(previewButtonsRow);
    previewRangeButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    previewRangeButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    previewRangeButton_->setIconSize(QSize(16, 16));
    previewRangeButton_->setFixedSize(QSize(32, 26));
    previewRangeButton_->setAutoRaise(false);
    stopPreviewButton_->setEnabled(false);
    previewButtonsLayout->addWidget(stopPreviewButton_, 0);
    previewButtonsLayout->addWidget(previewRangeButton_, 0);
    previewButtonsLayout->addStretch(1);
    rangeLayout->addWidget(previewButtonsRow, 0);

    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Export Range"), QStringLiteral("导出区间")),
            rangeContent_,
            true,
            &rangeToggle_
        )
    );

    optionsContent_ = new QWidget(this);
    auto* optionsLayout = new QGridLayout(optionsContent_);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setHorizontalSpacing(10);
    optionsLayout->setVerticalSpacing(6);
    optionsLayout->setColumnStretch(0, 1);
    optionsLayout->setColumnStretch(1, 1);
    showTimestampCheck_ = new QCheckBox(
        l10n(QStringLiteral("Show bottom-left timestamp"), QStringLiteral("显示左下角时间戳")),
        optionsContent_
    );
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    optionsLayout->addWidget(showTimestampCheck_, 0, 0, 1, 1, Qt::AlignLeft);
    const auto addBrightnessOption = [this](QWidget* parent, const QString& title, int valuePercent, QSlider** sliderOut, QLabel** valueOut) {
        auto* container = new QWidget(parent);
        auto* containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(2);
        auto* header = new QWidget(container);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(6);
        auto* titleLabel = new QLabel(title, header);
        auto* valueLabel = new QLabel(QStringLiteral("%1%").arg(valuePercent), header);
        valueLabel->setMinimumWidth(40);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        headerLayout->addWidget(titleLabel, 1);
        headerLayout->addWidget(valueLabel, 0);
        auto* slider = new QSlider(Qt::Horizontal, container);
        slider->setRange(0, 100);
        slider->setValue(valuePercent);
        containerLayout->addWidget(header, 0);
        containerLayout->addWidget(slider, 0);
        *sliderOut = slider;
        *valueOut = valueLabel;
        return container;
    };
    QWidget* outerBrightnessOption = addBrightnessOption(
        optionsContent_,
        l10n(QStringLiteral("Brightness (Outer)"), QStringLiteral("Brightness (Outer)")),
        qRound(qBound(0.0, baseTask_.backgroundBrightnessOuter, 1.0) * 100.0),
        &brightnessOuterSlider_,
        &brightnessOuterValueLabel_
    );
    QWidget* innerBrightnessOption = addBrightnessOption(
        optionsContent_,
        l10n(QStringLiteral("Brightness (Inner)"), QStringLiteral("Brightness (Inner)")),
        qRound(qBound(0.0, baseTask_.backgroundBrightnessInner, 1.0) * 100.0),
        &brightnessInnerSlider_,
        &brightnessInnerValueLabel_
    );
    // Keep sliders on separate lines for readability.
    optionsLayout->addWidget(outerBrightnessOption, 1, 0, 1, 2);
    optionsLayout->addWidget(innerBrightnessOption, 2, 0, 1, 2);
    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Options"), QStringLiteral("选项")),
            optionsContent_,
            true,
            &optionsToggle_
        )
    );

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    exportButton_ = buttonBox->addButton(l10n(QStringLiteral("Export"), QStringLiteral("导出")), QDialogButtonBox::AcceptRole);
    connect(exportButton_, &QPushButton::clicked, this, &VideoExportDialog::startExport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(kPreviewTickIntervalMs);
    connect(previewTimer_, &QTimer::timeout, this, &VideoExportDialog::onRangePreviewTick);

    connect(startSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(endSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(previewSlider_, &QSlider::valueChanged, this, &VideoExportDialog::onPreviewSliderChanged);
    connect(setStartButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeStartFromPreview);
    connect(setEndButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeEndFromPreview);
    connect(previewRangeButton_, &QToolButton::clicked, this, &VideoExportDialog::toggleRangePreview);
    connect(stopPreviewButton_, &QToolButton::clicked, this, &VideoExportDialog::stopRangePreviewToLeadIn);
    connect(resolutionCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!previewAspectRatioCallback_) {
            return;
        }
        const QSize size = selectedResolution();
        if (size.width() <= 0 || size.height() <= 0) {
            return;
        }
        previewAspectRatioCallback_(static_cast<double>(size.width()) / static_cast<double>(size.height()));
    });
    connect(brightnessOuterSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessOuterValueLabel_ != nullptr) {
            brightnessOuterValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
    });
    connect(brightnessInnerSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessInnerValueLabel_ != nullptr) {
            brightnessInnerValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
    });
    previewSlider_->setFocusPolicy(Qt::StrongFocus);
    previewSlider_->installEventFilter(this);

    updatePreviewPlayPauseUi();
    syncRangeUi();
    seekPreview(previewCursorSecond_);
    if (previewAspectRatioCallback_) {
        const QSize size = selectedResolution();
        if (size.width() > 0 && size.height() > 0) {
            previewAspectRatioCallback_(static_cast<double>(size.width()) / static_cast<double>(size.height()));
        }
    }
    refreshDialogGeometry();
    QTimer::singleShot(0, this, [this]() {
        if (outputPathEdit_ != nullptr) {
            outputPathEdit_->clearFocus();
        }
        if (rangeToggle_ != nullptr) {
            rangeToggle_->setFocus(Qt::OtherFocusReason);
        }
        refreshDialogGeometry();
    });
}

QWidget* VideoExportDialog::buildCollapsibleSection(
    const QString& title,
    QWidget* content,
    bool expanded,
    QToolButton** toggleOut
)
{
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* toggle = new QToolButton(container);
    toggle->setText(title);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setCheckable(true);
    toggle->setChecked(expanded);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggle->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: 600; text-align: left; }"));

    layout->addWidget(toggle, 0);
    layout->addWidget(content, 0);
    updateSectionToggle(toggle, content, expanded);

    connect(toggle, &QToolButton::toggled, this, [this, toggle, content](bool checked) {
        updateSectionToggle(toggle, content, checked);
        refreshDialogGeometry();
        QTimer::singleShot(0, this, [this]() { refreshDialogGeometry(); });
    });
    if (toggleOut != nullptr) {
        *toggleOut = toggle;
    }
    return container;
}

void VideoExportDialog::updateSectionToggle(QToolButton* toggle, QWidget* content, bool expanded)
{
    if (toggle != nullptr) {
        toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    }
    if (content != nullptr) {
        content->setSizePolicy(QSizePolicy::Preferred, expanded ? QSizePolicy::Preferred : QSizePolicy::Ignored);
        content->setMinimumHeight(0);
        content->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
        content->setVisible(expanded);
    }
}

void VideoExportDialog::refreshDialogGeometry()
{
    if (QLayout* l = layout()) {
        l->invalidate();
        l->activate();
    }
    adjustSize();
    const int targetWidth = qMax(minimumWidth(), width());
    const int targetHeight = sizeHint().height();
    setMinimumHeight(targetHeight);
    setMaximumHeight(targetHeight);
    resize(targetWidth, targetHeight);
}

void VideoExportDialog::browseOutputPath()
{
    const QString initial = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")),
        initial,
        QStringLiteral("MP4 Video (*.mp4)")
    );
    if (selected.isEmpty() || outputPathEdit_ == nullptr) {
        return;
    }
    outputPathEdit_->setText(selected);
}

bool VideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        return false;
    }
    VideoExportTask updated = baseTask_;
    const QString outputPath = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Please choose an output path."), QStringLiteral("请先选择输出路径。"));
        }
        return false;
    }
    updated.outputPath = outputPath;
    const QSize selectedSize = selectedResolution();
    updated.outputWidth = selectedSize.width() > 0 ? selectedSize.width() : updated.outputWidth;
    updated.outputHeight = selectedSize.height() > 0 ? selectedSize.height() : updated.outputHeight;
    updated.fps = 60;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;
    updated.backgroundBrightnessOuter = brightnessOuterSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessOuter;
    updated.backgroundBrightnessInner = brightnessInnerSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessInner;
    updated.exportStartSeconds = rangeStartSeconds();
    updated.contentDurationSeconds = qMax(0.0, rangeEndSeconds() - updated.exportStartSeconds);

    const QFileInfo outputInfo(updated.outputPath);
    const QDir outputDir = outputInfo.absoluteDir();
    if (!outputDir.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Output directory does not exist."), QStringLiteral("输出目录不存在。"));
        }
        return false;
    }
    if (updated.outputWidth <= 0 || updated.outputHeight <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Resolution is invalid."), QStringLiteral("分辨率无效。"));
        }
        return false;
    }
    if (updated.contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Export range is empty."), QStringLiteral("导出区间为空。"));
        }
        return false;
    }
    if (updated.exportStartSeconds < 0.0 || updated.exportStartSeconds > totalDurationSeconds_ + 1e-6) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Export start is out of range."), QStringLiteral("导出起始时间超出范围。"));
        }
        return false;
    }
    *task = updated;
    return true;
}

QSize VideoExportDialog::selectedResolution() const
{
    if (resolutionCombo_ == nullptr) {
        return QSize(qMax(1, baseTask_.outputWidth), qMax(1, baseTask_.outputHeight));
    }
    const QSize selected = resolutionCombo_->currentData().toSize();
    if (selected.width() > 0 && selected.height() > 0) {
        return selected;
    }
    return QSize(qMax(1, baseTask_.outputWidth), qMax(1, baseTask_.outputHeight));
}

void VideoExportDialog::onRangeSpinChanged()
{
    if (syncingRangeUi_) {
        return;
    }
    const double start = rangeStartSeconds();
    const double end = rangeEndSeconds();
    if (end <= start && totalDurationSeconds_ > 0.0) {
        const bool fromStart = sender() == startSecondSpin_;
        syncingRangeUi_ = true;
        if (fromStart && endSecondSpin_ != nullptr) {
            endSecondSpin_->setValue(qMin(totalDurationSeconds_, start + 0.001));
        } else if (startSecondSpin_ != nullptr) {
            startSecondSpin_->setValue(qMax(0.0, end - 0.001));
        }
        syncingRangeUi_ = false;
    }
    syncRangeUi();
}

void VideoExportDialog::onPreviewSliderChanged(int sliderValue)
{
    if (syncingRangeUi_) {
        return;
    }
    previewCursorSecond_ = qBound(0.0, sliderValueToSecond(sliderValue), totalDurationSeconds_);
    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::setRangeStartFromPreview()
{
    if (startSecondSpin_ == nullptr) {
        return;
    }
    startSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::setRangeEndFromPreview()
{
    if (endSecondSpin_ == nullptr) {
        return;
    }
    endSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::toggleRangePreview()
{
    if (rangePreviewPlaying_) {
        stopRangePreview(false);
        return;
    }
    if (rangeEndSeconds() <= rangeStartSeconds()) {
        return;
    }

    double startSecond = previewCursorSecond_;
    const double leadInStart = leadInStartSeconds();
    if (startSecond < leadInStart || startSecond > rangeEndSeconds()) {
        startSecond = leadInStart;
    }
    playPreview(startSecond);
    rangePreviewPlaying_ = true;
    updatePreviewPlayPauseUi();
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(true);
    }
    if (previewTimer_ != nullptr && !previewTimer_->isActive()) {
        previewTimer_->start();
    }
}

void VideoExportDialog::stopRangePreview(bool seekToCurrent)
{
    if (previewTimer_ != nullptr) {
        previewTimer_->stop();
    }
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        pausePreview();
        previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    }
    rangePreviewPlaying_ = false;
    updatePreviewPlayPauseUi();
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(previewCursorSecond_ > leadInStartSeconds() + 0.0005);
    }
    if (seekToCurrent) {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::stopRangePreviewToLeadIn()
{
    stopRangePreview(false);
    previewCursorSecond_ = qBound(0.0, leadInStartSeconds(), totalDurationSeconds_);
    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::updatePreviewPlayPauseUi()
{
    if (previewRangeButton_ == nullptr) {
        return;
    }
    if (rangePreviewPlaying_) {
        previewRangeButton_->setIcon(makePreviewPauseIcon(QColor("#2B3C4E")));
        previewRangeButton_->setToolTip(l10n(QStringLiteral("Pause"), QStringLiteral("暂停")));
        previewRangeButton_->setStyleSheet(
            QStringLiteral(
                "QToolButton { color: #2B3C4E; border: 1px solid #2E77D0; border-radius: 6px; background: #2E77D0; }"
                "QToolButton:hover { background: #3A86E8; }"
            )
        );
    } else {
        previewRangeButton_->setIcon(makePreviewPlayIcon(QColor("#2B3C4E")));
        previewRangeButton_->setToolTip(l10n(QStringLiteral("Play"), QStringLiteral("播放")));
        previewRangeButton_->setStyleSheet(QString());
    }
}

void VideoExportDialog::onRangePreviewTick()
{
    if (!rangePreviewPlaying_) {
        return;
    }
    if (!isPreviewPlaying()) {
        stopRangePreview(false);
        return;
    }

    previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    if (previewCursorSecond_ >= rangeEndSeconds()) {
        previewCursorSecond_ = rangeEndSeconds();
        stopRangePreview(false);
        seekPreview(previewCursorSecond_);
        syncRangeUi();
        return;
    }
    syncRangeUi();
}

void VideoExportDialog::syncRangeUi()
{
    if (syncingRangeUi_) {
        return;
    }
    syncingRangeUi_ = true;

    const double clampedStart = qBound(0.0, rangeStartSeconds(), totalDurationSeconds_);
    const double clampedEnd = qBound(clampedStart, rangeEndSeconds(), totalDurationSeconds_);
    previewCursorSecond_ = qBound(0.0, previewCursorSecond_, totalDurationSeconds_);

    if (startSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*startSecondSpin_);
        startSecondSpin_->setValue(clampedStart);
    }
    if (endSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*endSecondSpin_);
        endSecondSpin_->setValue(clampedEnd);
    }
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    }
    if (previewTimeLabel_ != nullptr) {
        previewTimeLabel_->setText(QStringLiteral("%1 / %2").arg(formatSecond(previewCursorSecond_), formatSecond(totalDurationSeconds_)));
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(rangePreviewPlaying_ || previewCursorSecond_ > leadInStartSeconds() + 0.0005);
    }

    syncingRangeUi_ = false;
}

void VideoExportDialog::seekPreview(double second)
{
    const double clamped = qBound(0.0, second, totalDurationSeconds_);
    previewCursorSecond_ = clamped;
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(clamped));
    }
    if (seekPreviewCallback_) {
        seekPreviewCallback_(clamped);
        return;
    }
    if (sourceCanvas_ != nullptr) {
        sourceCanvas_->setPlayheadSeconds(clamped);
        sourceCanvas_->update();
    }
}

void VideoExportDialog::playPreview(double second)
{
    previewCursorSecond_ = qBound(0.0, second, totalDurationSeconds_);
    if (playPreviewCallback_) {
        playPreviewCallback_(previewCursorSecond_);
        return;
    }
    seekPreview(previewCursorSecond_);
}

void VideoExportDialog::pausePreview()
{
    if (pausePreviewCallback_) {
        pausePreviewCallback_();
    }
}

bool VideoExportDialog::isPreviewPlaying() const
{
    if (isPreviewPlayingCallback_) {
        return isPreviewPlayingCallback_();
    }
    return false;
}

double VideoExportDialog::currentPreviewSecond() const
{
    if (currentPreviewSecondCallback_) {
        return currentPreviewSecondCallback_();
    }
    return previewCursorSecond_;
}

double VideoExportDialog::rangeStartSeconds() const
{
    return startSecondSpin_ != nullptr ? startSecondSpin_->value() : 0.0;
}

double VideoExportDialog::rangeEndSeconds() const
{
    return endSecondSpin_ != nullptr ? endSecondSpin_->value() : totalDurationSeconds_;
}

double VideoExportDialog::leadInStartSeconds() const
{
    return qMax(0.0, rangeStartSeconds() - miacode::video_export::kLeadInSeconds);
}

QString VideoExportDialog::formatSecond(double second) const
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(second * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

void VideoExportDialog::startExport()
{
    stopRangePreview(false);

    VideoExportTask task;
    QString errorMessage;
    if (!applyUiToTask(&task, &errorMessage)) {
        QMessageBox::warning(this, l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")), errorMessage);
        return;
    }
    if (sourceCanvas_ == nullptr) {
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")),
            l10n(QStringLiteral("Preview canvas is not initialized."), QStringLiteral("预览画布未初始化。"))
        );
        return;
    }

    QProgressDialog progress(
        l10n(QStringLiteral("Preparing export..."), QStringLiteral("正在准备导出...")),
        l10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        this
    );
    progress.setWindowTitle(l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    exportButton_->setEnabled(false);
    VideoExportResult result = VideoExportController::exportFullPreview(task, sourceCanvas_, &progress);
    exportButton_->setEnabled(true);

    if (!result.success) {
        if (result.message == QLatin1String("canceled")) {
            QMessageBox::information(
                this,
                l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")),
                l10n(QStringLiteral("Export canceled."), QStringLiteral("导出已取消。"))
            );
            return;
        }
        const QString details = result.details.trimmed();
        QMessageBox::critical(
            this,
            l10n(QStringLiteral("Export Failed"), QStringLiteral("导出失败")),
            details.isEmpty() ? result.message : QStringLiteral("%1\n\n%2").arg(result.message, details)
        );
        return;
    }

    QMessageBox::information(
        this,
        l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")),
        l10n(QStringLiteral("Export completed."), QStringLiteral("导出完成。"))
    );
    exportSucceeded_ = true;
    accept();
}

void VideoExportDialog::closeEvent(QCloseEvent* event)
{
    stopRangePreview(false);
    QDialog::closeEvent(event);
}

bool VideoExportDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (!keyEvent->isAutoRepeat() || previewSeekHeldArrowKey_ != keyEvent->key()) {
                    previewSeekHeldArrowKey_ = keyEvent->key();
                    previewSeekHeldArrowElapsed_.restart();
                } else if (!previewSeekHeldArrowElapsed_.isValid()) {
                    previewSeekHeldArrowElapsed_.restart();
                }

                const qreal heldMs = previewSeekHeldArrowElapsed_.isValid()
                    ? static_cast<qreal>(previewSeekHeldArrowElapsed_.elapsed())
                    : 0.0;
                const qreal acceleratedStep = qMin(
                    kPreviewSeekMaxStepSeconds,
                    kPreviewSeekInitialStepSeconds + (heldMs * kPreviewSeekLinearAccelerationSecondsPerMs)
                );
                const int deltaMs = direction * qRound(acceleratedStep * 1000.0);
                const int value = qBound(
                    previewSlider_->minimum(),
                    previewSlider_->value() + deltaMs,
                    previewSlider_->maximum()
                );
                previewSlider_->setValue(value);
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                previewSeekHeldArrowKey_ = 0;
                previewSeekHeldArrowElapsed_.invalidate();
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
