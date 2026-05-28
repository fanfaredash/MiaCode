#include "preview/quick_scene/PreviewQuickCenterDisplayLayer.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QPainter>

PreviewQuickCenterDisplayLayer::PreviewQuickCenterDisplayLayer(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
}

void PreviewQuickCenterDisplayLayer::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    if (runtimeUpdateConnection_) {
        QObject::disconnect(runtimeUpdateConnection_);
        runtimeUpdateConnection_ = {};
    }
    runtime_ = runtime;
    if (runtime_ != nullptr) {
        runtimeUpdateConnection_ = QObject::connect(
            runtime_,
            &PreviewRuntime::frameStateChanged,
            this,
            [this]() { update(); }
        );
    }
    emit runtimeChanged();
    update();
}

QObject* PreviewQuickCenterDisplayLayer::runtimeObject() const
{
    return runtime_;
}

void PreviewQuickCenterDisplayLayer::setRuntimeObject(QObject* runtimeObject)
{
    setRuntime(qobject_cast<PreviewRuntime*>(runtimeObject));
}

void PreviewQuickCenterDisplayLayer::paint(QPainter* painter)
{
    if (painter == nullptr) {
        return;
    }
    if (runtime_ == nullptr) {
        return;
    }
    miacode::preview::hud::paintCenterDisplay(
        *painter,
        runtime_->frameState(),
        boundingRect().size().toSize()
    );
}

