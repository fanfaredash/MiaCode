#include "BatchExportPanel.h"

#include "SimaiDocument.h"
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "tools/video_export/BatchExportTaskLayout.h"
#include "tools/video_export/VideoExportDialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTreeView>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <QWindow>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

#include <utility>

namespace {

QSettings exportDialogSettingsStore()
{
    return QSettings(QStringLiteral("fanfaredash"), QStringLiteral("MiaCode"));
}

QString exportBaseDirectory(const VideoExportTask& task)
{
    const QFileInfo chartInfo(task.chartPath);
    return chartInfo.absoluteDir().path().isEmpty()
        ? QDir::currentPath()
        : chartInfo.absoluteDir().absolutePath();
}

QString preferredChartFilePathInDirectory(const QString& directoryPath)
{
    const QDir directory(directoryPath);
    for (const QString& name : {QStringLiteral("majdata.txt"), QStringLiteral("maidata.txt"),
                                QStringLiteral("majdata.simai"), QStringLiteral("maidata.simai")}) {
        const QString candidate = directory.filePath(name);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
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
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.invalid_folder"));
        }
        return false;
    }
    const QString resolvedChartPath = preferredChartFilePathInDirectory(directoryInfo.absoluteFilePath());
    if (resolvedChartPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.missing_chart_file"));
        }
        return false;
    }
    if (miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath()).isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.missing_track_file"));
        }
        return false;
    }
    if (chartPath != nullptr) {
        *chartPath = resolvedChartPath;
    }
    return true;
}

#ifdef Q_OS_WIN
QStringList selectMultipleDirectoriesNativeWin(QWidget* parent, const QString& startDirectory)
{
    QStringList result;
    IFileOpenDialog* dialog = nullptr;
    const HRESULT createResult = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(createResult) || dialog == nullptr) {
        return result;
    }
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT | FOS_PATHMUSTEXIST
                           | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    }
    if (!startDirectory.trimmed().isEmpty()) {
        IShellItem* startItem = nullptr;
        const QString native = QDir::toNativeSeparators(QDir::cleanPath(startDirectory));
        if (SUCCEEDED(SHCreateItemFromParsingName(
                reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, IID_PPV_ARGS(&startItem)))
            && startItem != nullptr) {
            dialog->SetFolder(startItem);
            startItem->Release();
        }
    }
    dialog->SetTitle(reinterpret_cast<LPCWSTR>(UiText::text(QStringLiteral("dialog.batch_export.select_charts")).utf16()));
    HWND parentWindow = nullptr;
    if (parent != nullptr && parent->window() != nullptr) {
        parent->window()->createWinId();
        if (QWindow* handle = parent->window()->windowHandle(); handle != nullptr) {
            parentWindow = reinterpret_cast<HWND>(handle->winId());
        }
    }
    if (SUCCEEDED(dialog->Show(parentWindow))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr) {
            DWORD count = 0;
            if (SUCCEEDED(items->GetCount(&count))) {
                for (DWORD index = 0; index < count; ++index) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(index, &item)) && item != nullptr) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
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
#endif

QStringList selectMultipleDirectories(QWidget* parent, const QString& startDirectory)
{
#ifdef Q_OS_WIN
    return selectMultipleDirectoriesNativeWin(parent, startDirectory);
#else
    QFileDialog dialog(parent, UiText::text(QStringLiteral("dialog.batch_export.select_charts")), startDirectory);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    for (QListView* view : dialog.findChildren<QListView*>()) {
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    for (QTreeView* view : dialog.findChildren<QTreeView*>()) {
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
    return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles() : QStringList();
#endif
}

}  // namespace

namespace miacode::video_export {

BatchExportPanel::BatchExportPanel(
    const VideoExportTask& baseTask,
    QList<int> availableDifficultyIds,
    int initialPreviewDifficultyId,
    QWidget* parent)
    : QWidget(parent)
    , baseTask_(baseTask)
    , selectionState_(availableDifficultyIds, initialPreviewDifficultyId)
    , availableDifficultyIds_(std::move(availableDifficultyIds))
    , requestedTaskTemplate_(baseTask)
{
    setObjectName(QStringLiteral("EmbeddedBatchExportPanel"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    rootLayout_ = new QVBoxLayout(this);
    // The panel host already applies the common 4px inset.
    // Adding it again here made the batch tab strip sit below the ordinary
    // embedded video-export tab strip.
    rootLayout_->setContentsMargins(0, 0, 0, 0);
    rootLayout_->setSpacing(8);

    settingsHost_ = new QWidget(this);
    auto* settingsHostLayout = new QVBoxLayout(settingsHost_);
    settingsHostLayout->setContentsMargins(0, 0, 0, 0);
    settingsHostLayout->setSpacing(0);
    rootLayout_->addWidget(settingsHost_, 1);

    auto* footerRule = new QWidget(this);
    footerRule->setObjectName(QStringLiteral("EmbeddedBatchExportFooterRule"));
    footerRule->setAttribute(Qt::WA_StyledBackground, true);
    footerRule->setFixedHeight(1);
    rootLayout_->addWidget(footerRule, 0);

    auto* footer = new QWidget(this);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 4, 0, 0);
    footerLayout->setSpacing(8);
    footerLayout->addStretch(1);
    startExportButton_ = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.video_export.button.start_export")), footer, true);
    connect(startExportButton_, &QPushButton::clicked, this, &BatchExportPanel::onStartExportClicked);
    footerLayout->addWidget(startExportButton_, 0);
    rootLayout_->addWidget(footer, 0);

    batchOutputControls_ = buildBatchOutputControls();
    applyThemeStyles();
}

QWidget* BatchExportPanel::buildBatchOutputControls()
{
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(10);

    auto* difficultyGroup = new QGroupBox(
        UiText::text(QStringLiteral("dialog.batch_export.difficulty")), content);
    difficultyGroup->setObjectName(QStringLiteral("BatchExportDifficultyGroup"));
    auto* difficultyGrid = new QGridLayout(difficultyGroup);
    difficultyGrid->setContentsMargins(10, 8, 10, 8);
    difficultyGrid->setHorizontalSpacing(10);
    difficultyGrid->setVerticalSpacing(8);
    for (int column = 0; column < kBatchExportTaskDifficultyGridColumns; ++column) {
        difficultyGrid->setColumnStretch(column, 1);
    }
    for (int index = 0; index < availableDifficultyIds_.size(); ++index) {
        const int difficultyId = availableDifficultyIds_.at(index);
        auto* check = new QCheckBox(SimaiDocument::difficultyShortName(difficultyId), difficultyGroup);
        check->setChecked(selectionState_.selectedDifficultyIds().contains(difficultyId));
        difficultyChecks_.append(check);
        const BatchExportTaskGridPosition position = batchExportTaskDifficultyGridPosition(index);
        difficultyGrid->addWidget(check, position.row, position.column);
    }
    layout->addWidget(difficultyGroup, 0);

    auto* outputSection = new QWidget(content);
    auto* outputSectionLayout = new QVBoxLayout(outputSection);
    outputSectionLayout->setContentsMargins(0, 0, 0, 0);
    outputSectionLayout->setSpacing(6);
    outputSectionLayout->addWidget(
        new QLabel(UiText::text(QStringLiteral("dialog.batch_export.output_dir")), outputSection), 0);
    auto* outputRow = new QWidget(outputSection);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);
    outputDirectoryEdit_ = new QLineEdit(outputRow);
    const QString rememberedDirectory = lastOutputBrowseDirectory();
    outputDirectoryEdit_->setText(QDir::toNativeSeparators(
        rememberedDirectory.isEmpty() ? exportBaseDirectory(baseTask_) : rememberedDirectory));
    outputLayout->addWidget(outputDirectoryEdit_, 1);
    auto* browseButton = miacode::ui::createDialogAuxiliaryButton(
        outputRow, UiText::text(QStringLiteral("action.browse")));
    connect(browseButton, &QPushButton::clicked, this, &BatchExportPanel::browseOutputDirectory);
    outputLayout->addWidget(browseButton, 0);
    outputSectionLayout->addWidget(outputRow, 0);
    layout->addWidget(outputSection, 0);

    layout->addWidget(new QLabel(UiText::text(QStringLiteral("dialog.batch_export.chart_folders")), content), 0);
    chartDirectoryList_ = new QListWidget(content);
    chartDirectoryList_->setObjectName(QStringLiteral("BatchExportChartDirectoryList"));
    chartDirectoryList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    chartDirectoryList_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    chartDirectoryList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chartDirectoryList_->setFixedHeight(148);
    layout->addWidget(chartDirectoryList_, 0);

    auto* directoryTools = new QWidget(content);
    auto* directoryToolsLayout = new QHBoxLayout(directoryTools);
    directoryToolsLayout->setContentsMargins(0, 0, 0, 0);
    directoryToolsLayout->setSpacing(8);
    auto* addButton = miacode::ui::createDialogAuxiliaryButton(
        directoryTools, UiText::text(QStringLiteral("dialog.batch_export.add_folders")));
    connect(addButton, &QPushButton::clicked, this, &BatchExportPanel::browseChartDirectories);
    auto* removeButton = miacode::ui::createDialogAuxiliaryButton(
        directoryTools, UiText::text(QStringLiteral("dialog.batch_export.remove_selected")));
    connect(removeButton, &QPushButton::clicked, this, &BatchExportPanel::removeSelectedChartDirectories);
    auto* clearButton = miacode::ui::createDialogAuxiliaryButton(
        directoryTools, UiText::text(QStringLiteral("dialog.batch_export.clear")));
    connect(clearButton, &QPushButton::clicked, this, &BatchExportPanel::clearChartDirectories);
    directoryToolsLayout->addWidget(addButton, 0);
    directoryToolsLayout->addWidget(removeButton, 0);
    directoryToolsLayout->addWidget(clearButton, 0);
    directoryToolsLayout->addStretch(1);
    layout->addWidget(directoryTools, 0);
    layout->addStretch(1);
    return content;
}

void BatchExportPanel::installSharedSettingsPanel(VideoExportDialog* settingsPanel)
{
    if (settingsPanel == nullptr || settingsPanel_ != nullptr || settingsHost_ == nullptr) {
        return;
    }
    settingsPanel_ = settingsPanel;
    settingsPanel_->setParent(settingsHost_);
    settingsPanel_->setBatchSettingsPanelMode();
    settingsPanel_->insertBatchTaskTab(batchOutputControls_);
    auto* settingsHostLayout = qobject_cast<QVBoxLayout*>(settingsHost_->layout());
    if (settingsHostLayout != nullptr) {
        settingsHostLayout->addWidget(settingsPanel_, 1);
    }
    connect(settingsPanel_, &VideoExportDialog::clockCountEnabledChanged,
            this, &BatchExportPanel::clockCountEnabledChanged);
    connect(settingsPanel_, &VideoExportDialog::introPreviewSettingsChanged,
            this, &BatchExportPanel::introPreviewSettingsChanged);
}

QStringList BatchExportPanel::chartDirectories() const
{
    QStringList result;
    if (chartDirectoryList_ == nullptr) {
        return result;
    }
    for (int index = 0; index < chartDirectoryList_->count(); ++index) {
        if (QListWidgetItem* item = chartDirectoryList_->item(index); item != nullptr) {
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.trimmed().isEmpty()) {
                result.append(path);
            }
        }
    }
    return result;
}

QString BatchExportPanel::outputDirectory() const
{
    return outputDirectoryEdit_ != nullptr
        ? QDir::cleanPath(QDir::fromNativeSeparators(outputDirectoryEdit_->text().trimmed()))
        : QString();
}

QList<int> BatchExportPanel::selectedDifficultyIds() const
{
    QList<int> result;
    for (int index = 0; index < difficultyChecks_.size() && index < availableDifficultyIds_.size(); ++index) {
        if (difficultyChecks_.at(index) != nullptr && difficultyChecks_.at(index)->isChecked()) {
            result.append(availableDifficultyIds_.at(index));
        }
    }
    return result;
}

bool BatchExportPanel::prepareRequestedTask(QString* errorMessage)
{
    if (selectedDifficultyIds().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_difficulties"));
        }
        return false;
    }
    if (chartDirectories().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_chart_dirs"));
        }
        return false;
    }
    if (outputDirectory().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_output_dir"));
        }
        return false;
    }
    if (settingsPanel_ == nullptr || !settingsPanel_->buildBatchTaskTemplate(&requestedTaskTemplate_, errorMessage)) {
        return false;
    }
    saveLastOutputBrowseDirectory(outputDirectory());
    return true;
}

void BatchExportPanel::updatePreviewDifficulty(int difficultyId, const VideoExportTask& retargetedTask)
{
    selectionState_.updatePreviewDifficulty(difficultyId);
    // Keep the panel's own copy in step: previewIntroSpec() falls back to it,
    // and the folder browsers resolve their start directory from it.
    baseTask_ = retargetedTask;
    if (settingsPanel_ != nullptr) {
        settingsPanel_->retargetChartPayload(retargetedTask);
    }
}

int BatchExportPanel::previewDifficultyId() const
{
    return selectionState_.previewDifficultyId();
}

bool BatchExportPanel::isClockCountEnabledForPreview() const
{
    return settingsPanel_ != nullptr && settingsPanel_->isClockCountEnabledForPreview();
}

bool BatchExportPanel::isAddIntroActiveForPreview() const
{
    return settingsPanel_ != nullptr && settingsPanel_->isAddIntroActiveForPreview();
}

IntroBannerSpec BatchExportPanel::previewIntroSpec() const
{
    return settingsPanel_ != nullptr ? settingsPanel_->previewIntroSpec() : baseTask_.intro;
}

void BatchExportPanel::setBatchExportRunning(bool running)
{
    if (startExportButton_ == nullptr) {
        return;
    }
    startExportButton_->setEnabled(!running);
    startExportButton_->setText(running
        ? UiText::text(QStringLiteral("dialog.batch_export.progress.preparing"))
        : UiText::text(QStringLiteral("dialog.video_export.button.start_export")));
}

void BatchExportPanel::applyThemeStyles()
{
    setStyleSheet(UiTheme::exportDialogStyleSheet());
    if (settingsPanel_ != nullptr) {
        settingsPanel_->applyThemeStyles();
    }
    if (outputDirectoryEdit_ != nullptr) {
        outputDirectoryEdit_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
    }
    if (chartDirectoryList_ != nullptr) {
        chartDirectoryList_->setStyleSheet(UiTheme::batchExportChartDirectoryListStyleSheet());
    }
    if (QWidget* footerRule = findChild<QWidget*>(QStringLiteral("EmbeddedBatchExportFooterRule")); footerRule != nullptr) {
        footerRule->setStyleSheet(QStringLiteral("background: %1;")
            .arg(UiTheme::colors().border.name(QColor::HexRgb)));
    }
}

void BatchExportPanel::addChartDirectories(const QStringList& directories)
{
    if (chartDirectoryList_ == nullptr) {
        return;
    }
    QSet<QString> known;
    for (int index = 0; index < chartDirectoryList_->count(); ++index) {
        if (QListWidgetItem* item = chartDirectoryList_->item(index); item != nullptr) {
            known.insert(item->data(Qt::UserRole).toString());
        }
    }

    QStringList invalidDescriptions;
    QString firstValidDirectory;
    for (const QString& directory : directories) {
        const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
        if (normalized.isEmpty() || known.contains(normalized)) {
            continue;
        }
        QString chartPath;
        QString validationError;
        if (!validateBatchChartDirectory(normalized, &chartPath, &validationError)) {
            invalidDescriptions.append(QStringLiteral("%1 - %2")
                .arg(QDir::toNativeSeparators(normalized), validationError));
            continue;
        }
        auto* item = new QListWidgetItem(chartDirectoryList_);
        item->setData(Qt::UserRole, normalized);
        item->setData(Qt::UserRole + 1, chartPath);
        known.insert(normalized);
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
            UiText::text(QStringLiteral("dialog.batch_export.title")),
            UiText::text(QStringLiteral("dialog.batch_export.error.invalid_selection"))
                + QStringLiteral("\n\n") + invalidDescriptions.join(QLatin1Char('\n')));
    }
}

void BatchExportPanel::browseChartDirectories()
{
    QString startDirectory = lastChartBrowseDirectory();
    if (startDirectory.isEmpty()) {
        startDirectory = outputDirectory();
    }
    if (startDirectory.isEmpty()) {
        startDirectory = exportBaseDirectory(baseTask_);
    }
    addChartDirectories(selectMultipleDirectories(this, startDirectory));
}

void BatchExportPanel::browseOutputDirectory()
{
    QString startDirectory = lastOutputBrowseDirectory();
    if (startDirectory.isEmpty()) {
        startDirectory = outputDirectory();
    }
    if (startDirectory.isEmpty()) {
        startDirectory = exportBaseDirectory(baseTask_);
    }
    const QString directory = QFileDialog::getExistingDirectory(
        this, UiText::text(QStringLiteral("dialog.batch_export.output_dir")), startDirectory);
    if (!directory.isEmpty()) {
        const QString normalized = QDir::cleanPath(directory);
        outputDirectoryEdit_->setText(QDir::toNativeSeparators(normalized));
        saveLastOutputBrowseDirectory(normalized);
    }
}

void BatchExportPanel::removeSelectedChartDirectories()
{
    if (chartDirectoryList_ == nullptr) {
        return;
    }
    const QList<QListWidgetItem*> selected = chartDirectoryList_->selectedItems();
    for (QListWidgetItem* item : selected) {
        delete chartDirectoryList_->takeItem(chartDirectoryList_->row(item));
    }
    refreshChartDirectoryNumbering();
}

void BatchExportPanel::clearChartDirectories()
{
    if (chartDirectoryList_ != nullptr) {
        chartDirectoryList_->clear();
    }
}

void BatchExportPanel::refreshChartDirectoryNumbering()
{
    if (chartDirectoryList_ == nullptr) {
        return;
    }
    for (int index = 0; index < chartDirectoryList_->count(); ++index) {
        if (QListWidgetItem* item = chartDirectoryList_->item(index); item != nullptr) {
            item->setText(QStringLiteral("[%1] %2")
                .arg(index + 1)
                .arg(QDir::toNativeSeparators(item->data(Qt::UserRole).toString())));
        }
    }
}

QString BatchExportPanel::lastChartBrowseDirectory() const
{
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    const QString value = settings.value(QStringLiteral("last_chart_directory")).toString();
    settings.endGroup();
    return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

QString BatchExportPanel::lastOutputBrowseDirectory() const
{
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    const QString value = settings.value(QStringLiteral("last_output_directory")).toString();
    settings.endGroup();
    return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

void BatchExportPanel::saveLastChartBrowseDirectory(const QString& directory) const
{
    if (directory.isEmpty()) {
        return;
    }
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    settings.setValue(QStringLiteral("last_chart_directory"), QDir::cleanPath(directory));
    settings.endGroup();
}

void BatchExportPanel::saveLastOutputBrowseDirectory(const QString& directory) const
{
    if (directory.isEmpty()) {
        return;
    }
    QSettings settings = exportDialogSettingsStore();
    settings.beginGroup(QStringLiteral("batch_video_export_dialog"));
    settings.setValue(QStringLiteral("last_output_directory"), QDir::cleanPath(directory));
    settings.endGroup();
}

void BatchExportPanel::onStartExportClicked()
{
    QString errorMessage;
    if (!prepareRequestedTask(&errorMessage)) {
        QMessageBox::warning(this, UiText::text(QStringLiteral("dialog.batch_export.title")), errorMessage);
        return;
    }
    emit batchExportConfirmed();
}

}  // namespace miacode::video_export
