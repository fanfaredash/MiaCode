#include "VideoExportDialog.h"

#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "core/scene/PreviewHudState.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
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

struct ResolutionPreset {
    int width = 1080;
    int height = 1080;
    const char* label = "1080x1080 (1:1)";
    double aspectRatio = 1.0;
};

struct HudFontChoice {
    QString label;
    QString path;
    QString family;
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
constexpr int kAudioBitrateOptionsKbps[] = {128, 160, 192, 256, 320};

int normaliseAudioBitrateKbps(int requested)
{
    int closest = kAudioBitrateOptionsKbps[0];
    int closestDelta = qAbs(requested - closest);
    for (int candidate : kAudioBitrateOptionsKbps) {
        const int delta = qAbs(requested - candidate);
        if (delta < closestDelta) {
            closest = candidate;
            closestDelta = delta;
        }
    }
    return closest;
}

QString hudFontLibraryDirPath()
{
    const QFileInfo preferencesInfo(UiText::preferencesFilePath());
    return preferencesInfo.absoluteDir().filePath(QStringLiteral("fonts"));
}

QString currentHudFontPath()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    const QJsonObject videoExport = app.value(QStringLiteral("video_export")).toObject();
    const QString path = videoExport.value(QStringLiteral("hud_font_path")).toString();
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
}

QString uniqueHudFontLibraryPath(const QFileInfo& sourceInfo)
{
    QDir dir(hudFontLibraryDirPath());
    dir.mkpath(QStringLiteral("."));
    const QString baseName = sourceInfo.completeBaseName().isEmpty()
        ? QStringLiteral("font")
        : sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix().isEmpty() ? QStringLiteral("ttf") : sourceInfo.suffix();
    QString candidate = dir.filePath(baseName + QLatin1Char('.') + suffix);
    int copyIndex = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir.filePath(QStringLiteral("%1_%2.%3").arg(baseName).arg(copyIndex).arg(suffix));
        ++copyIndex;
    }
    return QFileInfo(candidate).absoluteFilePath();
}

QString fontFamilyForFile(const QString& path)
{
    const int fontId = QFontDatabase::addApplicationFont(path);
    if (fontId < 0) {
        return QString();
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    return families.isEmpty() ? QString() : families.first();
}

QVector<HudFontChoice> hudFontChoices()
{
    QVector<HudFontChoice> choices;
    choices.push_back({
        UiText::text(QStringLiteral("dialog.video_export.option.hud_font_default")).isEmpty()
            ? QStringLiteral("Default font")
            : UiText::text(QStringLiteral("dialog.video_export.option.hud_font_default")),
        QString(),
        QString()
    });

    QDir dir(hudFontLibraryDirPath());
    const QFileInfoList files = dir.entryInfoList(
        QStringList{QStringLiteral("*.ttf")},
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase
    );
    for (const QFileInfo& file : files) {
        const QString path = file.absoluteFilePath();
        const QString family = fontFamilyForFile(path);
        if (family.isEmpty()) {
            continue;
        }
        choices.push_back({
            QStringLiteral("%1 (%2)").arg(family, file.fileName()),
            path,
            family
        });
    }
    return choices;
}

void populateHudFontCombo(QComboBox* combo, const QString& selectedPath)
{
    if (combo == nullptr) {
        return;
    }
    const QSignalBlocker blocker(combo);
    combo->clear();
    const QVector<HudFontChoice> choices = hudFontChoices();
    int selectedIndex = 0;
    const QString normalizedSelected = selectedPath.isEmpty()
        ? QString()
        : QFileInfo(selectedPath).absoluteFilePath();
    for (int i = 0; i < choices.size(); ++i) {
        combo->addItem(choices[i].label, choices[i].path);
        if (!normalizedSelected.isEmpty()
            && QFileInfo(choices[i].path).absoluteFilePath() == normalizedSelected) {
            selectedIndex = i;
        }
    }
    combo->setCurrentIndex(selectedIndex);
}

enum class VideoExportShortcutAction {
    None,
    TogglePlayPause,
    StopOrPlay,
};

VideoExportShortcutAction matchVideoExportShortcut(const QKeyEvent* event)
{
    if (event == nullptr) {
        return VideoExportShortcutAction::None;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_Space) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        return VideoExportShortcutAction::StopOrPlay;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_X) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    return VideoExportShortcutAction::None;
}

QString uiText(const char* key, const QString& fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? fallback : translated;
}

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

QString flowSpeedValueLabel(double flowSpeed)
{
    const double snapped = qRound(flowSpeed * 4.0) / 4.0;
    const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
    const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
    return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
}

QString exportDialogResolutionLabel(const QSize& size)
{
    for (const ResolutionPreset& preset : kResolutionPresets) {
        if (preset.width == size.width() && preset.height == size.height()) {
            return QString::fromLatin1(preset.label);
        }
    }
    return QStringLiteral("%1x%2").arg(qMax(1, size.width())).arg(qMax(1, size.height()));
}

QString videoExportPresetToken(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return QStringLiteral("high_quality");
    case VideoExportPreset::Fast:
    default:
        return QStringLiteral("fast");
    }
}

VideoExportPreset videoExportPresetFromStoredValue(const QJsonValue& value, VideoExportPreset fallback)
{
    if (value.isString()) {
        const QString token = value.toString().trimmed();
        if (token.compare(QStringLiteral("high_quality"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("high_compression"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::Fast;
        }
    }
    return fallback;
}

QString exportDialogPresetLabel(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return uiText("dialog.video_export.preset.high_quality", QStringLiteral("High Quality"));
    case VideoExportPreset::Fast:
    default:
        return uiText("dialog.video_export.preset.fast", QStringLiteral("Fast"));
    }
}

QString exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode mode)
{
    switch (mode) {
    case PreviewBackgroundScaleMode::FitContain:
        return uiText("dialog.video_export.option.scale.fit", QStringLiteral("Fit (keep full image, may letterbox)"));
    case PreviewBackgroundScaleMode::SquareFitContain:
        return uiText("dialog.video_export.option.scale.square_fit", QStringLiteral("1:1 Fit (center square)"));
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    }
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
    return UiTheme::dialogTransportPlayIcon(color);
}

QIcon makePreviewStopIcon(const QColor& color)
{
    return UiTheme::dialogTransportStopIcon(color);
}

QIcon makePreviewPauseIcon(const QColor& color)
{
    return UiTheme::dialogTransportPauseIcon(color);
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
    SeekPreviewCallback seekPreviewCallback,
    PlayPreviewCallback playPreviewCallback,
    PausePreviewCallback pausePreviewCallback,
    IsPreviewPlayingCallback isPreviewPlayingCallback,
    CurrentPreviewSecondCallback currentPreviewSecondCallback,
    PreviewTimestampCallback previewTimestampCallback,
    PreviewObjectStatsCallback previewObjectStatsCallback,
    PreviewChartInfoCallback previewChartInfoCallback,
    PreviewAspectRatioCallback previewAspectRatioCallback,
    PreviewBrightnessCallback previewBrightnessCallback,
    PreviewLayoutScaleCallback previewLayoutScaleCallback,
    PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback,
    PreviewScaleModeCallback previewScaleModeCallback,
    PreviewTapFlowSpeedCallback previewTapFlowSpeedCallback,
    PreviewTouchFlowSpeedCallback previewTouchFlowSpeedCallback,
    QWidget* parent
)
    : QDialog(parent)
    , baseTask_(baseTask)
    , seekPreviewCallback_(std::move(seekPreviewCallback))
    , playPreviewCallback_(std::move(playPreviewCallback))
    , pausePreviewCallback_(std::move(pausePreviewCallback))
    , isPreviewPlayingCallback_(std::move(isPreviewPlayingCallback))
    , currentPreviewSecondCallback_(std::move(currentPreviewSecondCallback))
    , previewTimestampCallback_(std::move(previewTimestampCallback))
    , previewObjectStatsCallback_(std::move(previewObjectStatsCallback))
    , previewChartInfoCallback_(std::move(previewChartInfoCallback))
    , previewAspectRatioCallback_(std::move(previewAspectRatioCallback))
    , previewBrightnessCallback_(std::move(previewBrightnessCallback))
    , previewLayoutScaleCallback_(std::move(previewLayoutScaleCallback))
    , previewSmoothBrightnessCallback_(std::move(previewSmoothBrightnessCallback))
    , previewScaleModeCallback_(std::move(previewScaleModeCallback))
    , previewTapFlowSpeedCallback_(std::move(previewTapFlowSpeedCallback))
    , previewTouchFlowSpeedCallback_(std::move(previewTouchFlowSpeedCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(uiText("dialog.video_export.title", QStringLiteral("Export Video")));
    setModal(true);
    setMinimumWidth(kDialogMinWidth);
    resize(680, 360);
    setStyleSheet(UiTheme::exportDialogStyleSheet());
    UiDialogs::configureDialogPreviewShortcuts(this, UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls);
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        app->installEventFilter(this);
    }
    initialShowTimestamp_ = baseTask_.showTimestamp;
    initialShowObjectStats_ = baseTask_.showObjectStatsHud;
    initialShowChartInfo_ = baseTask_.showChartInfoHud;

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

    // Output section — same label-above-control style as the dropdown
    // grid below. Top line is the "Output" label, bottom line carries
    // the path field (stretches) and the Browse button (fixed width).
    auto* outputRow = new QWidget(primaryPanel);
    auto* outputColumn = new QVBoxLayout(outputRow);
    outputColumn->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    outputColumn->setSpacing(6);
    auto* outputLabel = new QLabel(l10n(QStringLiteral("Output"), QStringLiteral("杈撳嚭")), outputRow);
    outputLabel->setText(uiText("dialog.video_export.output", QStringLiteral("Output")));
    outputColumn->addWidget(outputLabel, 0);
    auto* outputControlRow = new QWidget(outputRow);
    auto* outputControlLayout = new QHBoxLayout(outputControlRow);
    outputControlLayout->setContentsMargins(0, 0, 0, 0);
    outputControlLayout->setSpacing(kFormRowSpacing);
    outputPathEdit_ = new QLineEdit(outputControlRow);
    outputPathEdit_->setText(displayOutputPathForDialog(baseTask_.outputPath, exportBaseDirectory(baseTask_)));
    auto* browseButton = new QPushButton(l10n(QStringLiteral("Browse..."), QStringLiteral("娴忚...")), outputRow);
    browseButton->setText(uiText("dialog.video_export.browse", QStringLiteral("Browse...")));
    browseButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    const int rightAlignedButtonWidth = qMax(browseButton->sizeHint().width(), kDialogActionButtonMinWidth);
    browseButton->setFixedWidth(rightAlignedButtonWidth);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputControlLayout->addWidget(outputPathEdit_, 1);
    outputControlLayout->addWidget(browseButton, 0);
    outputColumn->addWidget(outputControlRow, 0);
    primaryPanelLayout->addWidget(outputRow, 0);
    // Beta20-fix — 2x2 grid layout for the 4 dropdown options.
    //
    // Mirrors the "Gameplay" group in the Video Settings dialog: each
    // cell stacks its label above its control, two cells per row, equal
    // column stretch. Replaces the previous single-row "label + button
    // | label + button" arrangement whose labels were either truncated
    // (English ran past the 52 px label clamp, rendering as "Resolutio"
    // / "Audio qu" / "Export Se") or cramped against the dropdown when
    // the clamp was removed.
    //
    // Layout:
    //   ┌────────────────────────┬────────────────────────┐
    //   │ Resolution             │ FPS                    │
    //   │ [1920 × 1080       ▼] │ [60 FPS            ▼] │
    //   ├────────────────────────┼────────────────────────┤
    //   │ Audio quality          │ Export Settings        │
    //   │ [192 kbps          ▼] │ [Fast              ▼] │
    //   └────────────────────────┴────────────────────────┘
    auto* optionsGrid = new QWidget(primaryPanel);
    auto* optionsGridLayout = new QGridLayout(optionsGrid);
    optionsGridLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    optionsGridLayout->setHorizontalSpacing(16);
    optionsGridLayout->setVerticalSpacing(8);
    optionsGridLayout->setColumnStretch(0, 1);
    optionsGridLayout->setColumnStretch(1, 1);

    const auto addOptionField = [optionsGrid, optionsGridLayout](
        int row,
        int column,
        const QString& labelText,
        QWidget* control
    ) {
        auto* field = new QWidget(optionsGrid);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        fieldLayout->addWidget(control, 0);
        optionsGridLayout->addWidget(field, row, column);
    };

    // Resolution dropdown (row 0, col 0).
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
        optionsGrid,
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
            persistExportOnlySettings();
        });
    }
    resolutionButton_->setMenu(resolutionMenu_);
    addOptionField(0, 0, uiText("dialog.video_export.resolution", QStringLiteral("Resolution")), resolutionButton_);

    // FPS dropdown (row 0, col 1).
    selectedFps_ = baseTask_.fps >= 90 ? 120 : 60;
    fpsButton_ = createDialogMenuButton(optionsGrid, QStringLiteral("%1 FPS").arg(selectedFps_));
    fpsMenu_ = new QMenu(fpsButton_);
    UiTheme::styleRoundedMenu(*fpsMenu_);
    for (int fps : kFpsOptions) {
        const QString label = QStringLiteral("%1 FPS").arg(fps);
        addDialogMenuChoice(fpsMenu_, label, [this, fps, label]() {
            selectedFps_ = fps;
            if (fpsButton_ != nullptr) {
                fpsButton_->setText(label);
            }
            persistExportOnlySettings();
        });
    }
    fpsButton_->setMenu(fpsMenu_);
    addOptionField(0, 1, uiText("dialog.video_export.fps", QStringLiteral("FPS")), fpsButton_);

    // Audio quality dropdown (row 1, col 0) — picks AAC bitrate forwarded
    // to ffmpeg as `-b:a <kbps>k`. Default 192 is a step above the previous
    // hard-coded 160k baseline; 320k matches the AAC LC stereo ceiling for
    // users who care about bgm fidelity in the exported clip.
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(baseTask_.audioBitrateKbps);
    const auto formatAudioBitrateLabel = [](int kbps) -> QString {
        return QStringLiteral("%1 kbps").arg(kbps);
    };
    audioBitrateButton_ = createDialogMenuButton(optionsGrid, formatAudioBitrateLabel(selectedAudioBitrateKbps_));
    audioBitrateMenu_ = new QMenu(audioBitrateButton_);
    UiTheme::styleRoundedMenu(*audioBitrateMenu_);
    for (int kbps : kAudioBitrateOptionsKbps) {
        const QString label = formatAudioBitrateLabel(kbps);
        addDialogMenuChoice(audioBitrateMenu_, label, [this, kbps, label]() {
            selectedAudioBitrateKbps_ = kbps;
            if (audioBitrateButton_ != nullptr) {
                audioBitrateButton_->setText(label);
            }
            persistExportOnlySettings();
        });
    }
    audioBitrateButton_->setMenu(audioBitrateMenu_);
    addOptionField(
        1,
        0,
        uiText("dialog.video_export.audio_bitrate", QStringLiteral("Audio quality")),
        audioBitrateButton_
    );

    // Export Settings dropdown (row 1, col 1).
    selectedPreset_ = baseTask_.preset;
    presetButton_ = createDialogMenuButton(optionsGrid, exportDialogPresetLabel(selectedPreset_));
    presetMenu_ = new QMenu(presetButton_);
    UiTheme::styleRoundedMenu(*presetMenu_);
    addDialogMenuChoice(
        presetMenu_,
        uiText("dialog.video_export.preset.fast", QStringLiteral("Fast")),
        [this]() {
            selectedPreset_ = VideoExportPreset::Fast;
            if (presetButton_ != nullptr) {
                presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
            }
            persistExportOnlySettings();
        }
    );
    addDialogMenuChoice(
        presetMenu_,
        uiText("dialog.video_export.preset.high_quality", QStringLiteral("High Quality")),
        [this]() {
            selectedPreset_ = VideoExportPreset::HighQuality;
            if (presetButton_ != nullptr) {
                presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
            }
            persistExportOnlySettings();
        }
    );
    presetButton_->setMenu(presetMenu_);
    addOptionField(
        1,
        1,
        uiText("dialog.video_export.preset", QStringLiteral("Export Settings")),
        presetButton_
    );

    primaryPanelLayout->addWidget(optionsGrid, 0);

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

    auto* mergedRangeRows = new QWidget(rangeContent_);
    auto* mergedRangeLayout = new QGridLayout(mergedRangeRows);
    mergedRangeLayout->setContentsMargins(0, 0, 0, 0);
    mergedRangeLayout->setHorizontalSpacing(kSetButtonLeftGap);
    mergedRangeLayout->setVerticalSpacing(rangeLayout->spacing());
    mergedRangeLayout->setColumnStretch(3, 1);

    auto* startLabel = new QLabel(l10n(QStringLiteral("Start"), QStringLiteral("璧峰")), mergedRangeRows);
    startLabel->setFixedWidth(kRangeLabelWidth);
    startLabel->setText(uiText("dialog.video_export.range.start", QStringLiteral("Start")));
    startLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setStartButton = new QPushButton(l10n(QStringLiteral("Set Start"), QStringLiteral("璁惧畾璧峰")), mergedRangeRows);
    setStartButton->setFixedWidth(kRangeSetButtonWidth);
    setStartButton->setText(uiText("dialog.video_export.range.set_left", QStringLiteral("<- Set")));
    setStartButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    startCurrentTimeEdit_ = new QLineEdit(mergedRangeRows);
    startCurrentTimeEdit_->setReadOnly(true);
    startCurrentTimeEdit_->setFocusPolicy(Qt::NoFocus);
    startCurrentTimeEdit_->setMinimumWidth(108);
    startCurrentTimeEdit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    startCurrentTimeEdit_->setMinimumHeight(startSecondSpin_->sizeHint().height() * 2 + mergedRangeLayout->verticalSpacing() + 2);
    startCurrentTimeEdit_->setAlignment(Qt::AlignCenter);
    startCurrentTimeEdit_->setStyleSheet(UiTheme::readOnlyLineEditStyleSheet());

    auto* endLabel = new QLabel(l10n(QStringLiteral("End"), QStringLiteral("缁撴潫")), mergedRangeRows);
    endLabel->setFixedWidth(kRangeLabelWidth);
    endLabel->setText(uiText("dialog.video_export.range.end", QStringLiteral("End")));
    endLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setEndButton = new QPushButton(l10n(QStringLiteral("Set End"), QStringLiteral("璁惧畾缁撴潫")), mergedRangeRows);
    setEndButton->setFixedWidth(kRangeSetButtonWidth);
    setEndButton->setText(uiText("dialog.video_export.range.set_left", QStringLiteral("<- Set")));
    setEndButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());

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
        l10n(QStringLiteral("Show bottom-left timestamp"), QStringLiteral("鏄剧ず宸︿笅瑙掓椂闂存埑")),
        optionsContent_
    );
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    showTimestampCheck_->setText(uiText("dialog.video_export.option.show_timestamp", QStringLiteral("Show bottom-left timestamp")));
    showObjectStatsCheck_ = new QCheckBox(
        uiText("dialog.video_export.option.show_object_stats", QStringLiteral("Show object stats")),
        optionsContent_
    );
    showObjectStatsCheck_->setChecked(baseTask_.showObjectStatsHud);
    showChartInfoCheck_ = new QCheckBox(
        uiText("dialog.video_export.option.show_chart_info", QStringLiteral("Show chart info")),
        optionsContent_
    );
    showChartInfoCheck_->setChecked(baseTask_.showChartInfoHud);
    addIntroCheck_ = new QCheckBox(
        l10n(QStringLiteral("Add intro"), QStringLiteral("添加片头")),
        optionsContent_
    );
    addIntroCheck_->setChecked(baseTask_.intro.enabled);
    addIntroCheck_->setToolTip(l10n(
        QStringLiteral("Prepend the maimai track-start intro (full exports only)."),
        QStringLiteral("在视频开头加入 maimai 风格片头（仅完整导出包含）。")));
    smoothBrightnessCheck_ = new QCheckBox(
        l10n(QStringLiteral("Smooth brightness"), QStringLiteral("骞虫粦浜害")),
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
        l10n(QStringLiteral("Layout Size"), QStringLiteral("Layout鏁村浘澶у皬")),
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(miacode::preview_video::normalizedLayoutSquareScale(baseTask_.layoutSquareScale) * 100.0),
        &layoutSquareScaleSlider_,
        &layoutSquareScaleValueLabel_
    );
    setSliderOptionTitle(layoutSquareScaleOption, uiText("dialog.video_export.option.layout_size", QStringLiteral("Stage Display Scale")));
    optionsLayout->addWidget(layoutSquareScaleOption, 2, 0, 1, 2);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const auto snapFlowSpeed = [flowSpeedMin, flowSpeedMax, flowSpeedStep](double flowSpeed) {
        return qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
    };
    selectedTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(baseTask_.tapFlowSpeed);
    selectedTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(baseTask_.touchFlowSpeed);
    selectedTapFlowSpeed_ = snapFlowSpeed(selectedTapFlowSpeed_);
    selectedTouchFlowSpeed_ = snapFlowSpeed(selectedTouchFlowSpeed_);
    const auto createFlowSpeedRow = [
        this,
        flowSpeedMin,
        flowSpeedMax,
        snapFlowSpeed
    ](
        const QString& labelText,
        QLineEdit** editOut,
        double* selectedFlowSpeed,
        const std::function<void(double)>& callback
    ) {
        auto* flowSpeedEdit = new QLineEdit(this->optionsContent_);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
        flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
        auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
        flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowSpeedValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, this, [flowSpeedEdit, selectedFlowSpeed, callback, snapFlowSpeed]() {
            if (flowSpeedEdit == nullptr || selectedFlowSpeed == nullptr) {
                return;
            }
            bool ok = false;
            const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
            if (!ok) {
                flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
                return;
            }
            *selectedFlowSpeed = snapFlowSpeed(typedSpeed);
            flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
            if (callback) {
                callback(*selectedFlowSpeed);
            }
        });
        if (editOut != nullptr) {
            *editOut = flowSpeedEdit;
        }
        auto* flowSpeedRow = new QWidget(this->optionsContent_);
        auto* flowSpeedLayout = new QHBoxLayout(flowSpeedRow);
        flowSpeedLayout->setContentsMargins(0, 0, 0, 0);
        flowSpeedLayout->setSpacing(6);
        auto* flowSpeedLabel = new QLabel(labelText, flowSpeedRow);
        flowSpeedLayout->addWidget(flowSpeedLabel, 0);
        flowSpeedLayout->addWidget(flowSpeedEdit, 1);
        return flowSpeedRow;
    };
    QWidget* tapFlowSpeedRow = createFlowSpeedRow(
        uiText("dialog.video_export.option.tap_flow_speed", QStringLiteral("Tap Flow Speed")),
        &tapFlowSpeedEdit_,
        &selectedTapFlowSpeed_,
        [this](double flowSpeed) {
            if (previewTapFlowSpeedCallback_) {
                previewTapFlowSpeedCallback_(flowSpeed);
            }
        }
    );
    QWidget* touchFlowSpeedRow = createFlowSpeedRow(
        uiText("dialog.video_export.option.touch_flow_speed", QStringLiteral("Touch Flow Speed")),
        &touchFlowSpeedEdit_,
        &selectedTouchFlowSpeed_,
        [this](double flowSpeed) {
            if (previewTouchFlowSpeedCallback_) {
                previewTouchFlowSpeedCallback_(flowSpeed);
            }
        }
    );
    optionsLayout->addWidget(tapFlowSpeedRow, 3, 0, 1, 1);
    optionsLayout->addWidget(touchFlowSpeedRow, 3, 1, 1, 1);
    const QString scaleFillLabel = uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    const QString scaleFitLabel = uiText("dialog.video_export.option.scale.fit", QStringLiteral("Fit (keep full image, may letterbox)"));
    const QString scaleSquareFitLabel = uiText(
        "dialog.video_export.option.scale.square_fit",
        QStringLiteral("1:1 Fit (center square)"));
    selectedBackgroundScaleMode_ = baseTask_.backgroundScaleMode;
    backgroundScaleModeButton_ = createDialogMenuButton(
        optionsContent_,
        exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_)
    );
    backgroundScaleModeMenu_ = new QMenu(backgroundScaleModeButton_);
    UiTheme::styleRoundedMenu(*backgroundScaleModeMenu_);
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFillLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFitLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleSquareFitLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::SquareFitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
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
    auto* objectStatsRow = new QWidget(optionsContent_);
    auto* objectStatsLayout = new QHBoxLayout(objectStatsRow);
    objectStatsLayout->setContentsMargins(0, 0, 0, 0);
    objectStatsLayout->setSpacing(8);
    hudFontSettingsButton_ = new QPushButton(
        uiText("dialog.video_export.option.hud_font_settings", QStringLiteral("Font Settings")),
        objectStatsRow
    );
    hudFontSettingsButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    objectStatsLayout->addWidget(showObjectStatsCheck_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    objectStatsLayout->addWidget(showChartInfoCheck_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    objectStatsLayout->addWidget(hudFontSettingsButton_, 0);
    objectStatsLayout->addStretch(1);
    optionsLayout->addWidget(objectStatsRow, 6, 0, 1, 2);
    optionsLayout->addWidget(addIntroCheck_, 7, 0, 1, 2, Qt::AlignLeft | Qt::AlignTop);
    refreshAddIntroEnabledState();
    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Options"), QStringLiteral("閫夐」")),
            optionsContent_,
            false,
            &optionsToggle_
        )
    );
    rootLayout->addWidget(
        buildCollapsibleSection(
            l10n(QStringLiteral("Export Range"), QStringLiteral("瀵煎嚭鍖洪棿")),
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
    previewHeldSeekTimer_ = new QTimer(this);
    previewHeldSeekTimer_->setTimerType(Qt::PreciseTimer);
    previewHeldSeekTimer_->setInterval(miacode::preview_interaction::kSeekHoldTickIntervalMs);
    connect(previewHeldSeekTimer_, &QTimer::timeout, this, &VideoExportDialog::applyPreviewHeldSeekTick);

    connect(startSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(endSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(previewSlider_, &QSlider::valueChanged, this, &VideoExportDialog::onPreviewSliderChanged);
    connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
        stopPreviewHeldSeek();
        previewScrubRenderElapsed_.invalidate();
    });
    connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
        stopPreviewHeldSeek();
        previewScrubRenderElapsed_.invalidate();
        seekPreview(previewCursorSecond_);
        syncRangeUi();
    });
    connect(setStartButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeStartFromPreview);
    connect(setEndButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeEndFromPreview);
    connect(previewRangeButton_, &QToolButton::clicked, this, &VideoExportDialog::toggleRangePreview);
    connect(stopPreviewButton_, &QToolButton::clicked, this, &VideoExportDialog::stopRangePreviewToStart);
    connect(showTimestampCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewTimestampVisibility();
        initialShowTimestamp_ = checked;
    });
    connect(showObjectStatsCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewObjectStatsVisibility();
        initialShowObjectStats_ = checked;
    });
    connect(showChartInfoCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewChartInfoVisibility();
        initialShowChartInfo_ = checked;
    });
    connect(hudFontSettingsButton_, &QPushButton::clicked, this, &VideoExportDialog::openHudFontSettingsDialog);
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

    loadPersistedSettings();
    initialResolutionAspectRatio_ = selectedResolutionAspectRatio();
    syncRangeUi();
    updatePreviewPlayPauseUi();
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
    if (showTimestampCheck_ == nullptr || !previewTimestampCallback_) {
        return;
    }
    previewTimestampCallback_(showTimestampCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewObjectStatsVisibility()
{
    if (showObjectStatsCheck_ == nullptr || !previewObjectStatsCallback_) {
        return;
    }
    previewObjectStatsCallback_(showObjectStatsCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewChartInfoVisibility()
{
    if (showChartInfoCheck_ == nullptr || !previewChartInfoCallback_) {
        return;
    }
    previewChartInfoCallback_(showChartInfoCheck_->isChecked());
}

void VideoExportDialog::restoreLivePreviewState()
{
    if (previewStateRestored_) {
        return;
    }
    previewStateRestored_ = true;
    if (previewTimestampCallback_) {
        previewTimestampCallback_(initialShowTimestamp_);
    }
    if (previewObjectStatsCallback_) {
        previewObjectStatsCallback_(initialShowObjectStats_);
    }
    if (previewChartInfoCallback_) {
        previewChartInfoCallback_(initialShowChartInfo_);
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(1.0);
    }
}

void VideoExportDialog::openHudFontSettingsDialog()
{
    QDialog dialog(this);
    const QString title = uiText("dialog.video_export.option.hud_font_settings", QStringLiteral("Font Settings"));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowFlags((dialog.windowFlags() | Qt::FramelessWindowHint) & ~Qt::WindowContextHelpButtonHint);
    dialog.setStyleSheet(UiTheme::exportDialogStyleSheet());

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(0);

    auto* panel = new QFrame(&dialog);
    panel->setObjectName(QStringLiteral("VideoExportPrimaryPanel"));
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 12, 14, 14);
    panelLayout->setSpacing(10);

    auto* headerRow = new QWidget(panel);
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    auto* titleLabel = new QLabel(title, headerRow);
    QFont titleFont = titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    auto* headerCloseButton = new QToolButton(headerRow);
    headerCloseButton->setText(QStringLiteral("×"));
    headerCloseButton->setFixedSize(28, 28);
    headerCloseButton->setAutoRaise(false);
    headerCloseButton->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    headerCloseButton->setToolTip(uiText("dialog.video_export.button.close", QStringLiteral("Close")));
    headerLayout->addWidget(titleLabel, 1);
    headerLayout->addWidget(headerCloseButton, 0);

    auto* currentLabel = new QLabel(panel);
    currentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* fontCombo = new QComboBox(panel);
    fontCombo->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    fontCombo->setMinimumWidth(320);
    auto* sampleLabel = new QLabel(panel);
    sampleLabel->setMinimumWidth(320);
    sampleLabel->setAlignment(Qt::AlignCenter);
    sampleLabel->setText(QStringLiteral("12:34:567  TAP  HOLD  SLIDE  101.0000%"));
    sampleLabel->setStyleSheet(QStringLiteral(
        "QLabel { min-height: 48px; padding: 8px 10px; border: 1px solid rgba(128,128,128,80);"
        " border-radius: 8px; background: rgba(128,128,128,20); color: palette(text); }"
    ));

    populateHudFontCombo(fontCombo, currentHudFontPath());

    const auto refreshDialogFont = [&](const QString& selectedPath = currentHudFontPath()) {
        populateHudFontCombo(fontCombo, selectedPath);
        const QString family = miacode::preview::scene::previewHudFontDisplayName();
        const QString displayName = family == QLatin1String("Default")
            ? uiText("dialog.video_export.option.hud_font_default", QStringLiteral("Default font"))
            : family;
        currentLabel->setText(
            uiText("dialog.video_export.option.hud_font_current", QStringLiteral("Current font: %1")).arg(displayName)
        );
        sampleLabel->setFont(miacode::preview::scene::previewHudTimestampFont(13, QFont::DemiBold));
    };
    refreshDialogFont();

    auto* buttonRow = new QWidget(panel);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    auto* importButton = new QPushButton(
        uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
        buttonRow
    );
    auto* resetButton = new QPushButton(
        uiText("dialog.video_export.option.reset_hud_font", QStringLiteral("Reset")),
        buttonRow
    );
    auto* closeButton = new QPushButton(
        uiText("dialog.video_export.button.close", QStringLiteral("Close")),
        buttonRow
    );
    importButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    resetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    buttonLayout->addWidget(importButton, 0);
    buttonLayout->addWidget(resetButton, 0);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(closeButton, 0);

    panelLayout->addWidget(headerRow, 0);
    panelLayout->addWidget(currentLabel, 0);
    panelLayout->addWidget(fontCombo, 0);
    panelLayout->addWidget(sampleLabel, 0);
    panelLayout->addWidget(buttonRow, 0);
    layout->addWidget(panel, 0);

    connect(fontCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        const QString selectedPath = fontCombo->itemData(index).toString();
        miacode::preview::scene::setPreviewHudCustomFontPath(selectedPath);
        refreshLivePreviewHudFont();
        const QString family = miacode::preview::scene::previewHudFontDisplayName();
        const QString displayName = family == QLatin1String("Default")
            ? uiText("dialog.video_export.option.hud_font_default", QStringLiteral("Default font"))
            : family;
        currentLabel->setText(
            uiText("dialog.video_export.option.hud_font_current", QStringLiteral("Current font: %1")).arg(displayName)
        );
        sampleLabel->setFont(miacode::preview::scene::previewHudTimestampFont(13, QFont::DemiBold));
    });
    connect(importButton, &QPushButton::clicked, &dialog, [&]() {
        const QString importedPath = importHudFontFromUser(&dialog);
        if (!importedPath.isEmpty()) {
            refreshDialogFont(importedPath);
        }
    });
    connect(resetButton, &QPushButton::clicked, &dialog, [&]() {
        resetHudFont();
        refreshDialogFont(QString());
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(headerCloseButton, &QToolButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

QString VideoExportDialog::importHudFontFromUser(QWidget* parent)
{
    const QString selected = QFileDialog::getOpenFileName(
        parent != nullptr ? parent : this,
        uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
        QString(),
        QStringLiteral("TrueType Font (*.ttf)")
    );
    if (selected.isEmpty()) {
        return QString();
    }
    const QFileInfo info(selected);
    if (!info.isFile() || info.suffix().compare(QStringLiteral("ttf"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(
            parent != nullptr ? parent : this,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.invalid_hud_font", QStringLiteral("Please select a .ttf font file."))
        );
        return QString();
    }

    const int fontId = QFontDatabase::addApplicationFont(info.absoluteFilePath());
    const QStringList families = fontId >= 0 ? QFontDatabase::applicationFontFamilies(fontId) : QStringList();
    if (families.isEmpty()) {
        QMessageBox::warning(
            parent != nullptr ? parent : this,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.invalid_hud_font", QStringLiteral("Please select a .ttf font file."))
        );
        return QString();
    }
    const QString targetPath = uniqueHudFontLibraryPath(info);
    if (!QFile::copy(info.absoluteFilePath(), targetPath)) {
        QMessageBox::warning(
            parent != nullptr ? parent : this,
            uiText("dialog.video_export.option.import_hud_font", QStringLiteral("Import Font")),
            uiText("dialog.video_export.error.copy_hud_font_failed", QStringLiteral("Failed to copy the font into the font library."))
        );
        return QString();
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(targetPath);
    refreshLivePreviewHudFont();
    return targetPath;
}

void VideoExportDialog::resetHudFont()
{
    miacode::preview::scene::setPreviewHudCustomFontPath(QString());
    refreshLivePreviewHudFont();
}

void VideoExportDialog::refreshLivePreviewHudFont()
{
    const double second = currentPreviewSecond();
    if (qIsFinite(second)) {
        seekPreview(second);
    } else {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::loadPersistedSettings()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();

    const int savedWidth = settings.value(QStringLiteral("resolution_width")).toInt(selectedResolution_.width());
    const int savedHeight = settings.value(QStringLiteral("resolution_height")).toInt(selectedResolution_.height());
    if (savedWidth > 0 && savedHeight > 0) {
        selectedResolution_ = QSize(savedWidth, savedHeight);
        if (resolutionButton_ != nullptr) {
            resolutionButton_->setText(exportDialogResolutionLabel(selectedResolution_));
        }
        applySelectedAspectRatioToPreview(false);
    }

    const int savedFps = settings.value(QStringLiteral("fps")).toInt(selectedFps_);
    selectedFps_ = savedFps >= 90 ? 120 : 60;
    if (fpsButton_ != nullptr) {
        fpsButton_->setText(QStringLiteral("%1 FPS").arg(selectedFps_));
    }

    const int savedAudioBitrate = settings.value(QStringLiteral("audio_bitrate_kbps"))
                                         .toInt(selectedAudioBitrateKbps_);
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(savedAudioBitrate);
    if (audioBitrateButton_ != nullptr) {
        audioBitrateButton_->setText(QStringLiteral("%1 kbps").arg(selectedAudioBitrateKbps_));
    }

    selectedPreset_ = videoExportPresetFromStoredValue(
        settings.value(QStringLiteral("preset")),
        selectedPreset_
    );
    if (presetButton_ != nullptr) {
        presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
    }
}

void VideoExportDialog::savePersistedSettings(const VideoExportTask& task) const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), task.outputWidth);
    settings.insert(QStringLiteral("resolution_height"), task.outputHeight);
    settings.insert(QStringLiteral("fps"), task.fps);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), task.audioBitrateKbps);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(task.preset));
    miacode::video_export::saveDialogPreferences(settings);
}

void VideoExportDialog::persistExportOnlySettings() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), selectedResolution().width());
    settings.insert(QStringLiteral("resolution_height"), selectedResolution().height());
    settings.insert(QStringLiteral("fps"), selectedFps_);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), selectedAudioBitrateKbps_);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(selectedPreset_));
    miacode::video_export::saveDialogPreferences(settings);
}

bool VideoExportDialog::stepPreviewSliderBySeconds(double deltaSeconds)
{
    if (previewSlider_ == nullptr || !qIsFinite(deltaSeconds)) {
        return false;
    }
    const int deltaMs = qRound(deltaSeconds * 1000.0);
    if (deltaMs == 0) {
        return false;
    }
    const int value = qBound(
        previewSlider_->minimum(),
        previewSlider_->value() + deltaMs,
        previewSlider_->maximum()
    );
    if (value == previewSlider_->value()) {
        return true;
    }
    previewSlider_->setValue(value);
    return true;
}

bool VideoExportDialog::handlePreviewSliderWheel(QWheelEvent* event)
{
    if (previewSlider_ == nullptr || event == nullptr) {
        return false;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }
    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    previewSlider_->setFocus(Qt::MouseFocusReason);
    const bool handled = stepPreviewSliderBySeconds(
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void VideoExportDialog::beginPreviewHeldSeek(int direction, int key)
{
    if (direction == 0 || previewSlider_ == nullptr) {
        return;
    }
    previewHeldSeekDirection_ = direction > 0 ? 1 : -1;
    previewSeekHeldArrowKey_ = key;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.restart();
    if (previewHeldSeekTimer_ != nullptr && !previewHeldSeekTimer_->isActive()) {
        previewHeldSeekTimer_->start();
    }
}

void VideoExportDialog::stopPreviewHeldSeek(int key)
{
    if (key != 0 && previewSeekHeldArrowKey_ != key) {
        return;
    }
    previewHeldSeekDirection_ = 0;
    previewSeekHeldArrowKey_ = 0;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.invalidate();
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
}

void VideoExportDialog::applyPreviewHeldSeekTick()
{
    if (previewHeldSeekDirection_ == 0
        || previewSeekHeldArrowKey_ == 0
        || !previewSeekHeldArrowElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(previewSeekHeldArrowElapsed_.elapsed());
    const int deltaMs = previewSeekHeldArrowLastElapsedMs_ > 0
        ? (elapsedMs - previewSeekHeldArrowLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    previewSeekHeldArrowLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    stepPreviewSliderBySeconds(
        static_cast<double>(previewHeldSeekDirection_)
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds)
    );
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
        l10n(QStringLiteral("Export Video"), QStringLiteral("瀵煎嚭瑙嗛")),
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
    updated.audioBitrateKbps = normaliseAudioBitrateKbps(selectedAudioBitrateKbps_);
    updated.preset = selectedPreset_;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;
    updated.showObjectStatsHud = showObjectStatsCheck_ != nullptr ? showObjectStatsCheck_->isChecked() : false;
    updated.showChartInfoHud = showChartInfoCheck_ != nullptr ? showChartInfoCheck_->isChecked() : false;
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
    double exportTapFlowSpeed = selectedTapFlowSpeed_;
    if (tapFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = tapFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTapFlowSpeed = typedSpeed;
        }
    }
    double exportTouchFlowSpeed = selectedTouchFlowSpeed_;
    if (touchFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = touchFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTouchFlowSpeed = typedSpeed;
        }
    }
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    updated.tapFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTapFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    updated.touchFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTouchFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    const double selectedRangeStart = rangeStartSeconds();
    const double selectedRangeEnd = rangeEndSeconds();
    updated.exportStartSeconds = selectedRangeStart;
    updated.contentDurationSeconds = qMax(0.0, selectedRangeEnd - updated.exportStartSeconds);
    // Tolerate the spinbox / floating-point slop that prevented genuine
    // "Export All" selections (range == [0, totalDuration] up to the
    // dialog's 1 ms display precision) from being classified as
    // full-range. We treat anything within 0.01 s of the total as full.
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    updated.fullRangeExport =
        selectedRangeStart <= kFullRangeEpsilonSeconds
        && selectedRangeEnd + kFullRangeEpsilonSeconds >= totalDurationSeconds_;
    // The maimai intro is a full-export-only pre-roll; partial/clip exports
    // never get it regardless of the checkbox.
    updated.intro.enabled =
        (addIntroCheck_ != nullptr && addIntroCheck_->isChecked())
        && updated.fullRangeExport;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Export,
        QStringLiteral("dialog_range_decision"),
        QStringLiteral("rangeStart=%1 rangeEnd=%2 totalDuration=%3 fullRangeExport=%4 epsilon=%5")
            .arg(selectedRangeStart, 0, 'f', 9)
            .arg(selectedRangeEnd, 0, 'f', 9)
            .arg(totalDurationSeconds_, 0, 'f', 9)
            .arg(updated.fullRangeExport ? 1 : 0)
            .arg(kFullRangeEpsilonSeconds, 0, 'f', 6));

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
    refreshAddIntroEnabledState();
}

void VideoExportDialog::refreshAddIntroEnabledState()
{
    if (addIntroCheck_ == nullptr) {
        return;
    }
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    const bool fullRange =
        rangeStartSeconds() <= kFullRangeEpsilonSeconds
        && rangeEndSeconds() + kFullRangeEpsilonSeconds >= totalDurationSeconds_;
    addIntroCheck_->setEnabled(fullRange);
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

void VideoExportDialog::handlePreviewPlayPauseShortcut()
{
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        stopRangePreview(false);
        return;
    }
    toggleRangePreview();
}

void VideoExportDialog::handlePreviewStopOrPlayShortcut()
{
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        stopRangePreviewToStart();
        return;
    }
    toggleRangePreview();
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
    savePersistedSettings(task);
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
    if (event != nullptr && UiDialogs::dialogOwnsPreviewShortcutScope(this)) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const VideoExportShortcutAction action = matchVideoExportShortcut(keyEvent);
            if (action != VideoExportShortcutAction::None) {
                if (keyEvent->isAutoRepeat()) {
                    event->accept();
                    return true;
                }
                switch (action) {
                case VideoExportShortcutAction::TogglePlayPause:
                    handlePreviewPlayPauseShortcut();
                    break;
                case VideoExportShortcutAction::StopOrPlay:
                    handlePreviewStopOrPlayShortcut();
                    break;
                case VideoExportShortcutAction::None:
                    break;
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
    }
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::Wheel) {
            stopPreviewHeldSeek();
            if (handlePreviewSliderWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return QDialog::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                beginPreviewHeldSeek(direction, keyEvent->key());
                stepPreviewSliderBySeconds(
                    static_cast<double>(direction) * miacode::preview_interaction::kSeekSingleStepSeconds
                );
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
