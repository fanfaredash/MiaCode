#include "MainWindow.WindowSection.h"

MainWindow::WindowSection::WindowSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

bool MainWindow::WindowSection::confirmShellClose()
{
    return owner_.confirmShellClose();
}

void MainWindow::WindowSection::toggleShellPreviewPlayback()
{
    owner_.toggleShellPreviewPlayback();
}

void MainWindow::WindowSection::stopShellPreview()
{
    owner_.stopShellPreview();
}

void MainWindow::WindowSection::seekShellPreview(double second)
{
    owner_.seekShellPreview(second);
}

void MainWindow::WindowSection::setShellPreviewRate(double rate)
{
    owner_.setShellPreviewRate(rate);
}

void MainWindow::WindowSection::setShellPreviewFullscreen(bool fullscreen)
{
    owner_.setShellPreviewFullscreen(fullscreen);
}

bool MainWindow::WindowSection::shellHasShortcut(const QKeySequence& sequence) const
{
    return owner_.shellHasShortcut(sequence);
}

bool MainWindow::WindowSection::shellTriggerShortcut(const QKeySequence& sequence)
{
    return owner_.shellTriggerShortcut(sequence);
}

QString MainWindow::WindowSection::shellWindowTitle() const
{
    return owner_.shellWindowTitle();
}

bool MainWindow::WindowSection::shellWorkspacePanelsSwapped() const
{
    return owner_.shellWorkspacePanelsSwapped();
}

QString MainWindow::WindowSection::shellPreviewSpeedLabel() const
{
    return owner_.shellPreviewSpeedLabel();
}

bool MainWindow::WindowSection::shellPreviewPlaying() const
{
    return owner_.shellPreviewPlaying();
}

double MainWindow::WindowSection::shellPreviewPositionSeconds() const
{
    return owner_.shellPreviewPositionSeconds();
}

double MainWindow::WindowSection::shellPreviewDurationSeconds() const
{
    return owner_.shellPreviewDurationSeconds();
}

bool MainWindow::WindowSection::shellPreviewFullscreen() const
{
    return owner_.shellPreviewFullscreen();
}

QObject* MainWindow::WindowSection::shellPreviewRuntimeObject() const
{
    return owner_.shellPreviewRuntimeObject();
}

QObject* MainWindow::WindowSection::shellPreviewStageMediaHostObject() const
{
    return owner_.shellPreviewStageMediaHostObject();
}

bool MainWindow::WindowSection::shellPreviewUsesSeparateSurface() const
{
    return owner_.shellPreviewUsesSeparateSurface();
}

QWindow* MainWindow::WindowSection::shellPreviewCompositeWindow() const
{
    return owner_.shellPreviewCompositeWindow();
}

QWidget* MainWindow::WindowSection::shellWindowWidget() const
{
    return owner_.shellWindowWidget();
}

QDockWidget* MainWindow::WindowSection::shellOutlineDockWidget() const
{
    return owner_.shellOutlineDockWidget();
}

bool MainWindow::WindowSection::shellOutlineDockCollapsed() const
{
    return owner_.shellOutlineDockCollapsed();
}

int MainWindow::WindowSection::shellOutlineDockExpandedWidth() const
{
    return owner_.shellOutlineDockExpandedWidth();
}

QWidget* MainWindow::WindowSection::shellWorkspaceWidget() const
{
    return owner_.shellWorkspaceWidget();
}

QWidget* MainWindow::WindowSection::shellPreviewPanelWidget() const
{
    return owner_.shellPreviewPanelWidget();
}

QString MainWindow::WindowSection::shellPreviewPanelStyleSheet() const
{
    return owner_.shellPreviewPanelStyleSheet();
}

QWidget* MainWindow::WindowSection::shellPreviewControlCardWidget() const
{
    return owner_.shellPreviewControlCardWidget();
}

QWidget* MainWindow::WindowSection::shellPreviewStatsCardWidget() const
{
    return owner_.shellPreviewStatsCardWidget();
}

QLabel* MainWindow::WindowSection::shellPreviewTotalStatsLabel() const
{
    return owner_.shellPreviewTotalStatsLabel();
}

QGridLayout* MainWindow::WindowSection::shellPreviewStatsGridLayout() const
{
    return owner_.shellPreviewStatsGridLayout();
}

int MainWindow::WindowSection::shellPreviewStatsMinimumPanelHeight(int panelWidth) const
{
    return owner_.shellPreviewStatsMinimumPanelHeight(panelWidth);
}

int MainWindow::WindowSection::shellUpdatePreviewStatsLayout(int hostWidth)
{
    return owner_.shellUpdatePreviewStatsLayout(hostWidth);
}

double MainWindow::WindowSection::shellNormalizedPreviewCanvasAspectRatio() const
{
    return owner_.shellNormalizedPreviewCanvasAspectRatio();
}

void MainWindow::WindowSection::shellRefreshLayoutAfterResize()
{
    owner_.shellRefreshLayoutAfterResize();
}

void MainWindow::WindowSection::shellSetRootWindowFrameGeometry(const QRect& geometry)
{
    owner_.shellSetRootWindowFrameGeometry(geometry);
}

void MainWindow::WindowSection::shellNoteQuickUiReady()
{
    owner_.shellNoteQuickUiReady();
}

void MainWindow::WindowSection::applyUiTheme()
{
    owner_.applyUiTheme();
}

void MainWindow::WindowSection::updateOutlineDockCollapseButton()
{
    owner_.updateOutlineDockCollapseButton();
}

void MainWindow::WindowSection::setOutlineDockCollapsed(bool collapsed)
{
    owner_.setOutlineDockCollapsed(collapsed);
}

void MainWindow::WindowSection::applySystemWindowBackdrop(QWidget* target) const
{
    owner_.applySystemWindowBackdrop(target);
}

int MainWindow::WindowSection::computeBottomTabsDeviceHeight() const
{
    return owner_.computeBottomTabsDeviceHeight();
}

void MainWindow::WindowSection::updateBottomTabsDeviceHeight()
{
    owner_.updateBottomTabsDeviceHeight();
}

QString MainWindow::WindowSection::formatWindowStateFlags(Qt::WindowStates states) const
{
    return owner_.formatWindowStateFlags(states);
}

void MainWindow::WindowSection::logWindowGeometryDebug(const QString& tag, const QString& detail)
{
    owner_.logWindowGeometryDebug(tag, detail);
}

void MainWindow::WindowSection::logTopLevelWindowSnapshot(const QString& tag)
{
    owner_.logTopLevelWindowSnapshot(tag);
}

void MainWindow::WindowSection::closeEvent(QCloseEvent* event)
{
    owner_.closeEvent(event);
}

void MainWindow::WindowSection::appendOutput(const QString& title, const QString& payload)
{
    owner_.appendOutput(title, payload);
}

QTextEdit* MainWindow::WindowSection::activeFindTarget() const
{
    return owner_.activeFindTarget();
}

bool MainWindow::WindowSection::runFindInEditor(bool backward)
{
    return owner_.runFindInEditor(backward);
}

void MainWindow::WindowSection::updateEditorFindBarGeometry()
{
    owner_.updateEditorFindBarGeometry();
}

void MainWindow::WindowSection::applyFindOverlayInset()
{
    owner_.applyFindOverlayInset();
}

void MainWindow::WindowSection::hideFindReplaceBar()
{
    owner_.hideFindReplaceBar();
}

void MainWindow::WindowSection::onToggleFindReplace()
{
    owner_.onToggleFindReplace();
}

void MainWindow::WindowSection::onFindNext()
{
    owner_.onFindNext();
}

void MainWindow::WindowSection::onFindPrevious()
{
    owner_.onFindPrevious();
}

void MainWindow::WindowSection::onReplaceOne()
{
    owner_.onReplaceOne();
}

void MainWindow::WindowSection::onReplaceAll()
{
    owner_.onReplaceAll();
}

QList<QAction*> MainWindow::WindowSection::quickShellShortcutActions() const
{
    return owner_.quickShellShortcutActions();
}

void MainWindow::WindowSection::refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    owner_.refreshQuickShellRehostedWidgetParent(widget);
}

void MainWindow::WindowSection::setInvalidStarPreviewEasterEggEnabled(bool enabled)
{
    owner_.setInvalidStarPreviewEasterEggEnabled(enabled);
}

void MainWindow::WindowSection::ensureInvalidStarPreviewEasterEggSounds()
{
    owner_.ensureInvalidStarPreviewEasterEggSounds();
}

void MainWindow::WindowSection::playInvalidStarPreviewEasterEggSound(bool enabled)
{
    owner_.playInvalidStarPreviewEasterEggSound(enabled);
}

bool MainWindow::WindowSection::eventFilter(QObject* watched, QEvent* event)
{
    return owner_.eventFilter(watched, event);
}

void MainWindow::WindowSection::resizeEvent(QResizeEvent* event)
{
    owner_.resizeEvent(event);
}

void MainWindow::WindowSection::moveEvent(QMoveEvent* event)
{
    owner_.moveEvent(event);
}

void MainWindow::WindowSection::showEvent(QShowEvent* event)
{
    owner_.showEvent(event);
}

void MainWindow::WindowSection::hideEvent(QHideEvent* event)
{
    owner_.hideEvent(event);
}

bool MainWindow::WindowSection::event(QEvent* event)
{
    return owner_.event(event);
}

void MainWindow::WindowSection::changeEvent(QEvent* event)
{
    owner_.changeEvent(event);
}
