#include "VideoExportDialog.h"

#include "DialogLocalization.h"
#include "PreviewCanvas.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/PreviewInteractionConfig.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <limits>
#include <utility>

namespace {

constexpr int kPreviewSliderScale = 1000;
constexpr int kPreviewTickIntervalMs = 33;
constexpr int kFormLabelWidth = 52;
constexpr int kRangeLabelWidth = 36;
constexpr int kFormRowSpacing = 3;
constexpr int kSectionContentLeftInset = 12;
constexpr int kDialogMinWidth = 560;
constexpr int kSetButtonLeftGap = 12;
constexpr int kPreviewControlButtonWidth = 32;
constexpr int kPreviewControlButtonHeight = 26;
constexpr int kPreviewControlSpacing = 6;
constexpr int kPreviewControlsToSliderGap = 8;
constexpr int kDialogActionButtonMinWidth = 92;
constexpr int kRangeSetButtonWidth = 72;
constexpr int kPreviewScrubRenderIntervalMs = 33;
constexpr qreal kPreviewSeekInitialStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekInitialStepSeconds);
constexpr qreal kPreviewSeekMaxStepSeconds = static_cast<qreal>(miacode::preview_interaction::kSeekMaxStepSeconds);
constexpr qreal kPreviewSeekLinearAccelerationSecondsPerMs =
    static_cast<qreal>(miacode::preview_interaction::kSeekLinearAccelerationSecondsPerMs);

struct ResolutionPreset {
    int width = 1080;
    int height = 1080;
    const char* label = "1080x1080 (1:1)";
    double aspectRatio = 1.0;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {720, 720, "720x720 (1:1)", 1.0},
    {1024, 1024, "1024x1024 (1:1)", 1.0},
    {960, 720, "960x720 (4:3)", 4.0 / 3.0},
    {1280, 720, "1280x720 (16:9)", 16.0 / 9.0},
    {1080, 1080, "1080x1080 (1:1)", 1.0},
    {1440, 1080, "1440x1080 (4:3)", 4.0 / 3.0},
    {1920, 1080, "1920x1080 (16:9)", 16.0 / 9.0},
    {1440, 1440, "1440x1440 (1:1)", 1.0},
    {1920, 1440, "1920x1440 (4:3)", 4.0 / 3.0},
    {2560, 1440, "2560x1440 (16:9)", 16.0 / 9.0},
};

constexpr int kFpsOptions[] = {60, 120};

QString uiText(const char* key, const QString& fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? fallback : translated;
}

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

int secondToSliderValue(double second)
{
    return qMax(0, qRound(second * kPreviewSliderScale));
}

QString exportBaseDirectory(const VideoExportTask& task)
{
    const QFileInfo chartInfo(task.chartPath);
    if (!chartInfo.absoluteDir().path().isEmpty()) {
        return chartInfo.absoluteDir().absolutePath();
    }
    return QDir::currentPath();
}

QString displayOutputPathForDialog(const QString& outputPath, const QString& baseDirectory)
{
    if (outputPath.trimmed().isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(outputPath);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(outputPath)
        : outputInfo.absoluteFilePath();
    return QDir::toNativeSeparators(QDir(baseDirectory).relativeFilePath(QDir::cleanPath(absolutePath)));
}

QString resolveOutputPathForExport(const QString& outputPath, const QString& baseDirectory)
{
    const QString trimmed = QDir::fromNativeSeparators(outputPath.trimmed());
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(trimmed);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(trimmed)
        : outputInfo.absoluteFilePath();
    return QDir::cleanPath(absolutePath);
}

QToolButton* createDialogMenuButton(QWidget* parent, const QString& text, int minimumWidth = 0)
{
    auto* button = new QToolButton(parent);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    button->setText(text);
    if (minimumWidth > 0) {
        button->setMinimumWidth(minimumWidth);
    }
    return button;
}

QWidgetAction* addDialogMenuChoice(
    QMenu* menu,
    const QString& text,
    const std::function<void()>& onTriggered
)
{
    auto* action = new QWidgetAction(menu);
    auto* button = new QToolButton(menu);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setText(text);
    button->setCursor(Qt::PointingHandCursor);
    const auto& c = UiTheme::colors();
    button->setStyleSheet(
        QStringLiteral(
            "QToolButton {"
            " color: %1;"
            " background: transparent;"
            " border: none;"
            " padding: 6px 20px 6px 12px;"
            " text-align: left;"
            "}"
            "QToolButton:hover {"
            " background: %2;"
            " border-radius: 6px;"
            "}"
        )
            .arg(c.textPrimary.name(QColor::HexRgb))
            .arg(c.menuHoverBg.name(QColor::HexRgb))
    );
    QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
        if (onTriggered) {
            onTriggered();
        }
        action->trigger();
        menu->close();
    });
    action->setDefaultWidget(button);
    menu->addAction(action);
    return action;
}

QPoint desiredDialogTopLeft(QWidget* owner, const QSize& dialogSize)
{
    if (owner == nullptr) {
        return QPoint();
    }
    const QRect ownerFrame = owner->frameGeometry();
    int targetCenterX = ownerFrame.center().x();
    QRect anchorRect;
    bool hasAnchor = false;
    const auto mergeGlobalRect = [&](QWidget* widget) {
        if (widget == nullptr || !widget->isVisible()) {
            return;
        }
        const QRect rect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
        if (!hasAnchor) {
            anchorRect = rect;
            hasAnchor = true;
        } else {
            anchorRect = anchorRect.united(rect);
        }
    };
    if (QDockWidget* outlineDock = owner->findChild<QDockWidget*>(QStringLiteral("OutlineDock")); outlineDock != nullptr) {
        mergeGlobalRect(outlineDock);
    }
    if (QWidget* previewPanel = owner->findChild<QWidget*>(QStringLiteral("PreviewPanel")); previewPanel != nullptr) {
        if (QSplitter* splitter = qobject_cast<QSplitter*>(previewPanel->parentWidget()); splitter != nullptr && splitter->count() > 0) {
            mergeGlobalRect(splitter->widget(0));
        }
    }
    if (hasAnchor) {
        targetCenterX = anchorRect.center().x();
    }
    const int targetCenterY = ownerFrame.top() + (ownerFrame.height() / 3);
    return QPoint(targetCenterX - (dialogSize.width() / 2), targetCenterY - (dialogSize.height() / 2));
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
        setMinimumHeight(28);
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
    PreviewBrightnessCallback previewBrightnessCallback,
    PreviewLayoutScaleCallback previewLayoutScaleCallback,
    PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback,
    PreviewScaleModeCallback previewScaleModeCallback,
    PreviewFlowSpeedCallback previewFlowSpeedCallback,
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
    , previewBrightnessCallback_(std::move(previewBrightnessCallback))
    , previewLayoutScaleCallback_(std::move(previewLayoutScaleCallback))
    , previewSmoothBrightnessCallback_(std::move(previewSmoothBrightnessCallback))
    , previewScaleModeCallback_(std::move(previewScaleModeCallback))
    , previewFlowSpeedCallback_(std::move(previewFlowSpeedCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(uiText("dialog.video_export.title", QStringLiteral("Export Video")));
    setModal(true);
    setMinimumWidth(kDialogMinWidth);
    resize(680, 360);
    setStyleSheet(UiTheme::exportDialogStyleSheet());
    if (sourceCanvas_ != nullptr) {
        initialShowTimestamp_ = sourceCanvas_->showTimestamp();
        initialShowObjectStatsHud_ = sourceCanvas_->showObjectStatsHud();
    }

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(8);
    rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    auto* primaryPanel = new QFrame(this);
    primaryPanel->setObjectName(QStringLiteral("VideoExportPrimaryPanel"));
    auto* primaryPanelLayout = new QVBoxLayout(primaryPanel);
    primaryPanelLayout->setContentsMargins(10, 10, 10, 10);
    primaryPanelLayout->setSpacing(8);
    rootLayout->addWidget(primaryPanel, 0);

    auto* outputRow = new QWidget(primaryPanel);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    outputLayout->setSpacing(kFormRowSpacing);
    auto* outputLabel = new QLabel(l10n(QStringLiteral("Output"), QStringLiteral("输出")), outputRow);
    outputLabel->setFixedWidth(kFormLabelWidth);
    outputLabel->setText(uiText("dialog.video_export.output", QStringLiteral("Output")));
    outputPathEdit_ = new QLineEdit(outputRow);
    outputPathEdit_->setText(displayOutputPathForDialog(baseTask_.outputPath, exportBaseDirectory(baseTask_)));
    auto* browseButton = new QPushButton(l10n(QStringLiteral("Browse..."), QStringLiteral("浏览...")), outputRow);
    browseButton->setText(uiText("dialog.video_export.browse", QStringLiteral("Browse...")));
    browseButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    const int rightAlignedButtonWidth = qMax(browseButton->sizeHint().width(), kDialogActionButtonMinWidth);
    browseButton->setFixedWidth(rightAlignedButtonWidth);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputLayout->addWidget(outputLabel, 0);
    outputLayout->addWidget(outputPathEdit_, 1);
    outputLayout->addWidget(browseButton, 0);
    primaryPanelLayout->addWidget(outputRow, 0);
    auto* resolutionRow = new QWidget(primaryPanel);
    auto* resolutionLayout = new QHBoxLayout(resolutionRow);
    resolutionLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    resolutionLayout->setSpacing(kFormRowSpacing);
    auto* resolutionLabel = new QLabel(l10n(QStringLiteral("Resolution"), QStringLiteral("分辨率")), resolutionRow);
    resolutionLabel->setFixedWidth(kFormLabelWidth);
    resolutionLabel->setText(uiText("dialog.video_export.resolution", QStringLiteral("Resolution")));
    int currentPresetIndex = -1;
    for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
        const QSize size(kResolutionPresets[i].width, kResolutionPresets[i].height);
        if (size.width() == qMax(1, baseTask_.outputWidth)
            && size.height() == qMax(1, baseTask_.outputHeight)) {
            currentPresetIndex = i;
            break;
        }
    }
    if (currentPresetIndex < 0) {
        const double currentAspect =
            static_cast<double>(qMax(1, baseTask_.outputWidth)) / static_cast<double>(qMax(1, baseTask_.outputHeight));
        double bestScore = (std::numeric_limits<double>::max)();
        for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
            const QSize size(kResolutionPresets[i].width, kResolutionPresets[i].height);
            const double optionAspect = size.height() > 0
                ? static_cast<double>(size.width()) / static_cast<double>(size.height())
                : 1.0;
            const double aspectPenalty = qAbs(optionAspect - currentAspect) * 1000.0;
            const double areaPenalty = qAbs(static_cast<double>(size.width() * size.height()) - (1920.0 * 1080.0));
            const double score = aspectPenalty + areaPenalty;
            if (score < bestScore) {
                bestScore = score;
                currentPresetIndex = i;
            }
        }
    }
    currentPresetIndex = currentPresetIndex >= 0 ? currentPresetIndex : 0;
    selectedResolution_ = QSize(kResolutionPresets[currentPresetIndex].width, kResolutionPresets[currentPresetIndex].height);
    resolutionButton_ = createDialogMenuButton(
        resolutionRow,
        QString::fromLatin1(kResolutionPresets[currentPresetIndex].label)
    );
    resolutionMenu_ = new QMenu(resolutionButton_);
    UiTheme::styleRoundedMenu(*resolutionMenu_);
    for (const ResolutionPreset& preset : kResolutionPresets) {
        const QSize size(preset.width, preset.height);
        const QString label = QString::fromLatin1(preset.label);
        addDialogMenuChoice(resolutionMenu_, label, [this, size, label]() {
            selectedResolution_ = size;
            if (resolutionButton_ != nullptr) {
                resolutionButton_->setText(label);
            }
            applySelectedAspectRatioToPreview(true);
        });
    }
    resolutionButton_->setMenu(resolutionMenu_);
    resolutionLayout->addWidget(resolutionLabel, 0);
    resolutionLayout->addWidget(resolutionButton_, 1);
    auto* resolutionRightPlaceholder = new QWidget(resolutionRow);
    resolutionRightPlaceholder->setFixedWidth(rightAlignedButtonWidth);
    resolutionLayout->addWidget(resolutionRightPlaceholder, 0);
    primaryPanelLayout->addWidget(resolutionRow, 0);

    selectedFps_ = baseTask_.fps >= 90 ? 120 : 60;
    fpsButton_ = createDialogMenuButton(optionsContent_, QStringLiteral("%1 FPS").arg(selectedFps_));
    fpsMenu_ = new QMenu(fpsButton_);
    UiTheme::styleRoundedMenu(*fpsMenu_);
    for (int fps : kFpsOptions) {
        const QString label = QStringLiteral("%1 FPS").arg(fps);
        addDialogMenuChoice(fpsMenu_, label, [this, fps, label]() {
            selectedFps_ = fps;
            if (fpsButton_ != nullptr) {
                fpsButton_->setText(label);
            }
        });
    }
    fpsButton_->setMenu(fpsMenu_);
    auto* fpsRow = new QWidget(primaryPanel);
    auto* fpsLayout = new QHBoxLayout(fpsRow);
    fpsLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    fpsLayout->setSpacing(kFormRowSpacing);
    auto* fpsLabel = new QLabel(uiText("dialog.video_export.fps", QStringLiteral("FPS")), fpsRow);
    fpsLabel->setFixedWidth(kFormLabelWidth);
    fpsLayout->addWidget(fpsLabel, 0);
    fpsLayout->addWidget(fpsButton_, 1);
    auto* fpsRightPlaceholder = new QWidget(fpsRow);
    fpsRightPlaceholder->setFixedWidth(rightAlignedButtonWidth);
    fpsLayout->addWidget(fpsRightPlaceholder, 0);
    primaryPanelLayout->addWidget(fpsRow, 0);

    selectedPerformanceProfile_ = baseTask_.performanceProfile;
    const auto performanceProfileLabel = [](VideoExportPerformanceProfile profile) {
        switch (profile) {
        case VideoExportPerformanceProfile::Speed:
            return uiText("dialog.video_export.performance.speed", QStringLiteral("Speed First"));
        case VideoExportPerformanceProfile::Balanced:
        default:
            return uiText("dialog.video_export.performance.balanced", QStringLiteral("Balanced"));
        }
    };
    performanceButton_ = createDialogMenuButton(primaryPanel, performanceProfileLabel(selectedPerformanceProfile_));
    performanceMenu_ = new QMenu(performanceButton_);
    UiTheme::styleRoundedMenu(*performanceMenu_);
    addDialogMenuChoice(
        performanceMenu_,
        uiText("dialog.video_export.performance.balanced", QStringLiteral("Balanced")),
        [this, performanceProfileLabel]() {
            selectedPerformanceProfile_ = VideoExportPerformanceProfile::Balanced;
            if (performanceButton_ != nullptr) {
                performanceButton_->setText(performanceProfileLabel(selectedPerformanceProfile_));
            }
        }
    );
    addDialogMenuChoice(
        performanceMenu_,
        uiText("dialog.video_export.performance.speed", QStringLiteral("Speed First")),
        [this, performanceProfileLabel]() {
            selectedPerformanceProfile_ = VideoExportPerformanceProfile::Speed;
            if (performanceButton_ != nullptr) {
                performanceButton_->setText(performanceProfileLabel(selectedPerformanceProfile_));
            }
        }
    );
    performanceButton_->setMenu(performanceMenu_);
    auto* performanceRow = new QWidget(primaryPanel);
    auto* performanceLayout = new QHBoxLayout(performanceRow);
    performanceLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    performanceLayout->setSpacing(kFormRowSpacing);
    auto* performanceLabel = new QLabel(
        uiText("dialog.video_export.performance", QStringLiteral("Speed")),
        performanceRow
    );
    performanceLabel->setFixedWidth(kFormLabelWidth);
    performanceLayout->addWidget(performanceLabel, 0);
    performanceLayout->addWidget(performanceButton_, 1);
    auto* performanceRightPlaceholder = new QWidget(performanceRow);
    performanceRightPlaceholder->setFixedWidth(rightAlignedButtonWidth);
    performanceLayout->addWidget(performanceRightPlaceholder, 0);
    primaryPanelLayout->addWidget(performanceRow, 0);

    rangeContent_ = new QWidget(this);
    auto* rangeLayout = new QVBoxLayout(rangeContent_);
    rangeLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
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
    startLabel->setText(uiText("dialog.video_export.range.start", QStringLiteral("Start")));
    startLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setStartButton = new QPushButton(l10n(QStringLiteral("Set Start"), QStringLiteral("设定起始")), startRow);
    setStartButton->setFixedWidth(kRangeSetButtonWidth);
    setStartButton->setText(uiText("dialog.video_export.range.set_left", QStringLiteral("<- Set")));
    setStartButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    startCurrentTimeEdit_ = new QLineEdit(startRow);
    startCurrentTimeEdit_->setReadOnly(true);
    startCurrentTimeEdit_->setFocusPolicy(Qt::NoFocus);
    startCurrentTimeEdit_->setMinimumWidth(108);
    startCurrentTimeEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    startCurrentTimeEdit_->setMinimumHeight(startSecondSpin_->sizeHint().height() * 2 + rangeLayout->spacing());
    startCurrentTimeEdit_->setAlignment(Qt::AlignCenter);
    startCurrentTimeEdit_->setStyleSheet(UiTheme::readOnlyLineEditStyleSheet());
    startRowLayout->addWidget(startLabel, 0);
    startRowLayout->addWidget(startSecondSpin_, 0, Qt::AlignLeft);
    startRowLayout->addWidget(setStartButton, 0, Qt::AlignLeft);
    startRowLayout->addWidget(startCurrentTimeEdit_, 0, Qt::AlignLeft);
    startRowLayout->addStretch(1);
    rangeLayout->addWidget(startRow, 0);

    auto* endRow = new QWidget(rangeContent_);
    auto* endRowLayout = new QHBoxLayout(endRow);
    endRowLayout->setContentsMargins(0, 0, 0, 0);
    endRowLayout->setSpacing(kSetButtonLeftGap);
    auto* endLabel = new QLabel(l10n(QStringLiteral("End"), QStringLiteral("结束")), endRow);
    endLabel->setFixedWidth(kRangeLabelWidth);
    endLabel->setText(uiText("dialog.video_export.range.end", QStringLiteral("End")));
    endLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setEndButton = new QPushButton(l10n(QStringLiteral("Set End"), QStringLiteral("设定结束")), endRow);
    setEndButton->setFixedWidth(kRangeSetButtonWidth);
    setEndButton->setText(uiText("dialog.video_export.range.set_left", QStringLiteral("<- Set")));
    setEndButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    endCurrentTimeEdit_ = new QLineEdit(endRow);
    endCurrentTimeEdit_->setReadOnly(true);
    endCurrentTimeEdit_->setFocusPolicy(Qt::NoFocus);
    endCurrentTimeEdit_->setMinimumWidth(108);
    endCurrentTimeEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    endCurrentTimeEdit_->setAlignment(Qt::AlignCenter);
    endCurrentTimeEdit_->setStyleSheet(UiTheme::readOnlyLineEditStyleSheet());
    endCurrentTimeEdit_->hide();
    endRowLayout->addWidget(endLabel, 0);
    endRowLayout->addWidget(endSecondSpin_, 0, Qt::AlignLeft);
    endRowLayout->addWidget(setEndButton, 0, Qt::AlignLeft);
    endRowLayout->addWidget(endCurrentTimeEdit_, 0, Qt::AlignLeft);
    endRowLayout->addStretch(1);
    rangeLayout->addWidget(endRow, 0);
    startRow->hide();
    endRow->hide();

    auto* mergedRangeRows = new QWidget(rangeContent_);
    auto* mergedRangeLayout = new QGridLayout(mergedRangeRows);
    mergedRangeLayout->setContentsMargins(0, 0, 0, 0);
    mergedRangeLayout->setHorizontalSpacing(kSetButtonLeftGap);
    mergedRangeLayout->setVerticalSpacing(rangeLayout->spacing());
    mergedRangeLayout->setColumnStretch(3, 1);
    startLabel->setParent(mergedRangeRows);
    startSecondSpin_->setParent(mergedRangeRows);
    setStartButton->setParent(mergedRangeRows);
    startCurrentTimeEdit_->setParent(mergedRangeRows);
    endLabel->setParent(mergedRangeRows);
    endSecondSpin_->setParent(mergedRangeRows);
    setEndButton->setParent(mergedRangeRows);
    startCurrentTimeEdit_->setMinimumHeight(startSecondSpin_->sizeHint().height() * 2 + mergedRangeLayout->verticalSpacing() + 2);
    startCurrentTimeEdit_->show();
    endCurrentTimeEdit_->hide();
    mergedRangeLayout->addWidget(startLabel, 0, 0);
    mergedRangeLayout->addWidget(startSecondSpin_, 0, 1, Qt::AlignLeft);
    mergedRangeLayout->addWidget(setStartButton, 0, 2, Qt::AlignLeft);
    mergedRangeLayout->addWidget(startCurrentTimeEdit_, 0, 3, 2, 1, Qt::AlignLeft | Qt::AlignVCenter);
    mergedRangeLayout->addWidget(endLabel, 1, 0);
    mergedRangeLayout->addWidget(endSecondSpin_, 1, 1, Qt::AlignLeft);
    mergedRangeLayout->addWidget(setEndButton, 1, 2, Qt::AlignLeft);
    rangeLayout->addWidget(mergedRangeRows, 0);

    const int rangeControlWidth =
        kRangeLabelWidth
        + kSetButtonLeftGap
        + startSecondSpin_->width()
        + kSetButtonLeftGap
        + setStartButton->width()
        + kSetButtonLeftGap
        + startCurrentTimeEdit_->minimumWidth();

    previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    previewSlider_ = new QSlider(Qt::Horizontal, this);
    previewSlider_->setRange(0, secondToSliderValue(totalDurationSeconds_));
    previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    previewSlider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    previewSlider_->setStyleSheet(UiTheme::formSliderStyleSheet());
    auto* previewControlsRow = new QWidget(primaryPanel);
    auto* previewControlsLayout = new QHBoxLayout(previewControlsRow);
    previewControlsLayout->setContentsMargins(kSectionContentLeftInset, 2, kSectionContentLeftInset, 0);
    previewControlsLayout->setSpacing(kPreviewControlSpacing);

    previewTimeLabel_ = new QLabel(primaryPanel);
    previewTimeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    previewTimeLabel_->setFixedWidth(rangeControlWidth);
    auto* previewTimeRow = new QWidget(primaryPanel);
    auto* previewTimeLayout = new QHBoxLayout(previewTimeRow);
    const int previewControlsWidth =
        (kPreviewControlButtonWidth * 2)
        + kPreviewControlSpacing
        + kPreviewControlsToSliderGap;
    previewTimeLayout->setContentsMargins(kSectionContentLeftInset + previewControlsWidth, 0, kSectionContentLeftInset, 2);
    previewTimeLayout->setSpacing(0);
    previewTimeLayout->addWidget(previewTimeLabel_, 0, Qt::AlignLeft);
    previewTimeLayout->addStretch(1);

    stopPreviewButton_ = new QToolButton(previewControlsRow);
    stopPreviewButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    stopPreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopPreviewButton_->setIconSize(QSize(16, 16));
    stopPreviewButton_->setFixedSize(QSize(kPreviewControlButtonWidth, kPreviewControlButtonHeight));
    stopPreviewButton_->setIcon(makePreviewStopIcon(UiTheme::colors().iconPrimary));
    stopPreviewButton_->setToolTip(uiText("dialog.video_export.preview.stop", QStringLiteral("Stop")));
    stopPreviewButton_->setAutoRaise(false);
    stopPreviewButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    previewRangeButton_ = new QToolButton(previewControlsRow);
    previewRangeButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    previewRangeButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    previewRangeButton_->setIconSize(QSize(16, 16));
    previewRangeButton_->setFixedSize(QSize(kPreviewControlButtonWidth, kPreviewControlButtonHeight));
    previewRangeButton_->setAutoRaise(false);
    previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    stopPreviewButton_->setEnabled(false);
    previewControlsLayout->addWidget(stopPreviewButton_, 0);
    previewControlsLayout->addWidget(previewRangeButton_, 0);
    previewControlsLayout->addSpacing(kPreviewControlsToSliderGap);
    previewControlsLayout->addWidget(previewSlider_, 1);
    primaryPanelLayout->addWidget(previewControlsRow, 0);
    primaryPanelLayout->addWidget(previewTimeRow, 0);

    optionsContent_ = new QWidget(this);
    auto* optionsLayout = new QGridLayout(optionsContent_);
    optionsLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    optionsLayout->setHorizontalSpacing(10);
    optionsLayout->setVerticalSpacing(6);
    optionsLayout->setColumnStretch(0, 1);
    optionsLayout->setColumnStretch(1, 1);
    showTimestampCheck_ = new QCheckBox(
        l10n(QStringLiteral("Show bottom-left timestamp"), QStringLiteral("显示左下角时间戳")),
        optionsContent_
    );
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    showTimestampCheck_->setText(uiText("dialog.video_export.option.show_timestamp", QStringLiteral("Show bottom-left timestamp")));
    smoothBrightnessCheck_ = new QCheckBox(
        l10n(QStringLiteral("Smooth brightness"), QStringLiteral("平滑亮度")),
        optionsContent_
    );
    smoothBrightnessCheck_->setChecked(baseTask_.smoothBrightness);
    smoothBrightnessCheck_->setText(uiText("dialog.video_export.option.smooth_brightness", QStringLiteral("Smooth brightness")));
    const auto addPercentSliderOption = [](
        QWidget* parent,
        const QString& title,
        int minimum,
        int maximum,
        int step,
        int valuePercent,
        QSlider** sliderOut,
        QLabel** valueOut
    ) {
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
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        containerLayout->addWidget(header, 0);
        containerLayout->addWidget(slider, 0);
        *sliderOut = slider;
        *valueOut = valueLabel;
        return container;
    };
    const auto setSliderOptionTitle = [](QWidget* option, const QString& title) {
        if (option == nullptr) {
            return;
        }
        auto* optionLayout = qobject_cast<QVBoxLayout*>(option->layout());
        if (optionLayout == nullptr || optionLayout->count() <= 0) {
            return;
        }
        QWidget* header = optionLayout->itemAt(0)->widget();
        if (header == nullptr) {
            return;
        }
        auto* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
        if (headerLayout == nullptr || headerLayout->count() <= 0) {
            return;
        }
        auto* titleLabel = qobject_cast<QLabel*>(headerLayout->itemAt(0)->widget());
        if (titleLabel != nullptr) {
            titleLabel->setText(title);
        }
    };
    QWidget* outerBrightnessOption = addPercentSliderOption(
        optionsContent_,
        uiText("dialog.video_export.option.brightness_outer", QStringLiteral("Brightness (Outer)")),
        0,
        100,
        1,
        qRound(qBound(0.0, baseTask_.backgroundBrightnessOuter, 1.0) * 100.0),
        &brightnessOuterSlider_,
        &brightnessOuterValueLabel_
    );
    QWidget* innerBrightnessOption = addPercentSliderOption(
        optionsContent_,
        uiText("dialog.video_export.option.brightness_inner", QStringLiteral("Brightness (Inner)")),
        0,
        100,
        1,
        qRound(qBound(0.0, baseTask_.backgroundBrightnessInner, 1.0) * 100.0),
        &brightnessInnerSlider_,
        &brightnessInnerValueLabel_
    );
    setSliderOptionTitle(outerBrightnessOption, uiText("dialog.video_export.option.brightness_outer", QStringLiteral("Brightness (Outer)")));
    setSliderOptionTitle(innerBrightnessOption, uiText("dialog.video_export.option.brightness_inner", QStringLiteral("Brightness (Inner)")));
    optionsLayout->addWidget(outerBrightnessOption, 1, 0, 1, 1);
    optionsLayout->addWidget(innerBrightnessOption, 1, 1, 1, 1);
    QWidget* layoutSquareScaleOption = addPercentSliderOption(
        optionsContent_,
        l10n(QStringLiteral("Layout Size"), QStringLiteral("Layout整图大小")),
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(miacode::preview_video::normalizedLayoutSquareScale(baseTask_.layoutSquareScale) * 100.0),
        &layoutSquareScaleSlider_,
        &layoutSquareScaleValueLabel_
    );
    setSliderOptionTitle(layoutSquareScaleOption, uiText("dialog.video_export.option.layout_size", QStringLiteral("Layout Size")));
    optionsLayout->addWidget(layoutSquareScaleOption, 2, 0, 1, 2);
    selectedFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(baseTask_.noteFlowSpeed);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const int flowSpeedOptionCount = qRound((flowSpeedMax - flowSpeedMin) / flowSpeedStep);
    selectedFlowSpeed_ = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((selectedFlowSpeed_ - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    QString selectedFlowSpeedLabel = QString::number(selectedFlowSpeed_, 'f', 1);
    flowSpeedButton_ = createDialogMenuButton(optionsContent_, selectedFlowSpeedLabel);
    flowSpeedMenu_ = new QMenu(flowSpeedButton_);
    UiTheme::styleRoundedMenu(*flowSpeedMenu_);
    for (int optionIndex = 0; optionIndex <= flowSpeedOptionCount; ++optionIndex) {
        const double flowSpeed = flowSpeedMin + optionIndex * flowSpeedStep;
        const QString label = QString::number(flowSpeed, 'f', 1);
        addDialogMenuChoice(flowSpeedMenu_, label, [this, flowSpeed, label]() {
            selectedFlowSpeed_ = flowSpeed;
            if (flowSpeedButton_ != nullptr) {
                flowSpeedButton_->setText(label);
            }
            if (previewFlowSpeedCallback_) {
                previewFlowSpeedCallback_(flowSpeed);
            }
        });
    }
    flowSpeedButton_->setMenu(flowSpeedMenu_);
    auto* flowSpeedRow = new QWidget(optionsContent_);
    auto* flowSpeedLayout = new QHBoxLayout(flowSpeedRow);
    flowSpeedLayout->setContentsMargins(0, 0, 0, 0);
    flowSpeedLayout->setSpacing(6);
    auto* flowSpeedLabel = new QLabel(
        uiText("dialog.video_export.option.flow_speed", QStringLiteral("Flow Speed")),
        flowSpeedRow
    );
    flowSpeedLayout->addWidget(flowSpeedLabel, 0);
    flowSpeedLayout->addWidget(flowSpeedButton_, 1);
    optionsLayout->addWidget(flowSpeedRow, 3, 0, 1, 2);
    const QString scaleFillLabel = uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    const QString scaleFitLabel = uiText("dialog.video_export.option.scale.fit", QStringLiteral("Fit (keep full image, may letterbox)"));
    selectedBackgroundScaleMode_ = baseTask_.backgroundScaleMode;
    backgroundScaleModeButton_ = createDialogMenuButton(
        optionsContent_,
        selectedBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain ? scaleFitLabel : scaleFillLabel
    );
    backgroundScaleModeMenu_ = new QMenu(backgroundScaleModeButton_);
    UiTheme::styleRoundedMenu(*backgroundScaleModeMenu_);
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFillLabel, [this, scaleFillLabel]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(scaleFillLabel);
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFitLabel, [this, scaleFitLabel]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(scaleFitLabel);
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    backgroundScaleModeButton_->setMenu(backgroundScaleModeMenu_);
    auto* backgroundScaleModeRow = new QWidget(optionsContent_);
    auto* backgroundScaleModeLayout = new QHBoxLayout(backgroundScaleModeRow);
    backgroundScaleModeLayout->setContentsMargins(0, 0, 0, 0);
    backgroundScaleModeLayout->setSpacing(6);
    auto* backgroundScaleModeLabel = new QLabel(
        uiText("dialog.video_export.option.scale_mode", QStringLiteral("Background / PV Scale Mode")),
        backgroundScaleModeRow
    );
    backgroundScaleModeLayout->addWidget(backgroundScaleModeLabel, 0);
    backgroundScaleModeLayout->addWidget(backgroundScaleModeButton_, 1);
    optionsLayout->addWidget(backgroundScaleModeRow, 4, 0, 1, 2);
    optionsLayout->addWidget(smoothBrightnessCheck_, 5, 0, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    optionsLayout->addWidget(showTimestampCheck_, 5, 1, 1, 1, Qt::AlignLeft | Qt::AlignTop);
    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Options"), QStringLiteral("选项")),
            optionsContent_,
            false,
            &optionsToggle_
        )
    );
    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Export Range"), QStringLiteral("导出区间")),
            rangeContent_,
            false,
            &rangeToggle_
        )
    );

    if (optionsToggle_ != nullptr) {
        optionsToggle_->setText(uiText("dialog.video_export.section.options", QStringLiteral("Options")));
    }
    if (rangeToggle_ != nullptr) {
        rangeToggle_->setText(uiText("dialog.video_export.section.range", QStringLiteral("Export Range")));
    }

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    exportButton_ = buttonBox->addButton(uiText("dialog.video_export.button.export", QStringLiteral("Export")), QDialogButtonBox::AcceptRole);
    exportButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    exportButton_->setMinimumWidth(kDialogActionButtonMinWidth);
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(uiText("dialog.video_export.button.cancel", QStringLiteral("Cancel")));
        cancelButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        cancelButton->setMinimumWidth(kDialogActionButtonMinWidth);
    }
    connect(exportButton_, &QPushButton::clicked, this, &VideoExportDialog::startExport);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(kPreviewTickIntervalMs);
    connect(previewTimer_, &QTimer::timeout, this, &VideoExportDialog::onRangePreviewTick);

    connect(startSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(endSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(previewSlider_, &QSlider::valueChanged, this, &VideoExportDialog::onPreviewSliderChanged);
    connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
        previewScrubRenderElapsed_.invalidate();
    });
    connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
        previewScrubRenderElapsed_.invalidate();
        seekPreview(previewCursorSecond_);
        syncRangeUi();
    });
    connect(setStartButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeStartFromPreview);
    connect(setEndButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeEndFromPreview);
    connect(previewRangeButton_, &QToolButton::clicked, this, &VideoExportDialog::toggleRangePreview);
    connect(stopPreviewButton_, &QToolButton::clicked, this, &VideoExportDialog::stopRangePreviewToStart);
    connect(showTimestampCheck_, &QCheckBox::toggled, this, [this](bool) {
        syncLivePreviewTimestampVisibility();
    });
    connect(smoothBrightnessCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (previewSmoothBrightnessCallback_) {
            previewSmoothBrightnessCallback_(checked);
        }
    });
    connect(brightnessOuterSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessOuterValueLabel_ != nullptr) {
            brightnessOuterValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        if (previewBrightnessCallback_) {
            const double outer = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
            const double inner = brightnessInnerSlider_ != nullptr
                ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
                : baseTask_.backgroundBrightnessInner;
            previewBrightnessCallback_(outer, inner);
        }
    });
    connect(layoutSquareScaleSlider_, &QSlider::valueChanged, this, [this](int value) {
        const double scale = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        if (layoutSquareScaleValueLabel_ != nullptr) {
            layoutSquareScaleValueLabel_->setText(QStringLiteral("%1%").arg(qRound(scale * 100.0)));
        }
        if (previewLayoutScaleCallback_) {
            previewLayoutScaleCallback_(scale);
        }
    });
    connect(brightnessInnerSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessInnerValueLabel_ != nullptr) {
            brightnessInnerValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        if (previewBrightnessCallback_) {
            const double outer = brightnessOuterSlider_ != nullptr
                ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
                : baseTask_.backgroundBrightnessOuter;
            const double inner = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
            previewBrightnessCallback_(outer, inner);
        }
    });
    previewSlider_->setFocusPolicy(Qt::StrongFocus);
    previewSlider_->installEventFilter(this);

    updatePreviewPlayPauseUi();
    syncRangeUi();
    initialResolutionAspectRatio_ = selectedResolutionAspectRatio();
    refreshDialogGeometry();
    if (QWidget* owner = parentWidget(); owner != nullptr) {
        move(desiredDialogTopLeft(owner, size()));
    }
    QTimer::singleShot(0, this, [this]() {
        if (outputPathEdit_ != nullptr) {
            outputPathEdit_->clearFocus();
        }
        if (rangeToggle_ != nullptr) {
            rangeToggle_->setFocus(Qt::OtherFocusReason);
        }
        refreshDialogGeometry();
        if (QWidget* owner = parentWidget(); owner != nullptr) {
            move(desiredDialogTopLeft(owner, size()));
        }
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
    toggle->setStyleSheet(UiTheme::collapsibleToggleStyleSheet());

    auto* panel = new QFrame(container);
    panel->setObjectName(QStringLiteral("VideoExportSectionPanel"));
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(10, 10, 10, 10);
    panelLayout->setSpacing(0);
    panelLayout->addWidget(content, 0);

    layout->addWidget(toggle, 0);
    layout->addWidget(panel, 0);
    updateSectionToggle(toggle, content, expanded);
    panel->setVisible(expanded);

    connect(toggle, &QToolButton::toggled, this, [this, toggle, content, panel](bool checked) {
        updateSectionToggle(toggle, content, checked);
        if (panel != nullptr) {
            panel->setVisible(checked);
        }
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

void VideoExportDialog::syncLivePreviewTimestampVisibility()
{
    if (sourceCanvas_ == nullptr || showTimestampCheck_ == nullptr) {
        return;
    }
    sourceCanvas_->setShowTimestamp(showTimestampCheck_->isChecked());
}

void VideoExportDialog::restoreLivePreviewState()
{
    if (previewStateRestored_) {
        return;
    }
    previewStateRestored_ = true;
    if (sourceCanvas_ != nullptr) {
        sourceCanvas_->setShowTimestamp(initialShowTimestamp_);
        sourceCanvas_->setShowObjectStatsHud(initialShowObjectStatsHud_);
    }
}

void VideoExportDialog::applySelectedAspectRatioToPreview(bool markChanged)
{
    const double aspectRatio = selectedResolutionAspectRatio();
    if (markChanged && qAbs(aspectRatio - initialResolutionAspectRatio_) > 0.0001) {
        previewAspectChangedByDialog_ = true;
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(aspectRatio);
    }
}

void VideoExportDialog::browseOutputPath()
{
    const QString baseDirectory = exportBaseDirectory(baseTask_);
    const QString initial = outputPathEdit_ != nullptr
        ? resolveOutputPathForExport(outputPathEdit_->text(), baseDirectory)
        : QString();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        l10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")),
        initial,
        QStringLiteral("MP4 Video (*.mp4)")
    );
    if (selected.isEmpty() || outputPathEdit_ == nullptr) {
        return;
    }
    outputPathEdit_->setText(displayOutputPathForDialog(selected, baseDirectory));
}

bool VideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        return false;
    }
    VideoExportTask updated = baseTask_;
    const QString baseDirectory = exportBaseDirectory(updated);
    const QString outputPath = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Please choose an output path."), QStringLiteral("请先选择输出路径。"));
        }
        return false;
    }
    updated.outputPath = resolveOutputPathForExport(outputPath, baseDirectory);
    const QSize selectedSize = selectedResolution();
    updated.outputWidth = selectedSize.width() > 0 ? selectedSize.width() : updated.outputWidth;
    updated.outputHeight = selectedSize.height() > 0 ? selectedSize.height() : updated.outputHeight;
    updated.fps = qMax(1, selectedFps_);
    updated.performanceProfile = selectedPerformanceProfile_;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;
    updated.backgroundBrightnessOuter = brightnessOuterSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessOuter;
    updated.backgroundBrightnessInner = brightnessInnerSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessInner;
    updated.layoutSquareScale = layoutSquareScaleSlider_ != nullptr
        ? miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(layoutSquareScaleSlider_->value()) / 100.0)
        : updated.layoutSquareScale;
    updated.smoothBrightness = smoothBrightnessCheck_ != nullptr
        ? smoothBrightnessCheck_->isChecked()
        : updated.smoothBrightness;
    updated.backgroundScaleMode = selectedBackgroundScaleMode_;
    updated.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(selectedFlowSpeed_);
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
    if (selectedResolution_.width() <= 0 || selectedResolution_.height() <= 0) {
        return QSize(qMax(1, baseTask_.outputWidth), qMax(1, baseTask_.outputHeight));
    }
    return selectedResolution_;
}

double VideoExportDialog::selectedResolutionAspectRatio() const
{
    const QSize size = selectedResolution();
    if (size.width() <= 0 || size.height() <= 0) {
        return 1.0;
    }
    return static_cast<double>(size.width()) / static_cast<double>(size.height());
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
    const bool shouldRenderNow = previewSlider_ == nullptr
        || !previewSlider_->isSliderDown()
        || !previewScrubRenderElapsed_.isValid()
        || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        seekPreview(previewCursorSecond_);
        previewScrubRenderElapsed_.restart();
    }
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
    playPreview(previewCursorSecond_);
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
        stopPreviewButton_->setEnabled(previewCursorSecond_ > 0.0005);
    }
    if (seekToCurrent) {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::stopRangePreviewToStart()
{
    stopRangePreview(false);
    previewCursorSecond_ = 0.0;
    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::updatePreviewPlayPauseUi()
{
    if (previewRangeButton_ == nullptr) {
        return;
    }
    const QColor iconColor = UiTheme::colors().iconPrimary;
    if (rangePreviewPlaying_) {
        previewRangeButton_->setIcon(makePreviewPauseIcon(iconColor));
        previewRangeButton_->setToolTip(uiText("dialog.video_export.preview.pause", QStringLiteral("Pause")));
        previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet(true));
    } else {
        previewRangeButton_->setIcon(makePreviewPlayIcon(iconColor));
        previewRangeButton_->setToolTip(uiText("dialog.video_export.preview.play", QStringLiteral("Play")));
        previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
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
    if (previewCursorSecond_ >= totalDurationSeconds_) {
        previewCursorSecond_ = totalDurationSeconds_;
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
    if (startCurrentTimeEdit_ != nullptr) {
        startCurrentTimeEdit_->setText(formatSecond(previewCursorSecond_));
    }
    if (endCurrentTimeEdit_ != nullptr) {
        endCurrentTimeEdit_->setText(formatSecond(previewCursorSecond_));
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(rangePreviewPlaying_ || previewCursorSecond_ > 0.0005);
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
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.video_export.title", QStringLiteral("Export Video")),
            errorMessage
        );
        return;
    }
    requestedExportTask_ = task;
    exportRequested_ = true;
    accept();
}

void VideoExportDialog::closeEvent(QCloseEvent* event)
{
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::closeEvent(event);
}

void VideoExportDialog::done(int result)
{
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::done(result);
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
