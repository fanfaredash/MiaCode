#pragma once

#include "../../MainWindow.h"

class MainWindow::WindowSection {
public:
    WindowSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    bool quickShellRootWindowFrameGeometryAvailable() const;
    QRect quickShellRootWindowFrameGeometry() const;
    bool confirmShellClose();
    void toggleShellPreviewPlayback();
    void stopShellPreview();
    void seekShellPreview(double second);
    void beginShellPreviewScrub();
    void updateShellPreviewScrub(double second, bool centerView);
    void endShellPreviewScrub(double second, bool centerView);
    void setShellPreviewRate(double rate);
    bool stepShellPreviewBySeconds(double deltaSeconds, bool centerView);
    void beginShellPreviewHeldSeek(int direction, int key);
    void stopShellPreviewHeldSeek(int key = 0);
    void setShellPreviewFullscreen(bool fullscreen);
    bool shellHasShortcut(const QKeySequence& sequence) const;
    bool shellTriggerShortcut(const QKeySequence& sequence);
    QString shellWindowTitle() const;
    bool shellWorkspacePanelsSwapped() const;
    QString shellPreviewSpeedLabel() const;
    bool shellPreviewPlaying() const;
    double shellPreviewPositionSeconds() const;
    double shellPreviewDurationSeconds() const;
    QStringList shellPreviewStatsTexts() const;
    bool shellPreviewFullscreen() const;
    QObject* shellPreviewRuntimeObject() const;
    QObject* shellPreviewStageMediaHostObject() const;
    bool shellPreviewUsesSeparateSurface() const;
    QWindow* shellPreviewCompositeWindow() const;
    QWidget* shellWindowWidget() const;
    QDockWidget* shellOutlineDockWidget() const;
    bool shellOutlineDockCollapsed() const;
    int shellOutlineDockExpandedWidth() const;
    QWidget* shellWorkspaceWidget() const;
    QWidget* shellPreviewPanelWidget() const;
    double shellNormalizedPreviewCanvasAspectRatio() const;
    void shellRefreshLayoutAfterResize();
    void shellSetRootWindowFrameGeometry(const QRect& geometry);
    void shellNoteQuickUiReady();

    void configureRuntimeDebugOutput();
    void setupInitialWindowGeometry();
    void applyUiTheme();
    void updateOutlineDockCollapseButton();
    void setOutlineDockCollapsed(bool collapsed);
    void applySystemWindowBackdrop(QWidget* target = nullptr) const;
    int computeBottomTabsDeviceHeight() const;
    void updateBottomTabsDeviceHeight();
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    void logTopLevelWindowSnapshot(const QString& tag);
    void closeEvent(QCloseEvent* event);
    void appendOutput(const QString& title, const QString& payload);
    QTextEdit* activeFindTarget() const;
    bool runFindInEditor(bool backward);
    void updateEditorFindBarGeometry();
    void applyFindOverlayInset();
    void hideFindReplaceBar();
    void onToggleFindReplace();
    void onFindNext();
    void onFindPrevious();
    void onReplaceOne();
    void onReplaceAll();
    QList<QAction*> quickShellShortcutActions() const;
    void refreshQuickShellRehostedWidgetParent(QWidget* widget);
    void setInvalidStarPreviewEasterEggEnabled(bool enabled);
    void ensureInvalidStarPreviewEasterEggSounds();
    void playInvalidStarPreviewEasterEggSound(bool enabled);
    bool eventFilter(QObject* watched, QEvent* event);
    void resizeEvent(QResizeEvent* event);
    void moveEvent(QMoveEvent* event);
    void showEvent(QShowEvent* event);
    void hideEvent(QHideEvent* event);
    bool event(QEvent* event);
    void changeEvent(QEvent* event);

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
