#include "QmlEditorPageHost.h"

#include "mainwindow/MainWindow.h"
#include "UiTheme.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "common/DebugLog.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "tools/latency/LatencyDetectionPage.h"

#include <QBoxLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QWindow>

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

void applySurfaceStyle(QWidget* surface)
{
    if (surface == nullptr) {
        return;
    }
    const UiTheme::Colors& colors = UiTheme::colors();
    QPalette palette = surface->palette();
    palette.setColor(QPalette::Window, colors.windowBg);
    surface->setPalette(palette);
    surface->setAutoFillBackground(true);
}

void activateSurfaceLayout(QWidget* surface)
{
    if (surface == nullptr) {
        return;
    }
    if (QLayout* layout = surface->layout(); layout != nullptr) {
        layout->activate();
    }
    surface->updateGeometry();
    surface->update();
}

} // namespace

QmlEditorPageHost::QmlEditorPageHost(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
    // Eager surface so QML WindowContainer can bind pageWindow before the
    // first overlay open: the native window must exist before QML tries to
    // wrap it, so it is created here in the constructor rather than lazily
    // on first use.
    ensureSurface();
}

QmlEditorPageHost::~QmlEditorPageHost()
{
    detachCurrentPage(true);
    if (pageWindow_ != nullptr) {
        pageWindow_->setParent(nullptr);
        delete pageWindow_.data();
        pageWindow_ = nullptr;
    }
    delete surfaceWidget_;
    surfaceWidget_ = nullptr;
    surfaceLayout_ = nullptr;
}

QWindow* QmlEditorPageHost::pageWindow() const
{
    return pageWindow_.data();
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
    // Detach latency (or any) full-page widget before marking export active —
    // export chrome is QML, not this WindowContainer surface.
    if (attachedPage_ != nullptr) {
        detachCurrentPage(true);
    } else if (!activePageId_.isEmpty()) {
        activePageId_.clear();
        emit activePageIdChanged();
    }
    activePageId_ = QStringLiteral("export");
    emit activePageIdChanged();
}

void QmlEditorPageHost::ensureSurface()
{
    if (surfaceWidget_ != nullptr) {
        return;
    }

    surfaceWidget_ = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint);
    surfaceWidget_->setObjectName(QStringLiteral("QmlUiEditorPageSurface"));
    surfaceWidget_->setAttribute(Qt::WA_NativeWindow);
    surfaceWidget_->setAttribute(Qt::WA_StyledBackground, true);
    surfaceWidget_->setFocusPolicy(Qt::StrongFocus);
    surfaceWidget_->setContentsMargins(0, 0, 0, 0);
    surfaceWidget_->setMinimumSize(QSize(64, 64));
    surfaceWidget_->resize(960, 720);
    applySurfaceStyle(surfaceWidget_);
    // hide() before winId() — same flash-avoidance contract as QuickShell.
    surfaceWidget_->hide();
    surfaceWidget_->winId();

    surfaceLayout_ = new QVBoxLayout(surfaceWidget_);
    surfaceLayout_->setContentsMargins(0, 0, 0, 0);
    surfaceLayout_->setSpacing(0);

    pageWindow_ = QWindow::fromWinId(surfaceWidget_->winId());
    if (pageWindow_ != nullptr) {
        pageWindow_->QObject::setParent(this);
        miacode::ui::bindAdoptedSurfaceWindow(surfaceWidget_, pageWindow_.data());
    }
    appendPageHostLog(
        QStringLiteral("surface_ready"),
        QStringLiteral("window=%1").arg(pageWindow_ != nullptr ? 1 : 0));
    emit pageWindowChanged();
}

void QmlEditorPageHost::setSurfaceVisible(bool visible)
{
    if (surfaceWidget_ == nullptr) {
        return;
    }
#ifdef Q_OS_MACOS
    // After WindowContainer adoption, QWidget::show/hide on the Qt::Tool
    // panel can rip the content NSView back out of the Quick window.
    // Keep the bridge permanently shown; visibility is driven by the
    // foreign QWindow / WindowContainer only.
    if (visible && !surfaceWidget_->isVisible()) {
        surfaceWidget_->show();
    }
#else
    if (visible) {
        if (!surfaceWidget_->isVisible()) {
            surfaceWidget_->show();
        }
    } else if (surfaceWidget_->isVisible()) {
        surfaceWidget_->hide();
    }
#endif
    if (pageWindow_ != nullptr && pageWindow_->isVisible() != visible) {
        pageWindow_->setVisible(visible);
    }
}

bool QmlEditorPageHost::attachPageWidget(QWidget* page, const QString& pageId)
{
    if (page == nullptr || backend_ == nullptr) {
        return false;
    }

    ensureSurface();
    if (surfaceWidget_ == nullptr || surfaceLayout_ == nullptr || pageWindow_ == nullptr) {
        appendPageHostLog(QStringLiteral("attach_failed"), pageId);
        return false;
    }

    if (attachedPage_ == page && activePageId_ == pageId) {
        applySurfaceStyle(surfaceWidget_);
        page->show();
        setSurfaceVisible(true);
        activateSurfaceLayout(surfaceWidget_);
        return true;
    }

    detachCurrentPage(true);

    if (backend_->editorStack_ != nullptr && page->parentWidget() == backend_->editorStack_) {
        backend_->editorStack_->removeWidget(page);
    }
    surfaceLayout_->addWidget(page);
    page->show();
    attachedPage_ = page;
    applySurfaceStyle(surfaceWidget_);
    activateSurfaceLayout(surfaceWidget_);

    // Flip overlayActive first so WindowContainer adopts the HWND, then show
    // the bridge widget (Windows needs QWidget::show for paint; after adoption
    // the HWND is already reparented into the Quick window — no floating Tool).
    if (activePageId_ != pageId) {
        activePageId_ = pageId;
        emit activePageIdChanged();
    }
    setSurfaceVisible(true);

    appendPageHostLog(
        QStringLiteral("attach_ok"),
        QStringLiteral("page=%1 size=%2x%3")
            .arg(pageId)
            .arg(surfaceWidget_->width())
            .arg(surfaceWidget_->height()));
    return true;
}

void QmlEditorPageHost::detachCurrentPage(bool restoreToEditorStack)
{
    if (attachedPage_ == nullptr) {
        return;
    }

    QWidget* page = attachedPage_.data();
    attachedPage_ = nullptr;
    if (surfaceLayout_ != nullptr) {
        surfaceLayout_->removeWidget(page);
    }
    page->setParent(nullptr);

    if (restoreToEditorStack && backend_ != nullptr && backend_->editorStack_ != nullptr) {
        backend_->editorStack_->addWidget(page);
    }

    setSurfaceVisible(false);

    if (!activePageId_.isEmpty()) {
        activePageId_.clear();
        emit activePageIdChanged();
    }
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
    if (!attachPageWidget(backend_->latencyDetectionPage_, QStringLiteral("latency"))) {
        return false;
    }
    QTimer::singleShot(0, this, [this]() {
        if (surfaceWidget_ != nullptr && attachedPage_ != nullptr) {
            syncPageSize(surfaceWidget_->width(), surfaceWidget_->height());
            activateSurfaceLayout(surfaceWidget_);
            attachedPage_->update();
        }
    });
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

    const bool leavingExport = activePageId_ == QLatin1String("export");
    if (leavingExport) {
        if (!activePageId_.isEmpty()) {
            activePageId_.clear();
            emit activePageIdChanged();
        }
    } else {
        detachCurrentPage(true);
    }
    // switchToDifficultyField captures exportPreviewAuditionActive_ to restore
    // playhead and 代码跟随. leave() tears that flag down, so it runs after
    // resume — and again here if resume never reached a field switch.
    const bool resumed = resumeChartOrMetadata();
    if (leavingExport && backend_->qmlExportSession_ != nullptr) {
        backend_->qmlExportSession_->leave();
    }
    return resumed;
}

void QmlEditorPageHost::openMediaProcessingTools()
{
    if (overlayActive()) {
        leaveOverlayPage();
    }
    if (backend_ != nullptr) {
        backend_->onMediaProcessingTools();
    }
}

void QmlEditorPageHost::openNormalizeWholeChart()
{
    if (overlayActive()) {
        leaveOverlayPage();
    }
    if (backend_ != nullptr) {
        backend_->onNormalizeWholeChart();
    }
}

void QmlEditorPageHost::openNetBatchDownload()
{
    if (overlayActive()) {
        leaveOverlayPage();
    }
    if (backend_ != nullptr) {
        backend_->onNetBatchDownload();
    }
}

void QmlEditorPageHost::openNetBatchUpload()
{
    if (overlayActive()) {
        leaveOverlayPage();
    }
    if (backend_ != nullptr) {
        backend_->onNetBatchUpload();
    }
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

void QmlEditorPageHost::syncPageSize(int width, int height)
{
    if (surfaceWidget_ == nullptr) {
        return;
    }
    const int w = qMax(64, width);
    const int h = qMax(64, height);
    const QSize nextSize(w, h);
    if (surfaceWidget_->size() != nextSize) {
        surfaceWidget_->resize(nextSize);
    }
    if (attachedPage_ != nullptr && attachedPage_->size() != nextSize) {
        attachedPage_->resize(nextSize);
    }
    activateSurfaceLayout(surfaceWidget_);
    if (attachedPage_ != nullptr) {
        attachedPage_->update();
    }
}
