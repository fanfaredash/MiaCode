#pragma once

#include <QKeySequence>
#include <QRect>
#include <QString>
#include <QStringList>

class QDockWidget;
class QObject;
class QWidget;
class QWindow;

struct QuickShellNativeSurfaceBundle {
    QWindow* topChrome = nullptr;
    QWindow* sidebar = nullptr;
    QWindow* workspace = nullptr;
    QWindow* bottomTabs = nullptr;
    QWindow* status = nullptr;
    QWindow* previewCompositeWindow = nullptr;
};

class QuickShellCommandSink
{
public:
    virtual ~QuickShellCommandSink() = default;

    virtual bool confirmShellClose() = 0;
    virtual void toggleShellPreviewPlayback() = 0;
    virtual void stopShellPreview() = 0;
    virtual void seekShellPreview(double second) = 0;
    virtual void beginShellPreviewScrub() = 0;
    virtual void updateShellPreviewScrub(double second, bool centerView) = 0;
    virtual void endShellPreviewScrub(double second, bool centerView) = 0;
    virtual void setShellPreviewRate(double rate) = 0;
    virtual bool stepShellPreviewBySeconds(double deltaSeconds, bool centerView) = 0;
    virtual void beginShellPreviewHeldSeek(int direction, int key) = 0;
    virtual void stopShellPreviewHeldSeek(int key = 0) = 0;
    virtual void setShellPreviewFullscreen(bool fullscreen) = 0;
    virtual void setShellBottomTabsHeight(int height) = 0;
    virtual void setShellBottomTabsCurrentTab(const QString& tabId) = 0;
    virtual void navigateShellTimelineToSecond(double second) = 0;
    virtual void wheelShellTimelineNavigate(double second) = 0;
    virtual void centerShellTimelineNavigate(double second) = 0;
    virtual void shellTimelineDragStarted() = 0;
    virtual void shellTimelineDragFinished(double second) = 0;
    virtual void shellTimelineUserInteractionStarted() = 0;
    virtual void shellTimelineSurfaceReady() = 0;
    virtual void shellTimelineFollowPreviewToggled(bool enabled) = 0;
    virtual void shellTimelineViewportLockToggled(bool enabled) = 0;
    virtual void shellTimelineFollowProgressToggled(bool enabled) = 0;
    virtual void shellTimelineSyncToggled(bool enabled) = 0;
    virtual bool shellHasShortcut(const QKeySequence& sequence) const = 0;
    virtual bool shellTriggerShortcut(const QKeySequence& sequence) = 0;
};

class QuickShellStateSource
{
public:
    virtual ~QuickShellStateSource() = default;

    virtual QString shellWindowTitle() const = 0;
    virtual bool shellWorkspacePanelsSwapped() const = 0;
    virtual QString shellPreviewSpeedLabel() const = 0;
    virtual bool shellPreviewPlaying() const = 0;
    virtual double shellPreviewPositionSeconds() const = 0;
    virtual double shellPreviewDurationSeconds() const = 0;
    virtual QStringList shellPreviewStatsTexts() const = 0;
    virtual double shellPreviewCanvasAspectRatio() const = 0;
    virtual quint64 shellPreviewPaneRestoreGeneration() const = 0;
    virtual bool shellPreviewFullscreen() const = 0;
    // Inline export progress (an export launched from the Export page's
    // embedded video panel, A3 as amended 2026-06-11): the CHART TIME the
    // worker has rendered up to (exportStart + percent × duration), < 0 when
    // no inline export progress is showing. The QML transport rides its
    // normal playback slider/time display with this value (seeking disabled
    // while active); stage/ETA text goes to the shared status bar instead.
    virtual double shellVideoExportProgressSeconds() const { return -1.0; }
    virtual QObject* shellPreviewRuntimeObject() const = 0;
    virtual QObject* shellPreviewStageMediaHostObject() const = 0;
    virtual bool shellPreviewUsesSeparateSurface() const = 0;
    virtual QWindow* shellPreviewCompositeWindow() const = 0;
    virtual QObject* shellTimelineStateBridgeObject() const = 0;
    virtual bool shellTimelineSurfaceReady() const = 0;
    virtual QString shellBottomTabsCurrentTabId() const = 0;
    virtual int shellBottomTabsHeight() const = 0;
    virtual double shellBottomTabsHeaderScale() const = 0;
    virtual bool shellBottomTabsVisible() const = 0;
    virtual bool shellTimelineTabVisible() const = 0;
    virtual bool shellValidationTabVisible() const = 0;
    virtual bool shellMuriTabVisible() const = 0;
};

class QuickShellNativeContentProvider
{
public:
    virtual ~QuickShellNativeContentProvider() = default;

    virtual QWidget* shellWindowWidget() const = 0;
    virtual QDockWidget* shellOutlineDockWidget() const = 0;
    virtual bool shellOutlineDockCollapsed() const = 0;
    virtual int shellOutlineDockExpandedWidth() const = 0;
    virtual QWidget* shellWorkspaceWidget() const = 0;
    virtual QWidget* shellBottomTabsWidget() const = 0;
    virtual int shellBottomTabsHeight() const = 0;
    virtual QWidget* shellPreviewPanelWidget() const = 0;
    virtual double shellNormalizedPreviewCanvasAspectRatio() const = 0;
    virtual void shellRefreshLayoutAfterResize() = 0;
    virtual void shellSetRootWindowFrameGeometry(const QRect& geometry) = 0;
    virtual void shellNoteQuickUiReady() = 0;
};
