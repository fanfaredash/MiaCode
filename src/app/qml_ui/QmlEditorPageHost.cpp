#include "QmlEditorPageHost.h"

#include "common/DebugLog.h"
#include "app/qml_ui/export/QmlExportSession.h"

#include <QTimer>

namespace {

void appendPageHostLog(const QString& action, const QString& detail = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!detail.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + detail.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("qml_ui/page_host"),
        text);
}

} // namespace

QmlEditorPageHost::QmlEditorPageHost(miacode::v2::ShellNotifications& notifications,
                                     miacode::v2::EditorPageRouter*& routerSlot,
                                     QObject*& exportSessionSlot,
                                     QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
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
            leaveOverlayPage();
        }
        emit preferencesRequested();
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

bool QmlEditorPageHost::openVideoExportPage(const QString& tab)
{
    miacode::v2::EditorPageRouter* const pages = router();
    if (pages == nullptr || exportSessionObject() == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    if (tab == QLatin1String("batch")) {
        exportSessionObject()->setActiveTab(QStringLiteral("batch"));
    } else {
        exportSessionObject()->setActiveTab(QStringLiteral("export"));
    }
    if (!pages->enterExportPage()) {
        return false;
    }
    markExportPageActive();
    return true;
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
    if (!pages->enterLatencyPage()) {
        return false;
    }
    // The page is QML now; only the active id has to change so MainSplitView
    // shows it.
    if (activePageId_ != QLatin1String("latency")) {
        activePageId_ = QStringLiteral("latency");
        emit activePageIdChanged();
    }
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

    if (activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr) {
        exportSessionObject()->leave();
    }
    activePageId_.clear();
    emit activePageIdChanged();
    return resumeChartOrMetadata();
}

void QmlEditorPageHost::openMediaProcessingTools()
{
    if (overlayActive()) {
        leaveOverlayPage();
    }
    emit mediaToolsRequested();
}

void QmlEditorPageHost::openNormalizeWholeChart()
{
    if (overlayActive()) {
        leaveOverlayPage();
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
    const int selectedDifficultyId = difficultyId > 0 ? difficultyId
        : activePageId_ == QLatin1String("export") && exportSessionObject() != nullptr
            ? exportSessionObject()->selectedDifficultyId() : resumeDifficultyId_;
    emit coverWindowRequested(selectedDifficultyId);
    return true;
}

void QmlEditorPageHost::packAsZip()
{
    if (miacode::v2::EditorPageRouter* const pages = router(); pages != nullptr) {
        pages->packChartAsZip();
    }
}
