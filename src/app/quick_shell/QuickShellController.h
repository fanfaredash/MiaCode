#pragma once

#include "QuickShellContracts.h"

#include <QObject>
#include <QKeySequence>
#include <QWindow>

class QTimer;
class QuickShellNativeSurfaceHost;

class QuickShellController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY shellStateChanged)
    Q_PROPERTY(bool workspacePanelsSwapped READ workspacePanelsSwapped NOTIFY shellStateChanged)
    Q_PROPERTY(QString previewSpeedLabel READ previewSpeedLabel NOTIFY shellStateChanged)
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY shellStateChanged)
    Q_PROPERTY(double previewPositionSeconds READ previewPositionSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(double previewDurationSeconds READ previewDurationSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(QStringList previewStatsTexts READ previewStatsTexts NOTIFY shellStateChanged)
    Q_PROPERTY(double previewCanvasAspectRatio READ previewCanvasAspectRatio NOTIFY shellStateChanged)
    Q_PROPERTY(qulonglong previewPaneRestoreGeneration READ previewPaneRestoreGeneration NOTIFY shellStateChanged)
    Q_PROPERTY(double previewSeekSingleStepSeconds READ previewSeekSingleStepSeconds CONSTANT)
    Q_PROPERTY(bool previewFullscreen READ previewFullscreen WRITE setPreviewFullscreen NOTIFY previewFullscreenChanged)
    Q_PROPERTY(QObject* previewRuntime READ previewRuntime CONSTANT)
    Q_PROPERTY(QObject* previewStageMediaHost READ previewStageMediaHost CONSTANT)
    Q_PROPERTY(QWindow* previewCompositeWindow READ previewCompositeWindow CONSTANT)
    Q_PROPERTY(bool previewUsesSeparateSurface READ previewUsesSeparateSurface NOTIFY shellStateChanged)
    // Phase 4c — env-driven flag, exposed read-only to QML so siblings of
    // the DComp surface (PreviewStageMediaItem in particular) can hide
    // themselves to avoid double-rendering when DComp is the
    // authoritative chart renderer.
    Q_PROPERTY(bool previewDCompExclusive READ previewDCompExclusive CONSTANT)
    Q_PROPERTY(QObject* timelineStateBridge READ timelineStateBridge CONSTANT)
    Q_PROPERTY(bool timelineSurfaceReady READ timelineSurfaceReady NOTIFY shellStateChanged)
    Q_PROPERTY(QString bottomTabsCurrentTabId READ bottomTabsCurrentTabId NOTIFY shellStateChanged)
    Q_PROPERTY(bool bottomTabsVisible READ bottomTabsVisible NOTIFY shellStateChanged)
    Q_PROPERTY(bool timelineTabVisible READ timelineTabVisible NOTIFY shellStateChanged)
    Q_PROPERTY(bool validationTabVisible READ validationTabVisible NOTIFY shellStateChanged)
    Q_PROPERTY(bool muriTabVisible READ muriTabVisible NOTIFY shellStateChanged)
    Q_PROPERTY(QWindow* topChromeWindow READ topChromeWindow CONSTANT)
    Q_PROPERTY(QWindow* sidebarWindow READ sidebarWindow CONSTANT)
    Q_PROPERTY(QWindow* workspaceWindow READ workspaceWindow CONSTANT)
    Q_PROPERTY(QWindow* bottomTabsWindow READ bottomTabsWindow CONSTANT)
    Q_PROPERTY(QWindow* statusWindow READ statusWindow CONSTANT)

public:
    QuickShellController(
        QuickShellCommandSink* commandSink,
        QuickShellStateSource* stateSource,
        QuickShellNativeSurfaceHost* surfaceHost,
        QObject* parent = nullptr
    );

    QString windowTitle() const;
    bool workspacePanelsSwapped() const;
    QString previewSpeedLabel() const;
    bool previewPlaying() const;
    double previewPositionSeconds() const;
    double previewDurationSeconds() const;
    QStringList previewStatsTexts() const;
    double previewCanvasAspectRatio() const;
    qulonglong previewPaneRestoreGeneration() const;
    double previewSeekSingleStepSeconds() const;
    bool previewFullscreen() const;
    QObject* previewRuntime() const;
    QObject* previewStageMediaHost() const;
    QWindow* previewCompositeWindow() const;
    bool previewUsesSeparateSurface() const;
    bool previewDCompExclusive() const;
    QObject* timelineStateBridge() const;
    bool timelineSurfaceReady() const;
    QString bottomTabsCurrentTabId() const;
    bool bottomTabsVisible() const;
    bool timelineTabVisible() const;
    bool validationTabVisible() const;
    bool muriTabVisible() const;
    QWindow* topChromeWindow() const;
    QWindow* sidebarWindow() const;
    QWindow* workspaceWindow() const;
    QWindow* bottomTabsWindow() const;
    QWindow* statusWindow() const;

    void setPreviewFullscreen(bool fullscreen);
    void markNextCloseConfirmedExternally();
    void clearPendingExternalCloseConfirmation();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void togglePreviewPlayback();
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void seekPreview(double second);
    Q_INVOKABLE void beginPreviewScrub();
    Q_INVOKABLE void updatePreviewScrub(double second, bool centerView = true);
    Q_INVOKABLE void endPreviewScrub(double second, bool centerView = true);
    Q_INVOKABLE void setPreviewRate(double rate);
    Q_INVOKABLE void setBottomTabsCurrentTabId(const QString& tabId);
    Q_INVOKABLE void timelineHeaderNavigate(double second);
    Q_INVOKABLE void timelineWheelNavigate(double second);
    Q_INVOKABLE void timelineCenterNavigate(double second);
    Q_INVOKABLE void timelineDragStarted();
    Q_INVOKABLE void timelineDragFinished(double second);
    Q_INVOKABLE void timelineUserInteractionStarted();
    Q_INVOKABLE void noteTimelineSurfaceReady();
    Q_INVOKABLE void timelineFollowPreviewToggled(bool enabled);
    Q_INVOKABLE bool stepPreviewBySeconds(double deltaSeconds, bool centerView = true);
    Q_INVOKABLE void beginPreviewHeldSeek(int direction, int key);
    Q_INVOKABLE void stopPreviewHeldSeek(int key = 0);
    Q_INVOKABLE QString formatPreviewTimestamp(double second) const;
    Q_INVOKABLE void logPreviewInteraction(const QString& action, const QString& payload = QString());
    Q_INVOKABLE void logShellLifecycle(const QString& action, const QString& payload = QString());
    Q_INVOKABLE void notifyRootCloseAccepted(const QString& source);
    Q_INVOKABLE void syncTopChromeSurfaceSize(int width, int height);
    Q_INVOKABLE void syncSidebarSurfaceSize(int width, int height);
    Q_INVOKABLE void syncWorkspaceSurfaceSize(int width, int height);
    Q_INVOKABLE void syncBottomTabsSurfaceSize(int width, int height);
    Q_INVOKABLE void syncBottomTabsToastAnchor(int x, int y, int width, int height, bool visible);
    Q_INVOKABLE void syncStatusSurfaceSize(int width, int height);
    bool hasShortcut(const QKeySequence& sequence) const;
    bool triggerShortcut(const QKeySequence& sequence);

signals:
    void shellStateChanged();
    void previewFullscreenChanged();
    void rootCloseAccepted(const QString& source);

private:
    void refreshFromStateSource();
    void updateRefreshTimerInterval();

    QuickShellCommandSink* commandSink_ = nullptr;
    QuickShellStateSource* stateSource_ = nullptr;
    QuickShellNativeSurfaceHost* surfaceHost_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QString windowTitle_;
    bool workspacePanelsSwapped_ = false;
    QString previewSpeedLabel_;
    bool previewPlaying_ = false;
    double previewPositionSeconds_ = 0.0;
    double previewDurationSeconds_ = 0.0;
    QStringList previewStatsTexts_;
    double previewCanvasAspectRatio_ = 1.0;
    quint64 previewPaneRestoreGeneration_ = 0;
    bool previewFullscreen_ = false;
    bool previewUsesSeparateSurface_ = false;
    bool timelineSurfaceReady_ = false;
    QString bottomTabsCurrentTabId_;
    bool bottomTabsVisible_ = false;
    bool timelineTabVisible_ = false;
    bool validationTabVisible_ = false;
    bool muriTabVisible_ = false;
    bool previewSpeedToastInitialized_ = false;
    bool closeConfirmedExternally_ = false;
};
