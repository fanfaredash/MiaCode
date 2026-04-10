#pragma once

#include <QObject>
#include <QPointer>

class QQuickView;
class QWindow;

class PreviewRuntime;
class PreviewStageMediaHost;

class QuickShellPreviewCompositeSurface : public QObject
{
    Q_OBJECT

public:
    explicit QuickShellPreviewCompositeSurface(QObject* parent = nullptr);
    ~QuickShellPreviewCompositeSurface() override;

    void setRuntime(PreviewRuntime* runtime);
    void setMediaHost(PreviewStageMediaHost* mediaHost);
    void setActive(bool active);
    bool active() const;
    QWindow* hostWindow() const;

private:
    void bindRootObject();
    void applyRootBindings();

    QPointer<PreviewRuntime> runtime_;
    QPointer<PreviewStageMediaHost> mediaHost_;
    QPointer<QObject> rootObject_;
    QQuickView* view_ = nullptr;
    bool active_ = false;
};
