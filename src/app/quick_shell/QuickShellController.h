#pragma once

#include <QObject>
#include <QPointer>

class QTimer;
class QWindow;

class MainWindow;

class QuickShellController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY shellStateChanged)
    Q_PROPERTY(bool workspacePanelsSwapped READ workspacePanelsSwapped NOTIFY shellStateChanged)
    Q_PROPERTY(QString previewSpeedLabel READ previewSpeedLabel NOTIFY shellStateChanged)
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY shellStateChanged)
    Q_PROPERTY(double previewPositionSeconds READ previewPositionSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(double previewDurationSeconds READ previewDurationSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(bool previewFullscreen READ previewFullscreen WRITE setPreviewFullscreen NOTIFY previewFullscreenChanged)
    Q_PROPERTY(QObject* previewRuntime READ previewRuntime CONSTANT)
    Q_PROPERTY(QWindow* topChromeWindow READ topChromeWindow CONSTANT)
    Q_PROPERTY(QWindow* workspaceWindow READ workspaceWindow CONSTANT)
    Q_PROPERTY(QWindow* previewControlsWindow READ previewControlsWindow CONSTANT)
    Q_PROPERTY(QWindow* statusWindow READ statusWindow CONSTANT)

public:
    explicit QuickShellController(MainWindow* backend, QObject* parent = nullptr);

    QString windowTitle() const;
    bool workspacePanelsSwapped() const;
    QString previewSpeedLabel() const;
    bool previewPlaying() const;
    double previewPositionSeconds() const;
    double previewDurationSeconds() const;
    bool previewFullscreen() const;
    QObject* previewRuntime() const;
    QWindow* topChromeWindow() const;
    QWindow* workspaceWindow() const;
    QWindow* previewControlsWindow() const;
    QWindow* statusWindow() const;

    void setPreviewFullscreen(bool fullscreen);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void togglePreviewPlayback();
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void seekPreview(double second);
    Q_INVOKABLE void setPreviewRate(double rate);
    Q_INVOKABLE void syncTopChromeSurfaceSize(int width, int height);
    Q_INVOKABLE void syncWorkspaceSurfaceSize(int width, int height);
    Q_INVOKABLE void syncPreviewControlsSurfaceSize(int width, int height);
    Q_INVOKABLE void syncStatusSurfaceSize(int width, int height);

signals:
    void shellStateChanged();
    void previewFullscreenChanged();

private:
    QWindow* createForeignWindowForSurface(QWidget* surface) const;
    void refreshFromBackend();

    QPointer<MainWindow> backend_;
    QTimer* refreshTimer_ = nullptr;
    QWindow* topChromeWindow_ = nullptr;
    QWindow* workspaceWindow_ = nullptr;
    QWindow* previewControlsWindow_ = nullptr;
    QWindow* statusWindow_ = nullptr;
    QString windowTitle_;
    bool workspacePanelsSwapped_ = false;
    QString previewSpeedLabel_;
    bool previewPlaying_ = false;
    double previewPositionSeconds_ = 0.0;
    double previewDurationSeconds_ = 0.0;
    bool previewFullscreen_ = false;
};
