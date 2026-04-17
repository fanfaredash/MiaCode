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
    virtual bool shellPreviewFullscreen() const = 0;
    virtual QObject* shellPreviewRuntimeObject() const = 0;
    virtual QObject* shellPreviewStageMediaHostObject() const = 0;
    virtual bool shellPreviewUsesSeparateSurface() const = 0;
    virtual QWindow* shellPreviewCompositeWindow() const = 0;
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
    virtual QWidget* shellPreviewPanelWidget() const = 0;
    virtual double shellNormalizedPreviewCanvasAspectRatio() const = 0;
    virtual void shellRefreshLayoutAfterResize() = 0;
    virtual void shellSetRootWindowFrameGeometry(const QRect& geometry) = 0;
    virtual void shellNoteQuickUiReady() = 0;
};
