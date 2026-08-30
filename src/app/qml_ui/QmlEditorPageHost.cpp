#include "QmlEditorPageHost.h"

#include "mainwindow/MainWindow.h"
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

QmlEditorPageHost::QmlEditorPageHost(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
    // The menu action and the chart.normalize shortcut land on MainWindow;
    // re-emit so the editor sees one request regardless of where it came from.
    connect(&backend, &MainWindow::normalizeWholeChartRequested, this, [this]() {
        openNormalizeWholeChart();
    });
    connect(&backend, &MainWindow::mediaToolsRequested, this, [this]() {
        openMediaProcessingTools();
    });
    connect(&backend, &MainWindow::preferencesRequested, this, [this]() {
        if (overlayActive()) {
            leaveOverlayPage();
        }
        emit preferencesRequested();
    });
}

QObject* QmlEditorPageHost::exportSession() const
{
    return backend_ != nullptr ? static_cast<QObject*>(backend_->qmlExportSession_) : nullptr;
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
    if (backend_ == nullptr) {
        return;
    }
    if (backend_->hasActiveDifficulty() && backend_->activeDifficultyId_ > 0) {
        resumeDifficultyId_ = backend_->activeDifficultyId_;
    }
}

bool QmlEditorPageHost::resumeChartOrMetadata()
{
    if (backend_ == nullptr) {
        return false;
    }
    if (resumeDifficultyId_ > 0 && backend_->switchToDifficultyField(resumeDifficultyId_)) {
        return true;
    }
    return backend_->switchToMetadataField();
}

bool QmlEditorPageHost::openVideoExportPage(const QString& tab)
{
    if (backend_ == nullptr || backend_->qmlExportSession_ == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    if (tab == QLatin1String("batch")) {
        backend_->qmlExportSession_->setActiveTab(QStringLiteral("batch"));
    } else {
        backend_->qmlExportSession_->setActiveTab(QStringLiteral("export"));
    }
    if (!backend_->switchToExportField()) {
        return false;
    }
    QTimer::singleShot(0, this, [this]() {
        markExportPageActive();
    });
    return true;
}

bool QmlEditorPageHost::openExportPage()
{
    return openVideoExportPage(QStringLiteral("export"));
}

bool QmlEditorPageHost::openLatencyPage()
{
    if (backend_ == nullptr) {
        return false;
    }
    rememberResumeDifficulty();
    if (!backend_->switchToLatencyField()) {
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
    if (backend_ == nullptr) {
        return false;
    }
    if (!overlayActive()) {
        return true;
    }

    if (activePageId_ == QLatin1String("export") && backend_->qmlExportSession_ != nullptr) {
        backend_->qmlExportSession_->leave();
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

void QmlEditorPageHost::openCoverExport()
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->onExportCover();
}

void QmlEditorPageHost::packAsZip()
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->onPackAsZip();
}
