#include "QmlEditorPageHost.h"

#include "common/DebugLog.h"
#include "QmlDocumentModel.h"
#include "app/qml_ui/export/QmlExportSession.h"

QmlEditorPageHost::QmlEditorPageHost(miacode::v2::ShellNotifications& notifications,
                                     QmlDocumentModel& document,
                                     miacode::v2::EditorPageRouter*& routerSlot,
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
    connect(&notifications, &miacode::v2::ShellNotifications::normalizeWholeChartRequested, this, [this]() {
        openNormalizeWholeChart();
    });
    connect(&notifications, &miacode::v2::ShellNotifications::mediaToolsRequested, this, [this]() {
        openMediaProcessingTools();
    });
    connect(&notifications, &miacode::v2::ShellNotifications::preferencesRequested, this, [this]() {
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
    connect(&notifications, &miacode::v2::ShellNotifications::coverExportRequested, this, [this](int difficultyId) {
        openCoverExport(difficultyId);
    });
}

QmlExportSession* QmlEditorPageHost::exportSessionObject() const
{
    return exportSessionSlot_ != nullptr
        ? qobject_cast<QmlExportSession*>(*exportSessionSlot_)
        : nullptr;
}

QObject* QmlEditorPageHost::exportSession() const
{
    return exportSessionSlot_ != nullptr ? *exportSessionSlot_ : nullptr;
}

void QmlEditorPageHost::markExportPageActive()
{
    if (activePageId_ == QLatin1String("export")) {
        return;
    }
    activePageId_ = QStringLiteral("export");
    emit activePageIdChanged();
}

void QmlEditorPageHost::rememberResumeDifficulty()
{
    miacode::v2::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return;
    }
    if (pages->hasActiveDifficulty() && pages->activeDifficultyId() > 0) {
        resumeDifficultyId_ = pages->activeDifficultyId();
    }
}

bool QmlEditorPageHost::resumeChartOrMetadata()
{
    miacode::v2::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return false;
    }
    if (resumeDifficultyId_ > 0 && pages->enterDifficultyPage(resumeDifficultyId_)) {
        return true;
    }
    return pages->enterMetadataPage();
}

bool QmlEditorPageHost::requestPageSwitch(std::function<bool()> action)
{
    if (navigationPending_ || document_ == nullptr || !action) {
        return false;
    }

    navigationPending_ = true;
    emit navigationPendingChanged();
    const qulonglong documentGeneration = document_->documentGeneration();
    QPointer<QmlEditorPageHost> self(this);
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

bool QmlEditorPageHost::openVideoExportPage(const QString& tab)
{
    miacode::v2::EditorPageRouter* const pages = router();
    if (pages == nullptr || exportSessionObject() == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    return requestPageSwitch([this, tab]() {
        if (exportSessionObject() == nullptr || router() == nullptr) {
            return false;
        }
        exportSessionObject()->setActiveTab(
            tab == QLatin1String("batch") ? QStringLiteral("batch") : QStringLiteral("export"));
        if (!router()->enterExportPage()) {
            return false;
        }
        markExportPageActive();
        return true;
    });
}

bool QmlEditorPageHost::openExportPage()
{
    return openVideoExportPage(QStringLiteral("export"));
}

bool QmlEditorPageHost::openLatencyPage()
{
    miacode::v2::EditorPageRouter* const pages = router();
    if (pages == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    return requestPageSwitch([this]() {
        if (router() == nullptr || !router()->enterLatencyPage()) {
            return false;
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

bool QmlEditorPageHost::finishLeaveOverlay()
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

bool QmlEditorPageHost::leaveOverlayPage()
{
    if (router() == nullptr) {
        return false;
    }
    if (!overlayActive()) {
        return true;
    }
    return requestPageSwitch([this]() { return finishLeaveOverlay(); });
}

void QmlEditorPageHost::openMediaProcessingTools()
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

void QmlEditorPageHost::openNormalizeWholeChart()
{
    if (navigationPending_) {
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

void QmlEditorPageHost::openBatchExport()
{
    openVideoExportPage(QStringLiteral("batch"));
}

bool QmlEditorPageHost::openCoverExport(int difficultyId)
{
    if (router() == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    const int selectedDifficultyId = difficultyId > 0 ? difficultyId : resumeDifficultyId_;
    return requestPageSwitch([this, selectedDifficultyId]() {
        // A direct export-page → cover-page navigation must release the video
        // session before cover becomes the owner of the Tools-menu difficulty.
        if (activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr) {
            exportSessionObject()->leave();
        }
        if (activePageId_ != QLatin1String("cover")) {
            activePageId_ = QStringLiteral("cover");
            emit activePageIdChanged();
        }
        emit coverPageRequested(selectedDifficultyId);
        return true;
    });
}

void QmlEditorPageHost::packAsZip()
{
    if (miacode::v2::EditorPageRouter* const pages = router(); pages != nullptr) {
        pages->packChartAsZip();
    }
}
