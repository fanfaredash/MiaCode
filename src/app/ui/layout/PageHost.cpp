#include "layout/PageHost.h"

#include "common/DebugLog.h"
#include "document/DocumentModel.h"
#include "app/ui/export/ExportSession.h"


namespace miacode::ui {
PageHost::PageHost(miacode::ShellNotifications& notifications,
                                     DocumentModel& document,
                                     miacode::EditorPageRouter*& routerSlot,
                                     QObject*& exportSessionSlot,
                                     QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , document_(&document)
    , routerSlot_(&routerSlot)
    , exportSessionSlot_(&exportSessionSlot)
{
    // The menu action and the chart.normalize shortcut land on MainWindow;
    // re-emit so the editor sees one request regardless of where it came from.
    connect(&notifications, &miacode::ShellNotifications::normalizeWholeChartRequested, this, [this]() {
        openNormalizeWholeChart();
    });
    connect(&notifications, &miacode::ShellNotifications::mediaToolsRequested, this, [this]() {
        openMediaProcessingTools();
    });
    connect(&notifications, &miacode::ShellNotifications::preferencesRequested, this, [this]() {
        if (overlayActive()) {
            requestPageSwitch([this]() {
                if (!finishLeaveOverlay()) {
                    return false;
                }
                emit preferencesRequested();
                return true;
            });
        } else if (!navigationPending_) {
            emit preferencesRequested();
        }
    });
    connect(&notifications, &miacode::ShellNotifications::coverExportRequested, this, [this](int difficultyId) {
        openCoverExport(difficultyId);
    });
    connect(&notifications, &miacode::ShellNotifications::selectionRangeExportPageRequested, this, [this]() {
        openVideoExportPage();
    });
    // requestPageSwitch() is asynchronous: openVideoExportPage() returns true
    // once the switch is queued, and a refusal arrives here instead. Drop the
    // staged range then, so it cannot be applied by a later unrelated entry.
    connect(this, &PageHost::navigationRejected, this, [this]() {
        if (ExportSession* const session = exportSessionObject(); session != nullptr) {
            session->clearPendingSelectionRangeExport();
        }
    });
}

ExportSession* PageHost::exportSessionObject() const
{
    return exportSessionSlot_ != nullptr
        ? qobject_cast<ExportSession*>(*exportSessionSlot_)
        : nullptr;
}

QObject* PageHost::exportSession() const
{
    return exportSessionSlot_ != nullptr ? *exportSessionSlot_ : nullptr;
}

void PageHost::markExportPageActive()
{
    if (activePageId_ == QLatin1String("export")) {
        return;
    }
    activePageId_ = QStringLiteral("export");
    emit activePageIdChanged();
}

void PageHost::rememberResumeDifficulty()
{
    if (resumeEditorKeyExplicit_) {
        return;
    }
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return;
    }
    if (pages->hasActiveDifficulty() && pages->activeDifficultyId() > 0) {
        resumeDifficultyId_ = pages->activeDifficultyId();
    } else {
        resumeDifficultyId_ = 0;
    }
}

void PageHost::rememberEditorReturnTarget(const QString& editorKey)
{
    resumeEditorKey_ = editorKey;
    resumeEditorKeyExplicit_ = true;
    resumeDifficultyId_ = 0;
}

bool PageHost::resumeChartOrMetadata()
{
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return false;
    }
    if (resumeEditorKeyExplicit_ && resumeEditorKey_.isEmpty()) {
        resumeEditorKey_.clear();
        resumeEditorKeyExplicit_ = false;
        return pages->clearEditorPresentation();
    }
    int difficultyId = resumeDifficultyId_;
    if (resumeEditorKeyExplicit_ && resumeEditorKey_.startsWith(QStringLiteral("difficulty:"))) {
        difficultyId = resumeEditorKey_.mid(QStringLiteral("difficulty:").size()).toInt();
    }
    if (difficultyId > 0) {
        // The export session owns its selected difficulty independently from
        // the document workspace. Restore the editor data source before
        // restoring the runtime page, so the tab and editor text use one id.
        if (document_ != nullptr) {
            document_->selectDifficulty(difficultyId);
        }
    }
    if (difficultyId > 0 && pages->enterDifficultyPage(difficultyId)) {
        resumeEditorKey_.clear();
        resumeEditorKeyExplicit_ = false;
        resumeDifficultyId_ = 0;
        return true;
    }
    const bool restored = !resumeEditorKeyExplicit_ || resumeEditorKey_ == QLatin1String("metadata")
        ? pages->enterMetadataPage()
        : pages->clearEditorPresentation();
    resumeEditorKey_.clear();
    resumeEditorKeyExplicit_ = false;
    resumeDifficultyId_ = 0;
    return restored;
}

bool PageHost::requestPageSwitch(std::function<bool()> action)
{
    if (navigationPending_ || document_ == nullptr || !action) {
        return false;
    }

    navigationPending_ = true;
    emit navigationPendingChanged();
    const qulonglong documentGeneration = document_->documentGeneration();
    QPointer<PageHost> self(this);
    document_->requestLeaveCurrentField(
        [self, documentGeneration, action = std::move(action)](bool mayLeave) mutable {
            if (!self) {
                return;
            }
            self->navigationPending_ = false;
            emit self->navigationPendingChanged();
            if (!mayLeave || self->document_ == nullptr
                || self->document_->documentGeneration() != documentGeneration
                || !action()) {
                emit self->navigationRejected();
                return;
            }
        });
    return true;
}

bool PageHost::openVideoExportPage(const QString& tab)
{
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr || exportSessionObject() == nullptr) {
        return false;
    }
    const QString requestedTab = tab == QLatin1String("batch")
        ? QStringLiteral("batch") : QStringLiteral("export");
    if (activePageId_ == QLatin1String("export")) {
        // The export page is a resident QML surface. Re-clicking its sidebar
        // entry only changes the single/batch tab; tearing down and rebuilding
        // the audition here made an otherwise harmless navigation expensive.
        exportSessionObject()->setActiveTab(requestedTab);
        return true;
    }
    rememberResumeDifficulty();
    return requestPageSwitch([this, requestedTab]() {
        if (exportSessionObject() == nullptr || router() == nullptr) {
            return false;
        }
        exportSessionObject()->setActiveTab(requestedTab);
        if (!router()->enterExportPage()) {
            return false;
        }
        markExportPageActive();
        return true;
    });
}

bool PageHost::openExportPage()
{
    return openVideoExportPage(QStringLiteral("export"));
}

bool PageHost::openLatencyPage()
{
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    return requestPageSwitch([this]() {
        const bool leavingExportPage = activePageId_ == QLatin1String("export");
        if (router() == nullptr || !router()->enterLatencyPage()) {
            return false;
        }
        if (leavingExportPage && exportSessionObject() != nullptr) {
            exportSessionObject()->leave();
        }
        // The page is QML now; only the active id has to change so MainSplitView
        // shows it.
        if (activePageId_ != QLatin1String("latency")) {
            activePageId_ = QStringLiteral("latency");
            emit activePageIdChanged();
        }
        return true;
    });
}

bool PageHost::finishLeaveOverlay()
{
    if (router() == nullptr) {
        return false;
    }
    if (!overlayActive()) {
        return true;
    }

    if (activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr) {
        exportSessionObject()->leave();
    }
    if (!resumeChartOrMetadata()) {
        return false;
    }
    activePageId_.clear();
    emit activePageIdChanged();
    emit overlayPageLeft();
    return true;
}

bool PageHost::leaveOverlayPage()
{
    if (router() == nullptr) {
        return false;
    }
    if (!overlayActive()) {
        return true;
    }
    return requestPageSwitch([this]() { return finishLeaveOverlay(); });
}

bool PageHost::ensureDifficultyPageActive(int difficultyId)
{
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr || difficultyId <= 0) {
        return false;
    }
    if (pages->hasActiveDifficulty() && pages->activeDifficultyId() == difficultyId) {
        return true;
    }
    return pages->enterDifficultyPage(difficultyId);
}

bool PageHost::clearEditorPresentation()
{
    miacode::EditorPageRouter* const pages = router();
    if (pages == nullptr || overlayActive() || navigationPending_) {
        return false;
    }
    return pages->clearEditorPresentation();
}

void PageHost::openMediaProcessingTools()
{
    if (navigationPending_) {
        return;
    }
    if (overlayActive()) {
        requestPageSwitch([this]() {
            if (!finishLeaveOverlay()) {
                return false;
            }
            emit mediaToolsRequested();
            return true;
        });
        return;
    }
    emit mediaToolsRequested();
}

void PageHost::openNormalizeWholeChart()
{
    if (navigationPending_) {
        return;
    }
    if (activePageId_ == QLatin1String("export")) {
        return;
    }
    if (overlayActive()) {
        requestPageSwitch([this]() {
            if (!finishLeaveOverlay()) {
                return false;
            }
            emit normalizeWholeChartRequested();
            return true;
        });
        return;
    }
    emit normalizeWholeChartRequested();
}

void PageHost::openBatchExport()
{
    openVideoExportPage(QStringLiteral("batch"));
}

bool PageHost::openCoverExport(int difficultyId)
{
    if (router() == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    const int selectedDifficultyId = difficultyId > 0 ? difficultyId
        : activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr
            ? exportSessionObject()->selectedDifficultyId() : resumeDifficultyId_;
    return requestPageSwitch([this, selectedDifficultyId]() {
        if (activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr) {
            exportSessionObject()->leave();
        }
        emit coverWindowRequested(selectedDifficultyId);
        return true;
    });
}

void PageHost::packAsZip()
{
    if (miacode::EditorPageRouter* const pages = router(); pages != nullptr) {
        pages->packChartAsZip();
    }
}

} // namespace miacode::ui
