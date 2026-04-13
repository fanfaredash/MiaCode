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
    Q_PROPERTY(double previewSeekSingleStepSeconds READ previewSeekSingleStepSeconds CONSTANT)
    Q_PROPERTY(bool previewFullscreen READ previewFullscreen WRITE setPreviewFullscreen NOTIFY previewFullscreenChanged)
    Q_PROPERTY(QObject* previewRuntime READ previewRuntime CONSTANT)
    Q_PROPERTY(QObject* previewStageMediaHost READ previewStageMediaHost CONSTANT)
    Q_PROPERTY(QWindow* previewCompositeWindow READ previewCompositeWindow CONSTANT)
    Q_PROPERTY(bool previewUsesSeparateSurface READ previewUsesSeparateSurface NOTIFY shellStateChanged)
    Q_PROPERTY(QWindow* topChromeWindow READ topChromeWindow CONSTANT)
    Q_PROPERTY(QWindow* sidebarWindow READ sidebarWindow CONSTANT)
    Q_PROPERTY(QWindow* workspaceWindow READ workspaceWindow CONSTANT)
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
    double previewSeekSingleStepSeconds() const;
    bool previewFullscreen() const;
    QObject* previewRuntime() const;
    QObject* previewStageMediaHost() const;
    QWindow* previewCompositeWindow() const;
    bool previewUsesSeparateSurface() const;
    QWindow* topChromeWindow() const;
    QWindow* sidebarWindow() const;
    QWindow* workspaceWindow() const;
    QWindow* statusWindow() const;

    void setPreviewFullscreen(bool fullscreen);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void togglePreviewPlayback();
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void seekPreview(double second);
    Q_INVOKABLE void beginPreviewScrub();
    Q_INVOKABLE void updatePreviewScrub(double second, bool centerView = true);
    Q_INVOKABLE void endPreviewScrub(double second, bool centerView = true);
    Q_INVOKABLE void setPreviewRate(double rate);
    Q_INVOKABLE bool stepPreviewBySeconds(double deltaSeconds, bool centerView = true);
    Q_INVOKABLE void beginPreviewHeldSeek(int direction, int key);
    Q_INVOKABLE void stopPreviewHeldSeek(int key = 0);
    Q_INVOKABLE QString formatPreviewTimestamp(double second) const;
    Q_INVOKABLE void logPreviewInteraction(const QString& action, const QString& payload = QString());
    Q_INVOKABLE void syncTopChromeSurfaceSize(int width, int height);
    Q_INVOKABLE void syncSidebarSurfaceSize(int width, int height);
    Q_INVOKABLE void syncWorkspaceSurfaceSize(int width, int height);
    Q_INVOKABLE void syncStatusSurfaceSize(int width, int height);
    bool hasShortcut(const QKeySequence& sequence) const;
    bool triggerShortcut(const QKeySequence& sequence);

signals:
    void shellStateChanged();
    void previewFullscreenChanged();

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
    bool previewFullscreen_ = false;
    bool previewUsesSeparateSurface_ = false;
};
