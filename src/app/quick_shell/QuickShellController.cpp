#include "QuickShellController.h"

#include "IssueListModel.h"
#include "LegacyChartEditorSurface.h"
#include "LegacyTimelineSurface.h"
#include "OutlineListModel.h"
#include "mainwindow/MainWindow.h"
#include "preview/runtime/PreviewRuntime.h"
#include "UiText.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QTextEdit>
#include <QUrl>
#include <QVariant>
#include <QWindow>

namespace {

constexpr int kIssueTypeKeyRole = Qt::UserRole + 3;
constexpr int kIssueIgnoredRole = Qt::UserRole + 5;

const QList<double> kPreviewPlaybackRateOptions{
    0.25, 0.5, 0.75, 1.0, 1.25, 2.0,
};

template <typename T>
bool assignIfChanged(T& target, const T& value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

int countValidationErrors(const QListWidget* list)
{
    if (list == nullptr) {
        return 0;
    }
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) {
            continue;
        }
        if (item->data(Qt::UserRole + 2).toInt() != 1) {
            ++count;
        }
    }
    return count;
}

int countValidationWarnings(const QListWidget* list)
{
    if (list == nullptr) {
        return 0;
    }
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item == nullptr || !item->flags().testFlag(Qt::ItemIsEnabled)) {
            continue;
        }
        if (item->data(Qt::UserRole + 2).toInt() == 1) {
            ++count;
        }
    }
    return count;
}

int countEnabledIssues(const QListWidget* list)
{
    if (list == nullptr) {
        return 0;
    }
    int count = 0;
    for (int row = 0; row < list->count(); ++row) {
        const QListWidgetItem* item = list->item(row);
        if (item != nullptr && item->flags().testFlag(Qt::ItemIsEnabled)) {
            ++count;
        }
    }
    return count;
}

double steppedPreviewPlaybackRateLocal(double rate, int direction)
{
    if (kPreviewPlaybackRateOptions.isEmpty()) {
        return qMax(0.25, rate);
    }

    int bestIndex = 0;
    double bestDiff = qAbs(kPreviewPlaybackRateOptions.first() - rate);
    for (int index = 1; index < kPreviewPlaybackRateOptions.size(); ++index) {
        const double diff = qAbs(kPreviewPlaybackRateOptions.at(index) - rate);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = index;
        }
    }

    const int targetIndex = qBound(0, bestIndex + direction, kPreviewPlaybackRateOptions.size() - 1);
    return kPreviewPlaybackRateOptions.at(targetIndex);
}

}  // namespace

QuickShellController::QuickShellController(MainWindow* backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , outlineModel_(new OutlineListModel(this))
    , validationModel_(new IssueListModel(IssueListModel::SourceKind::Validation, this))
    , muriModel_(new IssueListModel(IssueListModel::SourceKind::Muri, this))
    , chartEditorSurface_(new LegacyChartEditorSurface(this))
    , timelineSurface_(new LegacyTimelineSurface(this))
{
    if (backend_ != nullptr) {
        chartEditorSurface_->setWindow(
            backend_->quickShellChartSurfaceWidget_ != nullptr
                ? backend_->quickShellChartSurfaceWidget_->windowHandle()
                : nullptr
        );
        timelineSurface_->setWindow(
            backend_->quickShellTimelineSurfaceWidget_ != nullptr
                ? backend_->quickShellTimelineSurfaceWidget_->windowHandle()
                : nullptr
        );
        if (QStatusBar* statusBar = backend_->statusBar(); statusBar != nullptr) {
            statusText_ = statusBar->currentMessage();
            connect(statusBar, &QStatusBar::messageChanged, this, [this](const QString& message) {
                if (statusText_ == message) {
                    return;
                }
                statusText_ = message;
                emit statusTextChanged();
            });
        }
    }

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(150);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellController::refreshFromBackend);
    refreshTimer_->start();

    refreshFromBackend();
}

QString QuickShellController::windowTitle() const { return windowTitle_; }
QString QuickShellController::currentPage() const { return currentPage_; }
bool QuickShellController::hasActiveDifficulty() const { return hasActiveDifficulty_; }
QString QuickShellController::activeDifficultyName() const { return activeDifficultyName_; }
QString QuickShellController::currentFileLabel() const { return currentFileLabel_; }
QString QuickShellController::cursorLabel() const { return cursorLabel_; }
QString QuickShellController::titleDraft() const { return titleDraft_; }
QString QuickShellController::artistDraft() const { return artistDraft_; }
QString QuickShellController::firstDraft() const { return firstDraft_; }
QString QuickShellController::designerDraft() const { return designerDraft_; }
QString QuickShellController::metadataExtraDraft() const { return metadataExtraDraft_; }
QString QuickShellController::difficultyLevelDraft() const { return difficultyLevelDraft_; }
QString QuickShellController::difficultyDesignerDraft() const { return difficultyDesignerDraft_; }
int QuickShellController::validationErrorCount() const { return validationErrorCount_; }
int QuickShellController::validationWarningCount() const { return validationWarningCount_; }
int QuickShellController::muriIssueCount() const { return muriIssueCount_; }
QString QuickShellController::previewSpeedLabel() const { return previewSpeedLabel_; }
bool QuickShellController::previewPlaying() const { return previewPlaying_; }
double QuickShellController::previewPositionSeconds() const { return previewPositionSeconds_; }
double QuickShellController::previewDurationSeconds() const { return previewDurationSeconds_; }
bool QuickShellController::workspacePanelsSwapped() const { return workspacePanelsSwapped_; }
QString QuickShellController::statusText() const { return statusText_; }
QVariantList QuickShellController::previewStats() const { return previewStats_; }
QVariantList QuickShellController::availableDifficultyOptions() const { return availableDifficultyOptions_; }
bool QuickShellController::showBottomTabs() const { return hasActiveDifficulty_; }
int QuickShellController::bottomTabIndex() const { return bottomTabIndex_; }
bool QuickShellController::previewFullscreen() const { return previewFullscreen_; }
OutlineListModel* QuickShellController::outlineModel() const { return outlineModel_; }
IssueListModel* QuickShellController::validationModel() const { return validationModel_; }
IssueListModel* QuickShellController::muriModel() const { return muriModel_; }
LegacyChartEditorSurface* QuickShellController::chartEditorSurface() const { return chartEditorSurface_; }
LegacyTimelineSurface* QuickShellController::timelineSurface() const { return timelineSurface_; }

QWindow* QuickShellController::previewWindow() const
{
    return backend_ != nullptr && backend_->previewCanvas_ != nullptr
        ? backend_->previewCanvas_->hostWindow()
        : nullptr;
}

void QuickShellController::setTitleDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->titleEdit_ != nullptr && backend_->titleEdit_->text() != text) {
        backend_->titleEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setArtistDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->artistEdit_ != nullptr && backend_->artistEdit_->text() != text) {
        backend_->artistEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setFirstDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->firstEdit_ != nullptr && backend_->firstEdit_->text() != text) {
        backend_->firstEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setDesignerDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->designerEdit_ != nullptr && backend_->designerEdit_->text() != text) {
        backend_->designerEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setMetadataExtraDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->metadataExtraEdit_ != nullptr
        && backend_->metadataExtraEdit_->toPlainText() != text) {
        backend_->setMetadataExtraText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setDifficultyLevelDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->difficultyLevelEdit_ != nullptr
        && backend_->difficultyLevelEdit_->text() != text) {
        backend_->difficultyLevelEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setDifficultyDesignerDraft(const QString& text)
{
    if (backend_ != nullptr && backend_->difficultyDesignerEdit_ != nullptr
        && backend_->difficultyDesignerEdit_->text() != text) {
        backend_->difficultyDesignerEdit_->setText(text);
        refreshFromBackend();
    }
}

void QuickShellController::setBottomTabIndex(int index)
{
    const int normalized = qBound(0, index, 2);
    if (bottomTabIndex_ == normalized) {
        return;
    }
    bottomTabIndex_ = normalized;
    emit shellStateChanged();
}

void QuickShellController::setPreviewFullscreen(bool fullscreen)
{
    if (previewFullscreen_ == fullscreen) {
        return;
    }
    previewFullscreen_ = fullscreen;
    emit previewFullscreenChanged();
}

void QuickShellController::refresh()
{
    refreshFromBackend();
}

bool QuickShellController::confirmClose()
{
    if (backend_ == nullptr) {
        return true;
    }
    if (!backend_->maybeSaveBeforeContinue()) {
        return false;
    }
    backend_->savePortableState();
    backend_->clearVideoExportWorkerState();
    return true;
}

void QuickShellController::newFile()
{
    if (backend_ != nullptr) {
        backend_->onNewFile();
        refreshFromBackend();
    }
}

void QuickShellController::openFile()
{
    if (backend_ != nullptr) {
        backend_->onOpenFile();
        refreshFromBackend();
    }
}

void QuickShellController::saveFile()
{
    if (backend_ != nullptr) {
        backend_->onSaveFile();
        refreshFromBackend();
    }
}

void QuickShellController::saveFileAs()
{
    if (backend_ != nullptr) {
        backend_->onSaveFileAs();
        refreshFromBackend();
    }
}

void QuickShellController::openPreferences()
{
    if (backend_ != nullptr) {
        backend_->onPreferences();
        refreshFromBackend();
    }
}

void QuickShellController::openAbout()
{
    if (backend_ != nullptr) {
        backend_->onAbout();
        refreshFromBackend();
    }
}

void QuickShellController::openPreviewAudioSettings()
{
    if (backend_ != nullptr) {
        backend_->onPreviewAudioSettings();
        refreshFromBackend();
    }
}

void QuickShellController::openPreviewVideoSettings()
{
    if (backend_ != nullptr) {
        backend_->onPreviewVideoSettings();
        refreshFromBackend();
    }
}

void QuickShellController::runSyntaxCheck()
{
    if (backend_ != nullptr) {
        backend_->onValidateSimai();
        refreshFromBackend();
    }
}

void QuickShellController::mirrorLeftRight()
{
    if (backend_ != nullptr) {
        backend_->onMirrorLeftRight();
        refreshFromBackend();
    }
}

void QuickShellController::mirrorUpDown()
{
    if (backend_ != nullptr) {
        backend_->onMirrorUpDown();
        refreshFromBackend();
    }
}

void QuickShellController::rotate180()
{
    if (backend_ != nullptr) {
        backend_->onRotate180();
        refreshFromBackend();
    }
}

void QuickShellController::rotate45CounterClockwise()
{
    if (backend_ != nullptr) {
        backend_->onRotate45CounterClockwise();
        refreshFromBackend();
    }
}

void QuickShellController::rotate45Clockwise()
{
    if (backend_ != nullptr) {
        backend_->onRotate45Clockwise();
        refreshFromBackend();
    }
}

void QuickShellController::normalizeWholeChart()
{
    if (backend_ != nullptr) {
        backend_->onNormalizeWholeChart();
        refreshFromBackend();
    }
}

void QuickShellController::toggleBreakSelection()
{
    if (backend_ != nullptr) {
        backend_->onToggleBreakSelection();
        refreshFromBackend();
    }
}

void QuickShellController::toggleExSelection()
{
    if (backend_ != nullptr) {
        backend_->onToggleExSelection();
        refreshFromBackend();
    }
}

void QuickShellController::toggleFireworkSelection()
{
    if (backend_ != nullptr) {
        backend_->onToggleFireworkSelection();
        refreshFromBackend();
    }
}

void QuickShellController::randomRotateSelection()
{
    if (backend_ != nullptr) {
        backend_->onRandomRotateSelection();
        refreshFromBackend();
    }
}

void QuickShellController::exportPreviewVideo()
{
    if (backend_ != nullptr) {
        backend_->onExportPreviewVideo();
        refreshFromBackend();
    }
}

void QuickShellController::batchExportPreviewVideo()
{
    if (backend_ != nullptr) {
        backend_->onBatchExportPreviewVideo();
        refreshFromBackend();
    }
}

void QuickShellController::openLatencyDetector()
{
    if (backend_ != nullptr) {
        backend_->onOpenLatencyDetector();
        refreshFromBackend();
    }
}

void QuickShellController::activateOutlineRow(int row)
{
    if (backend_ == nullptr || backend_->outlineList_ == nullptr || row < 0 || row >= backend_->outlineList_->count()) {
        return;
    }

    QListWidgetItem* current = backend_->outlineList_->item(row);
    if (current == nullptr) {
        return;
    }

    backend_->outlineList_->setCurrentRow(row);
    backend_->updateDifficultyDeleteButton(false);

    const QString kind = current->data(Qt::UserRole).toString();
    const int difficultyId = current->data(Qt::UserRole + 1).toInt();
    if (kind == QStringLiteral("metadata")) {
        backend_->activeOutlineKey_ = QStringLiteral("metadata");
        if (backend_->switchToMetadataField() && backend_->titleEdit_ != nullptr) {
            backend_->titleEdit_->setFocus();
        }
    } else if (kind == QStringLiteral("difficulty_chart")) {
        backend_->activeOutlineKey_ = QStringLiteral("chart");
        if (backend_->switchToDifficultyField(difficultyId) && backend_->editorWidget_ != nullptr) {
            backend_->editorWidget_->setFocus();
        }
    }

    refreshFromBackend();
}

void QuickShellController::addDifficulty(int difficultyId)
{
    if (backend_ == nullptr
        || !SimaiDocument::isDifficultyId(difficultyId)
        || backend_->document_.difficulty(difficultyId) != nullptr) {
        return;
    }

    if (!backend_->maybeSaveCurrentFieldChanges()) {
        backend_->rebuildFieldSidebar();
        refreshFromBackend();
        return;
    }

    backend_->document_.ensureDifficulty(difficultyId);
    backend_->documentDirty_ = true;
    backend_->activeOutlineKey_ = QStringLiteral("chart");
    backend_->updateDirtyState();
    backend_->switchToDifficultyField(difficultyId);
    refreshFromBackend();
}

void QuickShellController::deleteDifficulty(int difficultyId)
{
    if (backend_ == nullptr) {
        return;
    }

    const int targetDifficultyId = difficultyId > 0 ? difficultyId : backend_->activeDifficultyId_;
    if (!SimaiDocument::isDifficultyId(targetDifficultyId)) {
        return;
    }

    backend_->deleteDifficultyField(targetDifficultyId);
    refreshFromBackend();
}

void QuickShellController::activateIssue(bool muriList, int row)
{
    QListWidgetItem* item = issueItem(muriList, row);
    if (backend_ == nullptr || item == nullptr) {
        return;
    }

    if (muriList) {
        backend_->onMuriItemActivated(item);
    } else {
        backend_->onErrorItemActivated(item);
    }
    refreshFromBackend();
}

void QuickShellController::copyIssue(bool muriList, int row)
{
    QListWidgetItem* item = issueItem(muriList, row);
    if (item == nullptr) {
        return;
    }

    QGuiApplication::clipboard()->setText(item->toolTip().trimmed());
    showCopiedStatusMessage();
}

void QuickShellController::toggleIssueIgnored(bool muriList, int row)
{
    if (backend_ == nullptr) {
        return;
    }

    QListWidgetItem* item = issueItem(muriList, row);
    if (item == nullptr) {
        return;
    }
    const QString issueTypeKey = item->data(kIssueTypeKeyRole).toString();
    if (issueTypeKey.isEmpty()) {
        return;
    }

    const bool ignored = item->data(kIssueIgnoredRole).toBool();
    backend_->setIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey, !ignored);
    backend_->refreshValidationPanelForActiveField();
    backend_->refreshMuriDiagnosticsPanel();
    refreshFromBackend();
}

void QuickShellController::togglePreviewPlayback()
{
    if (backend_ != nullptr) {
        backend_->onTogglePreviewPause();
        refreshFromBackend();
    }
}

void QuickShellController::stopPreview()
{
    if (backend_ != nullptr) {
        backend_->onStopPreview();
        refreshFromBackend();
    }
}

void QuickShellController::seekPreview(double second)
{
    if (backend_ != nullptr) {
        backend_->seekPreviewToSecond(second, true);
        refreshFromBackend();
    }
}

void QuickShellController::stepPreviewBy(double deltaSeconds)
{
    if (backend_ != nullptr) {
        backend_->stepPreviewSliderBySeconds(deltaSeconds, true);
        refreshFromBackend();
    }
}

void QuickShellController::setPreviewRate(double rate)
{
    if (backend_ != nullptr) {
        backend_->applyPreviewPlaybackRate(rate);
        refreshFromBackend();
    }
}

void QuickShellController::stepPreviewRate(int direction)
{
    if (backend_ != nullptr) {
        backend_->applyPreviewPlaybackRate(steppedPreviewPlaybackRateLocal(backend_->previewPlaybackRate_, direction));
        refreshFromBackend();
    }
}

void QuickShellController::toggleWorkspacePanelsSwapped()
{
    if (backend_ != nullptr) {
        backend_->setWorkspacePanelsSwapped(!backend_->workspacePanelsSwapped_, true);
        refreshFromBackend();
    }
}

void QuickShellController::openOfficialChartMirror()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.maiviewer.net/")));
}

void QuickShellController::openSimaiWiki()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://w.atwiki.jp/simai/")));
}

QListWidgetItem* QuickShellController::issueItem(bool muriList, int row) const
{
    if (backend_ == nullptr) {
        return nullptr;
    }

    QListWidget* list = muriList ? backend_->muriList_ : backend_->errorList_;
    if (list == nullptr || row < 0 || row >= list->count()) {
        return nullptr;
    }
    return list->item(row);
}

void QuickShellController::refreshFromBackend()
{
    if (backend_ == nullptr) {
        return;
    }

    const auto detectCurrentPage = [this]() {
        if (backend_->editorStack_ == nullptr) {
            return QStringLiteral("welcome");
        }
        if (backend_->editorStack_->currentWidget() == backend_->metadataPage_) {
            return QStringLiteral("metadata");
        }
        if (backend_->editorStack_->currentWidget() == backend_->chartPage_) {
            return QStringLiteral("chart");
        }
        return QStringLiteral("welcome");
    };
    const auto buildPreviewStats = [this]() {
        QVariantList stats;
        const QList<QLabel*> labels{
            backend_->previewTapStatsLabel_,
            backend_->previewHoldStatsLabel_,
            backend_->previewSlideStatsLabel_,
            backend_->previewTouchStatsLabel_,
            backend_->previewBreakStatsLabel_,
            backend_->previewTotalStatsLabel_,
        };
        for (QLabel* label : labels) {
            if (label != nullptr) {
                stats.push_back(label->text());
            }
        }
        return stats;
    };
    const auto buildAvailableDifficultyOptions = [this]() {
        QVariantList options;
        for (int id = 1; id <= 7; ++id) {
            if (backend_->document_.difficulty(id) != nullptr) {
                continue;
            }
            QVariantMap option;
            option.insert(QStringLiteral("id"), id);
            option.insert(QStringLiteral("label"), SimaiDocument::difficultyName(id));
            options.push_back(option);
        }
        return options;
    };

    bool changed = false;
    outlineModel_->refreshFromList(backend_->outlineList_, backend_->activeDifficultyId_);
    validationModel_->refreshFromList(backend_->errorList_);
    muriModel_->refreshFromList(backend_->muriList_);

    changed |= assignIfChanged(windowTitle_, backend_->windowTitle());
    changed |= assignIfChanged(currentPage_, detectCurrentPage());
    changed |= assignIfChanged(hasActiveDifficulty_, backend_->hasActiveDifficulty());
    changed |= assignIfChanged(
        activeDifficultyName_,
        hasActiveDifficulty_ ? SimaiDocument::difficultyName(backend_->activeDifficultyId()) : QString()
    );
    changed |= assignIfChanged(
        currentFileLabel_,
        backend_->currentFileLabel_ != nullptr ? backend_->currentFileLabel_->text() : QString()
    );
    changed |= assignIfChanged(
        cursorLabel_,
        backend_->editorCursorLabel_ != nullptr ? backend_->editorCursorLabel_->text() : QString()
    );
    changed |= assignIfChanged(titleDraft_, backend_->titleEdit_ != nullptr ? backend_->titleEdit_->text() : QString());
    changed |= assignIfChanged(artistDraft_, backend_->artistEdit_ != nullptr ? backend_->artistEdit_->text() : QString());
    changed |= assignIfChanged(firstDraft_, backend_->firstEdit_ != nullptr ? backend_->firstEdit_->text() : QString());
    changed |= assignIfChanged(
        designerDraft_,
        backend_->designerEdit_ != nullptr ? backend_->designerEdit_->text() : QString()
    );
    changed |= assignIfChanged(
        metadataExtraDraft_,
        backend_->metadataExtraEdit_ != nullptr ? backend_->metadataExtraEdit_->toPlainText() : QString()
    );
    changed |= assignIfChanged(
        difficultyLevelDraft_,
        backend_->difficultyLevelEdit_ != nullptr ? backend_->difficultyLevelEdit_->text() : QString()
    );
    changed |= assignIfChanged(
        difficultyDesignerDraft_,
        backend_->difficultyDesignerEdit_ != nullptr ? backend_->difficultyDesignerEdit_->text() : QString()
    );
    changed |= assignIfChanged(validationErrorCount_, countValidationErrors(backend_->errorList_));
    changed |= assignIfChanged(validationWarningCount_, countValidationWarnings(backend_->errorList_));
    changed |= assignIfChanged(muriIssueCount_, countEnabledIssues(backend_->muriList_));
    changed |= assignIfChanged(
        previewSpeedLabel_,
        backend_->previewSpeedButton_ != nullptr ? backend_->previewSpeedButton_->text() : QStringLiteral("1x")
    );
    changed |= assignIfChanged(previewPlaying_, backend_->qtPreviewPlaying_);
    changed |= assignIfChanged(
        previewPositionSeconds_,
        backend_->previewSlider_ != nullptr
            ? static_cast<double>(backend_->previewSlider_->value()) / 1000.0
            : backend_->qtPreviewPauseSecond_
    );
    changed |= assignIfChanged(previewDurationSeconds_, backend_->previewDurationSeconds());
    changed |= assignIfChanged(workspacePanelsSwapped_, backend_->workspacePanelsSwapped_);
    changed |= assignIfChanged(previewStats_, buildPreviewStats());
    changed |= assignIfChanged(availableDifficultyOptions_, buildAvailableDifficultyOptions());

    const int normalizedTabIndex = hasActiveDifficulty_ ? qBound(0, bottomTabIndex_, 2) : 0;
    if (normalizedTabIndex != bottomTabIndex_) {
        bottomTabIndex_ = normalizedTabIndex;
        changed = true;
    }

    if (changed) {
        emit shellStateChanged();
    }
}

void QuickShellController::showCopiedStatusMessage()
{
    if (backend_ == nullptr || backend_->statusBar() == nullptr) {
        return;
    }
    backend_->statusBar()->showMessage(
        UiText::isChineseUi() ? QStringLiteral("已复制信息。") : QStringLiteral("Issue info copied.")
    );
}
