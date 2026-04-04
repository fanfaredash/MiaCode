#pragma once

#include <QObject>
#include <QPointer>

class QQuickView;
class QWindow;
class PreviewQuickHudLayer;
class PreviewQuickSceneRoot;
class PreviewRuntime;

class PreviewQuickRuntimeSurface : public QObject
{
    Q_OBJECT

public:
    explicit PreviewQuickRuntimeSurface(QObject* parent = nullptr);
    ~PreviewQuickRuntimeSurface() override;

    void setRuntime(PreviewRuntime* runtime);
    QWindow* hostWindow() const;
    void requestActivate();
    void requestFrame();

signals:
    void framePresented();

private:
    void bindQuickItem();

    PreviewRuntime* runtime_ = nullptr;
    QQuickView* view_ = nullptr;
    QPointer<PreviewQuickSceneRoot> sceneRoot_;
    QPointer<PreviewQuickHudLayer> hudLayer_;
};
