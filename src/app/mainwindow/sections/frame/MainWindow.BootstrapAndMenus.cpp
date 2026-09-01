#include "../../MainWindow.h"
#include "MainWindow.FrameSection.h"

MainWindow::FrameSection::FrameSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::FrameSection::setupMenusAndActions(
    QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    Q_UNUSED(fileMenu);
    Q_UNUSED(editMenu);
    Q_UNUSED(transformMenu);
    Q_UNUSED(previewMenu);
    Q_UNUSED(helpMenu);
}

void MainWindow::setupMenusAndActions(
    QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu)
{
    frameSection_->setupMenusAndActions(fileMenu, editMenu, transformMenu, previewMenu, helpMenu);
}
