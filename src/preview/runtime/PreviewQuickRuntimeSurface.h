#pragma once

#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class QQuickView;
class QTimer;
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
    PreviewTextureStats textureStats() const;
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
    quint64 framePresentedCount_ = 0;
    QElapsedTimer frameSwapElapsed_;
    QTimer* frameSwapWatchdog_ = nullptr;
    qint64 lastLoggedFrameStallBucket_ = -1;
    bool frameStallActive_ = false;
};
