#include "BatchVideoExportDialog.h"

#include "common/ChartAssetPaths.h"
#include "SimaiNativeParser.h"
#include "SimaiDocument.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QDir>
#include <QFileDialog>

#ifdef Q_OS_WIN
#include <QWindow>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtMath>

#include <functional>

namespace {

constexpr int kDialogMinWidth = 620;
constexpr int kFormLabelWidth = 72;
constexpr int kDialogActionButtonMinWidth = 92;
constexpr int kSliderValueLabelWidth = 44;

struct ResolutionPreset {
    int width = 1024;
    int height = 1024;
    const char* label = "1024x1024 (1:1)";
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {720, 720, "720x720 (1:1)"},
    {1024, 1024, "1024x1024 (1:1)"},
    {960, 720, "960x720 (4:3)"},
    {1280, 720, "1280x720 (16:9)"},
    {1080, 1080, "1080x1080 (1:1)"},
    {1440, 1080, "1440x1080 (4:3)"},
    {1920, 1080, "1920x1080 (16:9)"},
    {1440, 1440, "1440x1440 (1:1)"},
    {1920, 1440, "1920x1440 (4:3)"},
    {2560, 1440, "2560x1440 (16:9)"},
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

QString uiText(const char* key, const QString& fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? fallback : translated;
}

QString systemL10n(const QString& en, const QString& zh)
{
    const QLocale locale = QLocale::system();
    return locale.language() == QLocale::Chinese ? zh : en;
}

QSettings exportDialogSettingsStore()
{
    return QSettings(QStringLiteral("fanfaredash"), QStringLiteral("MiaCode"));
}

QString batchExportChartBrowseDirectoryKey()
{
    return QStringLiteral("last_chart_directory");
}

QString batchExportOutputBrowseDirectoryKey()
{
    return QStringLiteral("last_output_directory");
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
    case PreviewBackgroundScaleMode::InnerCircleFitOuterFill:
        return uiText(
            "dialog.video_export.option.scale.inner_circle_fit_outer_fill",
            QStringLiteral("Inner 1:1 Fit + Outer Fill"));
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    }
}

QString flowSpeedValueLabel(double flowSpeed)
{
    const double snapped = qRound(flowSpeed * 4.0) / 4.0;
    const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
    const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
    return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
}

QString exportBaseDirectory(const VideoExportTask& task)
{
    const QFileInfo chartInfo(task.chartPath);
    if (!chartInfo.absoluteDir().path().isEmpty()) {
        return chartInfo.absoluteDir().absolutePath();
    }
    return QDir::currentPath();
}

QString batchExportLineEditStyleSheet()
{
    const auto& c = UiTheme::colors();
    return QStringLiteral(
        "QLineEdit { min-height: 24px; padding: 0px 10px; border: 1px solid %1; border-radius: 8px; background: %2; color: %3; font-weight: 500; }"
        "QLineEdit:hover { border-color: %4; }"
        "QLineEdit:focus { border-color: %4; background: %2; }"
    )
        .arg(c.border.name(QColor::HexRgb))
        .arg(c.inputBg.name(QColor::HexRgb))
        .arg(c.textPrimary.name(QColor::HexRgb))
        .arg(c.accent.name(QColor::HexRgb));
}

QString batchDifficultyLabel(int difficultyId)
{
    return SimaiDocument::difficultyShortName(difficultyId);
}

QString preferredChartFilePathInDirectory(const QString& directoryPath)
{
    const QDir dir(directoryPath);
    const QStringList preferredNames{
        QStringLiteral("majdata.txt"),
        QStringLiteral("maidata.txt"),
        QStringLiteral("majdata.simai"),
        QStringLiteral("maidata.simai"),
    };
    for (const QString& name : preferredNames) {
        const QString candidate = dir.filePath(name);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

bool validateBatchChartDirectory(const QString& directoryPath, QString* chartPath, QString* errorMessage)
{
    if (chartPath != nullptr) {
        chartPath->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.invalid_folder",
                QStringLiteral("The selected path is not a valid folder.")
            );
        }
        return false;
    }

    const QString resolvedChartPath = preferredChartFilePathInDirectory(directoryInfo.absoluteFilePath());
    if (resolvedChartPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.missing_chart_file",
                QStringLiteral("Missing majdata.txt (or maidata.txt).")
            );
        }
        return false;
    }

    if (miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath()).isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.missing_track_file",
                QStringLiteral("Missing track.mp3.")
            );
        }
        return false;
    }

    if (chartPath != nullptr) {
        *chartPath = resolvedChartPath;
    }
    return true;
}

QToolButton* createDialogMenuButton(QWidget* parent, const QString& text)
{
    auto* button = new QToolButton(parent);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    button->setText(text);
    return button;
}

void addDialogMenuChoice(
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
}

#ifdef Q_OS_WIN
// Windows native multi-folder picker via IFileOpenDialog. The shell
// supports FOS_PICKFOLDERS + FOS_ALLOWMULTISELECT (Win Vista+), giving
// the user the same Explorer-style chooser they get elsewhere in the
// OS with Ctrl/Shift selection. Qt's QFileDialog in native mode can't
// do this directly (the platform plugin maps Directory mode to
// SHBrowseForFolder-style picker which is single-select); falling back
// to the COM API is the supported route.
QStringList selectMultipleDirectoriesNativeWin(QWidget* parent, const QString& startDirectory)
{
    QStringList result;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        return result;
    }
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(
            options
            | FOS_PICKFOLDERS
            | FOS_ALLOWMULTISELECT
            | FOS_PATHMUSTEXIST
            | FOS_FORCEFILESYSTEM
            | FOS_NOCHANGEDIR);
    }
    const QString trimmedStart = startDirectory.trimmed();
    if (!trimmedStart.isEmpty()) {
        const QString native = QDir::toNativeSeparators(QDir::cleanPath(trimmedStart));
        IShellItem* startItem = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                reinterpret_cast<LPCWSTR>(native.utf16()),
                nullptr,
                IID_PPV_ARGS(&startItem)))
            && startItem != nullptr) {
            dialog->SetFolder(startItem);
            startItem->Release();
        }
    }
    const QString title = uiText(
        "dialog.batch_export.select_charts",
        QStringLiteral("Select Chart Folders"));
    dialog->SetTitle(reinterpret_cast<LPCWSTR>(title.utf16()));

    HWND parentHwnd = nullptr;
    if (parent != nullptr) {
        if (QWidget* topLevel = parent->window(); topLevel != nullptr) {
            topLevel->createWinId();
            if (QWindow* handle = topLevel->windowHandle(); handle != nullptr) {
                parentHwnd = reinterpret_cast<HWND>(handle->winId());
            }
        }
    }
    hr = dialog->Show(parentHwnd);
    if (SUCCEEDED(hr)) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr) {
            DWORD count = 0;
            if (SUCCEEDED(items->GetCount(&count))) {
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item != nullptr) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))
                            && path != nullptr) {
                            result.append(QString::fromWCharArray(path));
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
            }
            items->Release();
        }
    }
    dialog->Release();
    return result;
}
#endif  // Q_OS_WIN

QStringList selectMultipleDirectories(QWidget* parent, const QString& startDirectory)
{
#ifdef Q_OS_WIN
    // Prefer the OS-native Explorer-style picker when available. Falls
    // through to the Qt-driven dialog if the shell call fails (e.g.
    // the COM init somehow isn't apartment-threaded), so the user
    // never ends up with no picker at all.
    QStringList nativeResult = selectMultipleDirectoriesNativeWin(parent, startDirectory);
    if (!nativeResult.isEmpty()) {
        return nativeResult;
    }
    // Note: an empty result here can also mean "user cancelled" — we
    // can't distinguish that from "shell call failed" without tracking
    // the HRESULT. Treat empty as cancel and skip the Qt fallback, so
    // closing the native dialog doesn't pop a second one.
    return nativeResult;
#else
    QFileDialog dialog(parent, uiText("dialog.batch_export.select_charts", QStringLiteral("Select Chart Folders")), startDirectory);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    for (QListView* view : dialog.findChildren<QListView*>()) {
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    for (QTreeView* view : dialog.findChildren<QTreeView*>()) {
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    return dialog.selectedFiles();
#endif
}

QWidget* createSliderOption(
    QWidget* parent,
    const QString& title,
    int valuePercent,
    QSlider** sliderOut,
    QLabel** valueLabelOut
)
{
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, container);
    auto* slider = new QSlider(Qt::Horizontal, container);
    slider->setRange(0, 100);
    slider->setSingleStep(1);
    slider->setPageStep(1);
    slider->setTickInterval(1);
    slider->setValue(valuePercent);
    slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
    auto* valueLabel = new miacode::ui::EditableValueLabel(QStringLiteral("%1%").arg(valuePercent), container);
    valueLabel->setMinimumWidth(kSliderValueLabelWidth);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->bindSlider(slider);

    layout->addWidget(titleLabel, 0);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel, 0);

    if (sliderOut != nullptr) {
        *sliderOut = slider;
    }
    if (valueLabelOut != nullptr) {
        *valueLabelOut = valueLabel;
    }
    return container;
}

}

BatchVideoExportDialog::BatchVideoExportDialog(
    const VideoExportTask& baseTask,
    const QString& difficultyLabel,
    SharedSettingsChangedCallback sharedSettingsChangedCallback,
    QWidget* parent
)
    : QDialog(parent)
    , baseTask_(baseTask)
    , difficultyLabel_(difficultyLabel)
    , sharedSettingsChangedCallback_(std::move(sharedSettingsChangedCallback))
    , requestedTask_(baseTask)
    , selectedResolution_(QSize(qMax(1, baseTask.outputWidth), qMax(1, baseTask.outputHeight)))
    , selectedFps_(baseTask.fps >= 90 ? 120 : 60)
    , selectedAudioBitrateKbps_(baseTask.audioBitrateKbps)
    , selectedPreset_(baseTask.preset)
    , selectedBackgroundScaleMode_(baseTask.backgroundScaleMode)
    , selectedTapFlowSpeed_(baseTask.tapFlowSpeed)
    , selectedTouchFlowSpeed_(baseTask.touchFlowSpeed)
{
    setWindowTitle(uiText("dialog.batch_export.title", QStringLiteral("Batch Export")));
    setModal(true);
    setMinimumWidth(kDialogMinWidth);
    setStyleSheet(UiTheme::exportDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    auto* topForm = new QGridLayout();
    topForm->setHorizontalSpacing(10);
    topForm->setVerticalSpacing(8);
    int row = 0;

    auto* difficultyLabelTitle = new QLabel(uiText("dialog.batch_export.difficulty", QStringLiteral("Difficulty")), this);
    difficultyLabelTitle->setFixedWidth(kFormLabelWidth);
    auto* difficultySelector = new QWidget(this);
    auto* difficultyLayout = new QHBoxLayout(difficultySelector);
    difficultyLayout->setContentsMargins(0, 0, 0, 0);
    difficultyLayout->setSpacing(10);
    for (int difficultyId = 1; difficultyId <= 7; ++difficultyId) {
        auto* check = new QCheckBox(batchDifficultyLabel(difficultyId), difficultySelector);
        check->setChecked(batchDifficultyLabel(difficultyId) == difficultyLabel_);
        difficultyChecks_.append(check);
        difficultyIds_.append(difficultyId);
        difficultyLayout->addWidget(check, 0);
    }
    difficultyLayout->addStretch(1);
    topForm->addWidget(difficultyLabelTitle, row, 0);
    topForm->addWidget(difficultySelector, row, 1, 1, 2);
    ++row;

    auto* outputLabel = new QLabel(uiText("dialog.batch_export.output_dir", QStringLiteral("Output Folder")), this);
    outputLabel->setFixedWidth(kFormLabelWidth);
    outputDirectoryEdit_ = new QLineEdit(this);
    const QString rememberedOutputDirectory = lastOutputBrowseDirectory();
    const QString initialOutputDirectory = rememberedOutputDirectory.trimmed().isEmpty()
        ? exportBaseDirectory(baseTask_)
        : rememberedOutputDirectory;
    outputDirectoryEdit_->setText(QDir::toNativeSeparators(initialOutputDirectory));
    outputDirectoryEdit_->setStyleSheet(batchExportLineEditStyleSheet());
    auto* outputBrowseButton = new QPushButton(uiText("action.browse", QStringLiteral("Browse...")), this);
    outputBrowseButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    connect(outputBrowseButton, &QPushButton::clicked, this, &BatchVideoExportDialog::browseOutputDirectory);
    topForm->addWidget(outputLabel, row, 0);
    topForm->addWidget(outputDirectoryEdit_, row, 1);
    topForm->addWidget(outputBrowseButton, row, 2);
    ++row;

    auto* resolutionLabel = new QLabel(uiText("dialog.video_export.resolution", QStringLiteral("Resolution")), this);
    resolutionLabel->setFixedWidth(kFormLabelWidth);
    resolutionButton_ = createDialogMenuButton(this, exportDialogResolutionLabel(selectedResolution_));
    resolutionMenu_ = new QMenu(resolutionButton_);
    UiTheme::styleRoundedMenu(*resolutionMenu_);
    resolutionButton_->setMenu(resolutionMenu_);
    for (const ResolutionPreset& preset : kResolutionPresets) {
        addDialogMenuChoice(resolutionMenu_, QString::fromLatin1(preset.label), [this, preset]() {
            selectedResolution_ = QSize(preset.width, preset.height);
            if (resolutionButton_ != nullptr) {
                resolutionButton_->setText(QString::fromLatin1(preset.label));
            }
            persistExportOnlySettings();
        });
    }
    topForm->addWidget(resolutionLabel, row, 0);
    topForm->addWidget(resolutionButton_, row, 1, 1, 2);
    ++row;

    auto* fpsLabel = new QLabel(uiText("dialog.video_export.fps", QStringLiteral("FPS")), this);
    fpsLabel->setFixedWidth(kFormLabelWidth);
    fpsButton_ = createDialogMenuButton(this, QStringLiteral("%1 FPS").arg(selectedFps_));
    fpsMenu_ = new QMenu(fpsButton_);
    UiTheme::styleRoundedMenu(*fpsMenu_);
    fpsButton_->setMenu(fpsMenu_);
    for (int fps : kFpsOptions) {
        addDialogMenuChoice(fpsMenu_, QStringLiteral("%1 FPS").arg(fps), [this, fps]() {
            selectedFps_ = fps;
            if (fpsButton_ != nullptr) {
                fpsButton_->setText(QStringLiteral("%1 FPS").arg(selectedFps_));
            }
            persistExportOnlySettings();
        });
    }
    topForm->addWidget(fpsLabel, row, 0);
    topForm->addWidget(fpsButton_, row, 1, 1, 2);
    ++row;

    // Audio quality dropdown — drives ffmpeg `-b:a <kbps>k`. Mirrors
    // the single-export VideoExportDialog implementation; both dialogs
    // load/save through the same dialog-preferences JSON object.
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(selectedAudioBitrateKbps_);
    auto* audioBitrateLabel = new QLabel(uiText("dialog.video_export.audio_bitrate", QStringLiteral("Audio quality")), this);
    audioBitrateLabel->setFixedWidth(kFormLabelWidth);
    audioBitrateButton_ = createDialogMenuButton(this, QStringLiteral("%1 kbps").arg(selectedAudioBitrateKbps_));
    audioBitrateMenu_ = new QMenu(audioBitrateButton_);
    UiTheme::styleRoundedMenu(*audioBitrateMenu_);
    audioBitrateButton_->setMenu(audioBitrateMenu_);
    for (int kbps : kAudioBitrateOptionsKbps) {
        addDialogMenuChoice(audioBitrateMenu_, QStringLiteral("%1 kbps").arg(kbps), [this, kbps]() {
            selectedAudioBitrateKbps_ = kbps;
            if (audioBitrateButton_ != nullptr) {
                audioBitrateButton_->setText(QStringLiteral("%1 kbps").arg(selectedAudioBitrateKbps_));
            }
            persistExportOnlySettings();
        });
    }
    topForm->addWidget(audioBitrateLabel, row, 0);
    topForm->addWidget(audioBitrateButton_, row, 1, 1, 2);
    ++row;

    auto* presetLabel = new QLabel(uiText("dialog.video_export.preset", QStringLiteral("Export Quality")), this);
    presetLabel->setFixedWidth(kFormLabelWidth);
    presetButton_ = createDialogMenuButton(this, exportDialogPresetLabel(selectedPreset_));
    presetMenu_ = new QMenu(presetButton_);
    UiTheme::styleRoundedMenu(*presetMenu_);
    presetButton_->setMenu(presetMenu_);
    addDialogMenuChoice(presetMenu_, uiText("dialog.video_export.preset.fast", QStringLiteral("Fast")), [this]() {
        selectedPreset_ = VideoExportPreset::Fast;
        if (presetButton_ != nullptr) {
            presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
        }
        persistExportOnlySettings();
    });
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
    topForm->addWidget(presetLabel, row, 0);
    topForm->addWidget(presetButton_, row, 1, 1, 2);
    ++row;

    rootLayout->addLayout(topForm);

    auto* chartLabel = new QLabel(uiText("dialog.batch_export.chart_folders", QStringLiteral("Chart Folders")), this);
    rootLayout->addWidget(chartLabel);

    auto* chartSectionLayout = new QHBoxLayout();
    chartSectionLayout->setSpacing(8);
    chartDirectoryList_ = new QListWidget(this);
    chartDirectoryList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    chartDirectoryList_->setMinimumHeight(180);
    chartDirectoryList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    chartSectionLayout->addWidget(chartDirectoryList_, 1);

    auto* chartButtonsLayout = new QVBoxLayout();
    chartButtonsLayout->setSpacing(6);
    auto* addButton = new QPushButton(uiText("dialog.batch_export.add_folders", QStringLiteral("Add Folders")), this);
    addButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    connect(addButton, &QPushButton::clicked, this, &BatchVideoExportDialog::browseChartDirectories);
    chartButtonsLayout->addWidget(addButton);
    auto* removeButton = new QPushButton(uiText("dialog.batch_export.remove_selected", QStringLiteral("Remove Selected")), this);
    removeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    connect(removeButton, &QPushButton::clicked, this, &BatchVideoExportDialog::removeSelectedChartDirectories);
    chartButtonsLayout->addWidget(removeButton);
    auto* clearButton = new QPushButton(uiText("dialog.batch_export.clear", QStringLiteral("Clear")), this);
    clearButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    connect(clearButton, &QPushButton::clicked, this, &BatchVideoExportDialog::clearChartDirectories);
    chartButtonsLayout->addWidget(clearButton);
    chartButtonsLayout->addStretch(1);
    chartSectionLayout->addLayout(chartButtonsLayout);
    rootLayout->addLayout(chartSectionLayout);

    auto* optionsCard = new QFrame(this);
    optionsCard->setObjectName(QStringLiteral("SettingsCard"));
    auto* optionsLayout = new QGridLayout(optionsCard);
    optionsLayout->setContentsMargins(12, 12, 12, 12);
    optionsLayout->setHorizontalSpacing(10);
    optionsLayout->setVerticalSpacing(8);
    optionsLayout->setColumnStretch(1, 1);
    optionsLayout->setColumnStretch(3, 1);
    int optionRow = 0;

    auto* checkboxRow = new QWidget(optionsCard);
    auto* checkboxLayout = new QHBoxLayout(checkboxRow);
    checkboxLayout->setContentsMargins(0, 0, 0, 0);
    checkboxLayout->setSpacing(14);
    showTimestampCheck_ = new QCheckBox(uiText("dialog.video_export.option.show_timestamp", QStringLiteral("Show timestamp")), optionsCard);
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    showObjectStatsCheck_ = new QCheckBox(uiText("dialog.video_export.option.show_object_stats", QStringLiteral("Show object stats")), optionsCard);
    showObjectStatsCheck_->setChecked(baseTask_.showObjectStatsHud);
    showChartInfoCheck_ = new QCheckBox(uiText("dialog.video_export.option.show_chart_info", QStringLiteral("Show chart info")), optionsCard);
    showChartInfoCheck_->setChecked(baseTask_.showChartInfoHud);
    addIntroCheck_ = new QCheckBox(
        UiText::localized(QStringLiteral("Add intro"), QStringLiteral("添加片头")),
        optionsCard);
    addIntroCheck_->setChecked(baseTask_.intro.enabled);
    addIntroCheck_->setToolTip(UiText::localized(QStringLiteral("Prepend the maimai track-start intro to each export."), QStringLiteral("在每个视频开头加入 maimai 风格片头（批量导出整谱）。")));
    smoothBrightnessCheck_ = new QCheckBox(uiText("dialog.video_export.option.smooth_brightness", QStringLiteral("Smooth brightness")), optionsCard);
    smoothBrightnessCheck_->setChecked(baseTask_.smoothBrightness);
    checkboxLayout->addWidget(showTimestampCheck_, 0);
    checkboxLayout->addWidget(showObjectStatsCheck_, 0);
    checkboxLayout->addWidget(showChartInfoCheck_, 0);
    checkboxLayout->addWidget(addIntroCheck_, 0);
    checkboxLayout->addWidget(smoothBrightnessCheck_, 0);
    checkboxLayout->addStretch(1);
    optionsLayout->addWidget(checkboxRow, optionRow, 0, 1, 4);
    ++optionRow;

    QWidget* outerBrightnessOption = createSliderOption(
        optionsCard,
        uiText("dialog.video_export.option.brightness_outer", QStringLiteral("Brightness (Outer)")),
        qRound(qBound(0.0, baseTask_.backgroundBrightnessOuter, 1.0) * 100.0),
        &brightnessOuterSlider_,
        &brightnessOuterValueLabel_
    );
    optionsLayout->addWidget(outerBrightnessOption, optionRow, 0, 1, 4);
    ++optionRow;

    QWidget* innerBrightnessOption = createSliderOption(
        optionsCard,
        uiText("dialog.video_export.option.brightness_inner", QStringLiteral("Brightness (Inner)")),
        qRound(qBound(0.0, baseTask_.backgroundBrightnessInner, 1.0) * 100.0),
        &brightnessInnerSlider_,
        &brightnessInnerValueLabel_
    );
    optionsLayout->addWidget(innerBrightnessOption, optionRow, 0, 1, 4);
    ++optionRow;

    QWidget* judgeLineOption = createSliderOption(
        optionsCard,
        uiText("dialog.video_export.option.layout_size", QStringLiteral("Stage Display Scale")),
        qRound(miacode::preview_video::normalizedLayoutSquareScale(baseTask_.layoutSquareScale) * 100.0),
        &layoutSquareScaleSlider_,
        &layoutSquareScaleValueLabel_
    );
    layoutSquareScaleSlider_->setRange(50, 150);
    layoutSquareScaleSlider_->setSingleStep(1);
    layoutSquareScaleSlider_->setPageStep(1);
    optionsLayout->addWidget(judgeLineOption, optionRow, 0, 1, 4);
    ++optionRow;

    auto* scaleModeLabel = new QLabel(uiText("dialog.video_export.option.scale_mode", QStringLiteral("Background / PV Scale")), optionsCard);
    backgroundScaleModeButton_ = createDialogMenuButton(optionsCard, exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
    backgroundScaleModeMenu_ = new QMenu(backgroundScaleModeButton_);
    UiTheme::styleRoundedMenu(*backgroundScaleModeMenu_);
    backgroundScaleModeButton_->setMenu(backgroundScaleModeMenu_);
    addDialogMenuChoice(backgroundScaleModeMenu_, exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode::FillCrop), [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        notifySharedSettingsChanged();
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode::FitContain), [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        notifySharedSettingsChanged();
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode::SquareFitContain), [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::SquareFitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        notifySharedSettingsChanged();
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode::InnerCircleFitOuterFill), [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::InnerCircleFitOuterFill;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        notifySharedSettingsChanged();
    });
    optionsLayout->addWidget(scaleModeLabel, optionRow, 0);
    optionsLayout->addWidget(backgroundScaleModeButton_, optionRow, 1, 1, 3);
    ++optionRow;

    const auto createFlowSpeedEdit = [this, optionsCard](QLineEdit** editOut, double* selectedFlowSpeed) {
        auto* flowSpeedEdit = new QLineEdit(optionsCard);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
        flowSpeedEdit->setStyleSheet(batchExportLineEditStyleSheet());
        auto* flowValidator = new QDoubleValidator(
            miacode::preview_gameplay::kPreviewTimingFlowSpeedMin,
            miacode::preview_gameplay::kPreviewTimingFlowSpeedMax,
            2,
            flowSpeedEdit
        );
        flowValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, this, [this, flowSpeedEdit, selectedFlowSpeed]() {
            if (flowSpeedEdit == nullptr || selectedFlowSpeed == nullptr) {
                return;
            }
            bool ok = false;
            const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
            if (!ok) {
                flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
                return;
            }
            *selectedFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(typedSpeed);
            flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
            notifySharedSettingsChanged();
        });
        if (editOut != nullptr) {
            *editOut = flowSpeedEdit;
        }
        return flowSpeedEdit;
    };
    auto* tapFlowSpeedLabel = new QLabel(
        uiText("dialog.video_export.option.tap_flow_speed", QStringLiteral("Tap Flow Speed")),
        optionsCard
    );
    tapFlowSpeedEdit_ = createFlowSpeedEdit(&tapFlowSpeedEdit_, &selectedTapFlowSpeed_);
    auto* touchFlowSpeedLabel = new QLabel(
        uiText("dialog.video_export.option.touch_flow_speed", QStringLiteral("Touch Flow Speed")),
        optionsCard
    );
    touchFlowSpeedEdit_ = createFlowSpeedEdit(&touchFlowSpeedEdit_, &selectedTouchFlowSpeed_);
    optionsLayout->addWidget(tapFlowSpeedLabel, optionRow, 0);
    optionsLayout->addWidget(tapFlowSpeedEdit_, optionRow, 1);
    optionsLayout->addWidget(touchFlowSpeedLabel, optionRow, 2);
    optionsLayout->addWidget(touchFlowSpeedEdit_, optionRow, 3);

    connect(brightnessOuterSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessOuterValueLabel_ != nullptr) {
            brightnessOuterValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        notifySharedSettingsChanged();
    });
    connect(brightnessInnerSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessInnerValueLabel_ != nullptr) {
            brightnessInnerValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        notifySharedSettingsChanged();
    });
    connect(layoutSquareScaleSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (layoutSquareScaleValueLabel_ != nullptr) {
            layoutSquareScaleValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        notifySharedSettingsChanged();
    });
    connect(showTimestampCheck_, &QCheckBox::toggled, this, [this](bool) {
        notifySharedSettingsChanged();
    });
    connect(showObjectStatsCheck_, &QCheckBox::toggled, this, [this](bool) {
        notifySharedSettingsChanged();
    });
    connect(showChartInfoCheck_, &QCheckBox::toggled, this, [this](bool) {
        notifySharedSettingsChanged();
    });
    connect(addIntroCheck_, &QCheckBox::toggled, this, [this](bool) {
        notifySharedSettingsChanged();
    });
    connect(smoothBrightnessCheck_, &QCheckBox::toggled, this, [this](bool) {
        notifySharedSettingsChanged();
    });

    rootLayout->addWidget(optionsCard);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportButton = new QPushButton(uiText("dialog.video_export.button.export", QStringLiteral("Export")), this);
    exportButton->setMinimumWidth(kDialogActionButtonMinWidth);
    exportButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    buttons->addButton(exportButton, QDialogButtonBox::AcceptRole);
    if (QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel); cancelButton != nullptr) {
        cancelButton->setMinimumWidth(kDialogActionButtonMinWidth);
        cancelButton->setText(systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")));
        cancelButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        connect(cancelButton, &QPushButton::clicked, this, &BatchVideoExportDialog::reject);
    }
    connect(exportButton, &QPushButton::clicked, this, &BatchVideoExportDialog::startExport);
    rootLayout->addWidget(buttons);

    loadPersistedSettings();
}

QStringList BatchVideoExportDialog::selectedChartDirectories() const
{
    QStringList directories;
    for (int i = 0; i < chartDirectoryList_->count(); ++i) {
        QListWidgetItem* item = chartDirectoryList_->item(i);
        if (item == nullptr) {
            continue;
        }
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.trimmed().isEmpty()) {
            directories.append(path);
        }
    }
    return directories;
}

QString BatchVideoExportDialog::outputDirectory() const
{
    return QDir::cleanPath(QDir::fromNativeSeparators(outputDirectoryEdit_->text().trimmed()));
}

QList<int> BatchVideoExportDialog::selectedDifficultyIds() const
{
    QList<int> result;
    const int count = qMin(difficultyChecks_.size(), difficultyIds_.size());
    for (int i = 0; i < count; ++i) {
        if (difficultyChecks_.at(i) != nullptr && difficultyChecks_.at(i)->isChecked()) {
            result.append(difficultyIds_.at(i));
        }
    }
    return result;
}

void BatchVideoExportDialog::addChartDirectories(const QStringList& directories)
{
    QSet<QString> existing;
    for (int i = 0; i < chartDirectoryList_->count(); ++i) {
        if (QListWidgetItem* item = chartDirectoryList_->item(i); item != nullptr) {
            existing.insert(item->data(Qt::UserRole).toString());
        }
    }

    QStringList invalidDescriptions;
    QString firstValidDirectory;

    for (const QString& directory : directories) {
        const QString normalized = QDir::cleanPath(directory.trimmed());
        if (normalized.isEmpty() || existing.contains(normalized)) {
            continue;
        }
        QString resolvedChartPath;
        QString errorMessage;
        if (!validateBatchChartDirectory(normalized, &resolvedChartPath, &errorMessage)) {
            invalidDescriptions.append(
                QStringLiteral("%1 - %2")
                    .arg(QDir::toNativeSeparators(normalized), errorMessage)
            );
            continue;
        }
        existing.insert(normalized);
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(normalized), chartDirectoryList_);
        item->setData(Qt::UserRole, normalized);
        item->setData(Qt::UserRole + 1, resolvedChartPath);
        if (firstValidDirectory.isEmpty()) {
            firstValidDirectory = normalized;
        }
    }

    refreshChartDirectoryNumbering();

    if (!firstValidDirectory.isEmpty()) {
        saveLastChartBrowseDirectory(firstValidDirectory);
    }
    if (!invalidDescriptions.isEmpty()) {
        QMessageBox::warning(
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText(
                "dialog.batch_export.error.invalid_selection",
                QStringLiteral("Some folders were skipped because required files are missing.")
            ) + QStringLiteral("\n\n") + invalidDescriptions.join(QLatin1Char('\n'))
        );
    }
}

void BatchVideoExportDialog::browseChartDirectories()
{
    QString startDir = lastChartBrowseDirectory();
    if (startDir.trimmed().isEmpty()) {
        startDir = outputDirectory();
    }
    if (startDir.trimmed().isEmpty()) {
        startDir = exportBaseDirectory(baseTask_);
    }
    addChartDirectories(selectMultipleDirectories(this, startDir));
}

void BatchVideoExportDialog::browseOutputDirectory()
{
    QString startDir = lastOutputBrowseDirectory();
    if (startDir.trimmed().isEmpty()) {
        startDir = outputDirectory();
    }
    const QString selected = QFileDialog::getExistingDirectory(
        this,
        uiText("dialog.batch_export.output_dir", QStringLiteral("Output Folder")),
        startDir.trimmed().isEmpty() ? exportBaseDirectory(baseTask_) : startDir
    );
    if (!selected.trimmed().isEmpty()) {
        const QString normalized = QDir::cleanPath(selected);
        outputDirectoryEdit_->setText(QDir::toNativeSeparators(normalized));
        saveLastOutputBrowseDirectory(normalized);
    }
}

void BatchVideoExportDialog::removeSelectedChartDirectories()
{
    qDeleteAll(chartDirectoryList_->selectedItems());
    refreshChartDirectoryNumbering();
}

void BatchVideoExportDialog::clearChartDirectories()
{
    chartDirectoryList_->clear();
}

void BatchVideoExportDialog::refreshChartDirectoryNumbering()
{
    if (chartDirectoryList_ == nullptr) {
        return;
    }
    for (int i = 0; i < chartDirectoryList_->count(); ++i) {
        QListWidgetItem* item = chartDirectoryList_->item(i);
        if (item == nullptr) {
            continue;
        }
        const QString path = item->data(Qt::UserRole).toString();
        item->setText(QStringLiteral("[%1] %2")
                          .arg(i + 1)
                          .arg(QDir::toNativeSeparators(path)));
    }
}

bool BatchVideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("task is null");
        }
        return false;
    }

    bool tapFlowSpeedOk = false;
    const double tapFlowSpeed = tapFlowSpeedEdit_->text().trimmed().toDouble(&tapFlowSpeedOk);
    bool touchFlowSpeedOk = false;
    const double touchFlowSpeed = touchFlowSpeedEdit_->text().trimmed().toDouble(&touchFlowSpeedOk);
    if (!tapFlowSpeedOk || !touchFlowSpeedOk) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.invalid_flow_speed", QStringLiteral("Flow speed is invalid."));
        }
        return false;
    }

    task->outputWidth = selectedResolution_.width();
    task->outputHeight = selectedResolution_.height();
    task->fps = selectedFps_;
    task->audioBitrateKbps = normaliseAudioBitrateKbps(selectedAudioBitrateKbps_);
    task->preset = selectedPreset_;
    task->showTimestamp = showTimestampCheck_ != nullptr && showTimestampCheck_->isChecked();
    task->showObjectStatsHud = showObjectStatsCheck_ != nullptr && showObjectStatsCheck_->isChecked();
    task->showChartInfoHud = showChartInfoCheck_ != nullptr && showChartInfoCheck_->isChecked();
    task->intro.enabled = addIntroCheck_ != nullptr && addIntroCheck_->isChecked();
    task->smoothBrightness = smoothBrightnessCheck_ != nullptr && smoothBrightnessCheck_->isChecked();
    task->backgroundBrightnessOuter = qBound(0.0, brightnessOuterSlider_->value() / 100.0, 1.0);
    task->backgroundBrightnessInner = qBound(0.0, brightnessInnerSlider_->value() / 100.0, 1.0);
    task->layoutSquareScale = miacode::preview_video::normalizedLayoutSquareScale(layoutSquareScaleSlider_->value() / 100.0);
    task->backgroundScaleMode = selectedBackgroundScaleMode_;
    task->tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(tapFlowSpeed);
    task->touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(touchFlowSpeed);
    return true;
}

void BatchVideoExportDialog::startExport()
{
    if (selectedDifficultyIds().isEmpty()) {
        QMessageBox::warning(
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.no_difficulties", QStringLiteral("Please select at least one difficulty."))
        );
        return;
    }

    if (selectedChartDirectories().isEmpty()) {
        QMessageBox::warning(
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.no_chart_dirs", QStringLiteral("Please add at least one chart folder."))
        );
        return;
    }

    const QString outputDir = outputDirectory();
    if (outputDir.trimmed().isEmpty()) {
        QMessageBox::warning(
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.no_output_dir", QStringLiteral("Please choose an output folder."))
        );
        return;
    }

    VideoExportTask task = baseTask_;
    QString errorMessage;
    if (!applyUiToTask(&task, &errorMessage)) {
        QMessageBox::warning(
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            errorMessage
        );
        return;
    }

    requestedTask_ = task;
    saveLastOutputBrowseDirectory(outputDir);
    savePersistedSettings(task);
    exportRequested_ = true;
    accept();
}

QSize BatchVideoExportDialog::selectedResolution() const
{
    return selectedResolution_;
}

QString BatchVideoExportDialog::lastChartBrowseDirectory() const
{
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    const QString value = settings.value(batchExportChartBrowseDirectoryKey()).toString().trimmed();
    settings.endGroup();
    return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

QString BatchVideoExportDialog::lastOutputBrowseDirectory() const
{
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    const QString value = settings.value(batchExportOutputBrowseDirectoryKey()).toString().trimmed();
    settings.endGroup();
    return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

void BatchVideoExportDialog::saveLastChartBrowseDirectory(const QString& directory) const
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    settings.setValue(batchExportChartBrowseDirectoryKey(), QDir::cleanPath(directory));
    settings.endGroup();
}

void BatchVideoExportDialog::saveLastOutputBrowseDirectory(const QString& directory) const
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    settings.setValue(batchExportOutputBrowseDirectoryKey(), QDir::cleanPath(directory));
    settings.endGroup();
}

void BatchVideoExportDialog::loadPersistedSettings()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();

    const int savedWidth = settings.value(QStringLiteral("resolution_width")).toInt(selectedResolution_.width());
    const int savedHeight = settings.value(QStringLiteral("resolution_height")).toInt(selectedResolution_.height());
    if (savedWidth > 0 && savedHeight > 0) {
        selectedResolution_ = QSize(savedWidth, savedHeight);
        if (resolutionButton_ != nullptr) {
            resolutionButton_->setText(exportDialogResolutionLabel(selectedResolution_));
        }
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

void BatchVideoExportDialog::savePersistedSettings(const VideoExportTask& task) const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), task.outputWidth);
    settings.insert(QStringLiteral("resolution_height"), task.outputHeight);
    settings.insert(QStringLiteral("fps"), task.fps);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), task.audioBitrateKbps);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(task.preset));
    miacode::video_export::saveDialogPreferences(settings);
}

void BatchVideoExportDialog::persistExportOnlySettings() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), selectedResolution_.width());
    settings.insert(QStringLiteral("resolution_height"), selectedResolution_.height());
    settings.insert(QStringLiteral("fps"), selectedFps_);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), selectedAudioBitrateKbps_);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(selectedPreset_));
    miacode::video_export::saveDialogPreferences(settings);
}

void BatchVideoExportDialog::notifySharedSettingsChanged()
{
    if (sharedSettingsChangedCallback_) {
        sharedSettingsChangedCallback_(currentSharedSettingsTask());
    }
}

VideoExportTask BatchVideoExportDialog::currentSharedSettingsTask() const
{
    VideoExportTask task = baseTask_;
    task.preset = selectedPreset_;
    task.showTimestamp = showTimestampCheck_ != nullptr && showTimestampCheck_->isChecked();
    task.showObjectStatsHud = showObjectStatsCheck_ != nullptr && showObjectStatsCheck_->isChecked();
    task.showChartInfoHud = showChartInfoCheck_ != nullptr && showChartInfoCheck_->isChecked();
    task.intro.enabled = addIntroCheck_ != nullptr && addIntroCheck_->isChecked();
    task.smoothBrightness = smoothBrightnessCheck_ != nullptr && smoothBrightnessCheck_->isChecked();
    task.backgroundBrightnessOuter =
        brightnessOuterSlider_ != nullptr ? qBound(0.0, brightnessOuterSlider_->value() / 100.0, 1.0)
                                          : task.backgroundBrightnessOuter;
    task.backgroundBrightnessInner =
        brightnessInnerSlider_ != nullptr ? qBound(0.0, brightnessInnerSlider_->value() / 100.0, 1.0)
                                          : task.backgroundBrightnessInner;
    task.layoutSquareScale =
        layoutSquareScaleSlider_ != nullptr
        ? miacode::preview_video::normalizedLayoutSquareScale(layoutSquareScaleSlider_->value() / 100.0)
        : task.layoutSquareScale;
    task.backgroundScaleMode = selectedBackgroundScaleMode_;
    task.tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(selectedTapFlowSpeed_);
    task.touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(selectedTouchFlowSpeed_);
    return task;
}
