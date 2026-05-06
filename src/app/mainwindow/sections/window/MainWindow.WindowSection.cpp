#include "MainWindow.WindowSection.h"

#include "common/DebugOptions.h"

#include <QApplication>

MainWindow::WindowSection::WindowSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        QObject::connect(app, &QApplication::focusChanged, &owner_, [this](QWidget* old, QWidget* now) {
            this->handleApplicationFocusChanged(old, now);
        });
        QObject::connect(app, &QGuiApplication::applicationStateChanged, &owner_, [this](Qt::ApplicationState state) {
            this->handleApplicationStateChanged(state);
        });
    }
}

bool MainWindow::quickShellRootWindowFrameGeometryAvailable() const
{
    return windowSection_->quickShellRootWindowFrameGeometryAvailable();
}

QRect MainWindow::quickShellRootWindowFrameGeometry() const
{
    return windowSection_->quickShellRootWindowFrameGeometry();
}

bool MainWindow::confirmShellClose()
{
    return windowSection_->confirmShellClose();
}

void MainWindow::toggleShellPreviewPlayback()
{
    windowSection_->toggleShellPreviewPlayback();
}

void MainWindow::stopShellPreview()
{
    windowSection_->stopShellPreview();
}

void MainWindow::seekShellPreview(double second)
{
    windowSection_->seekShellPreview(second);
}

void MainWindow::beginShellPreviewScrub()
{
    windowSection_->beginShellPreviewScrub();
}

void MainWindow::updateShellPreviewScrub(double second, bool centerView)
{
    windowSection_->updateShellPreviewScrub(second, centerView);
}

void MainWindow::endShellPreviewScrub(double second, bool centerView)
{
    windowSection_->endShellPreviewScrub(second, centerView);
}

void MainWindow::setShellPreviewRate(double rate)
{
    windowSection_->setShellPreviewRate(rate);
}

bool MainWindow::stepShellPreviewBySeconds(double deltaSeconds, bool centerView)
{
    return windowSection_->stepShellPreviewBySeconds(deltaSeconds, centerView);
}

void MainWindow::beginShellPreviewHeldSeek(int direction, int key)
{
    windowSection_->beginShellPreviewHeldSeek(direction, key);
}

void MainWindow::stopShellPreviewHeldSeek(int key)
{
    windowSection_->stopShellPreviewHeldSeek(key);
}

void MainWindow::setShellPreviewFullscreen(bool fullscreen)
{
    windowSection_->setShellPreviewFullscreen(fullscreen);
}

void MainWindow::setShellBottomTabsCurrentTab(const QString& tabId)
{
    windowSection_->setShellBottomTabsCurrentTab(tabId);
}

void MainWindow::navigateShellTimelineToSecond(double second)
{
    windowSection_->navigateShellTimelineToSecond(second);
}

void MainWindow::wheelShellTimelineNavigate(double second)
{
    windowSection_->wheelShellTimelineNavigate(second);
}

void MainWindow::centerShellTimelineNavigate(double second)
{
    windowSection_->centerShellTimelineNavigate(second);
}

void MainWindow::shellTimelineDragStarted()
{
    windowSection_->shellTimelineDragStarted();
}

void MainWindow::shellTimelineDragFinished(double second)
{
    windowSection_->shellTimelineDragFinished(second);
}

void MainWindow::shellTimelineUserInteractionStarted()
{
    windowSection_->shellTimelineUserInteractionStarted();
}

void MainWindow::shellTimelineSurfaceReady()
{
    windowSection_->shellTimelineSurfaceReady();
}

void MainWindow::shellTimelineFollowPreviewToggled(bool enabled)
{
    windowSection_->shellTimelineFollowPreviewToggled(enabled);
}

void MainWindow::shellTimelineFollowProgressToggled(bool enabled)
{
    windowSection_->shellTimelineFollowProgressToggled(enabled);
}

bool MainWindow::shellHasShortcut(const QKeySequence& sequence) const
{
    return windowSection_->shellHasShortcut(sequence);
}

bool MainWindow::shellTriggerShortcut(const QKeySequence& sequence)
{
    return windowSection_->shellTriggerShortcut(sequence);
}

QString MainWindow::shellWindowTitle() const
{
    return windowSection_->shellWindowTitle();
}

bool MainWindow::shellWorkspacePanelsSwapped() const
{
    return windowSection_->shellWorkspacePanelsSwapped();
}

QString MainWindow::shellPreviewSpeedLabel() const
{
    return windowSection_->shellPreviewSpeedLabel();
}

bool MainWindow::shellPreviewPlaying() const
{
    return windowSection_->shellPreviewPlaying();
}

double MainWindow::shellPreviewPositionSeconds() const
{
    return windowSection_->shellPreviewPositionSeconds();
}

double MainWindow::shellPreviewDurationSeconds() const
{
    return windowSection_->shellPreviewDurationSeconds();
}

QStringList MainWindow::shellPreviewStatsTexts() const
{
    return windowSection_->shellPreviewStatsTexts();
}

double MainWindow::shellPreviewCanvasAspectRatio() const
{
    return windowSection_->shellPreviewCanvasAspectRatio();
}

quint64 MainWindow::shellPreviewPaneRestoreGeneration() const
{
    return windowSection_->shellPreviewPaneRestoreGeneration();
}

bool MainWindow::shellPreviewFullscreen() const
{
    return windowSection_->shellPreviewFullscreen();
}

QObject* MainWindow::shellPreviewRuntimeObject() const
{
    return windowSection_->shellPreviewRuntimeObject();
}

QObject* MainWindow::shellPreviewStageMediaHostObject() const
{
    return windowSection_->shellPreviewStageMediaHostObject();
}

bool MainWindow::shellPreviewUsesSeparateSurface() const
{
    return windowSection_->shellPreviewUsesSeparateSurface();
}

QWindow* MainWindow::shellPreviewCompositeWindow() const
{
    return windowSection_->shellPreviewCompositeWindow();
}

QObject* MainWindow::shellTimelineStateBridgeObject() const
{
    return windowSection_->shellTimelineStateBridgeObject();
}

QString MainWindow::shellBottomTabsCurrentTabId() const
{
    return windowSection_->shellBottomTabsCurrentTabId();
}

bool MainWindow::shellBottomTabsVisible() const
{
    return windowSection_->shellBottomTabsVisible();
}

bool MainWindow::shellTimelineTabVisible() const
{
    return windowSection_->shellTimelineTabVisible();
}

bool MainWindow::shellValidationTabVisible() const
{
    return windowSection_->shellValidationTabVisible();
}

bool MainWindow::shellMuriTabVisible() const
{
    return windowSection_->shellMuriTabVisible();
}

QWidget* MainWindow::shellWindowWidget() const
{
    return windowSection_->shellWindowWidget();
}

QDockWidget* MainWindow::shellOutlineDockWidget() const
{
    return windowSection_->shellOutlineDockWidget();
}

bool MainWindow::shellOutlineDockCollapsed() const
{
    return windowSection_->shellOutlineDockCollapsed();
}

int MainWindow::shellOutlineDockExpandedWidth() const
{
    return windowSection_->shellOutlineDockExpandedWidth();
}

QWidget* MainWindow::shellWorkspaceWidget() const
{
    return windowSection_->shellWorkspaceWidget();
}

QWidget* MainWindow::shellBottomTabsWidget() const
{
    return windowSection_->shellBottomTabsWidget();
}

int MainWindow::shellBottomTabsHeight() const
{
    return windowSection_->shellBottomTabsHeight();
}

QWidget* MainWindow::shellPreviewPanelWidget() const
{
    return windowSection_->shellPreviewPanelWidget();
}

double MainWindow::shellNormalizedPreviewCanvasAspectRatio() const
{
    return windowSection_->shellNormalizedPreviewCanvasAspectRatio();
}

void MainWindow::shellRefreshLayoutAfterResize()
{
    windowSection_->shellRefreshLayoutAfterResize();
}

void MainWindow::shellSetRootWindowFrameGeometry(const QRect& geometry)
{
    windowSection_->shellSetRootWindowFrameGeometry(geometry);
}

void MainWindow::shellNoteQuickUiReady()
{
    windowSection_->shellNoteQuickUiReady();
}

void MainWindow::configureRuntimeDebugOutput()
{
    if (windowSection_ == nullptr) {
        runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
        return;
    }
    windowSection_->configureRuntimeDebugOutput();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::closeEvent(event);
        return;
    }
    windowSection_->closeEvent(event);
}

void MainWindow::onToggleFindReplace()
{
    windowSection_->onToggleFindReplace();
}

void MainWindow::onFindNext()
{
    windowSection_->onFindNext();
}

void MainWindow::onFindPrevious()
{
    windowSection_->onFindPrevious();
}

void MainWindow::onReplaceOne()
{
    windowSection_->onReplaceOne();
}

void MainWindow::onReplaceAll()
{
    windowSection_->onReplaceAll();
}

void MainWindow::refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    windowSection_->refreshQuickShellRehostedWidgetParent(widget);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (windowSection_ == nullptr) {
        return QMainWindow::eventFilter(watched, event);
    }
    return windowSection_->eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::resizeEvent(event);
        return;
    }
    windowSection_->resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::moveEvent(event);
        return;
    }
    windowSection_->moveEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::showEvent(event);
        return;
    }
    windowSection_->showEvent(event);
}

void MainWindow::hideEvent(QHideEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::hideEvent(event);
        return;
    }
    windowSection_->hideEvent(event);
}

bool MainWindow::event(QEvent* event)
{
    if (windowSection_ == nullptr) {
        return QMainWindow::event(event);
    }
    return windowSection_->event(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (windowSection_ == nullptr) {
        QMainWindow::changeEvent(event);
        return;
    }
    windowSection_->changeEvent(event);
}
