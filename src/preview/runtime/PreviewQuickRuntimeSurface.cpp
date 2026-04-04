#include "preview/runtime/PreviewQuickRuntimeSurface.h"

#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/scene/PreviewLayerOrder.h"
#include "preview/runtime/PreviewQuickItem.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QQuickView>
#include <QQmlContext>
#include <QtQml/qqml.h>

namespace {

void ensurePreviewQuickTypeRegistered()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    qmlRegisterType<PreviewQuickItem>("MiaCode.Preview", 1, 0, "PreviewQuickItem");
    qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
    qmlRegisterType<PreviewQuickHudLayer>("MiaCode.Preview", 1, 0, "PreviewQuickHudLayer");
    registered = true;
}

}  // namespace

PreviewQuickRuntimeSurface::PreviewQuickRuntimeSurface(QObject* parent)
    : QObject(parent)
{
    ensurePreviewQuickTypeRegistered();

    view_ = new QQuickView();
    view_->setColor(Qt::transparent);
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    view_->setSource(QUrl(QStringLiteral("qrc:/preview/runtime/qml/PreviewRuntimeView.qml")));

    connect(view_, &QQuickView::statusChanged, this, [this](QQuickView::Status status) {
        if (status == QQuickView::Ready) {
            bindQuickItem();
        }
    });
    connect(view_, &QQuickView::frameSwapped, this, &PreviewQuickRuntimeSurface::framePresented);

    if (view_->status() == QQuickView::Ready) {
        bindQuickItem();
    }
}

PreviewQuickRuntimeSurface::~PreviewQuickRuntimeSurface()
{
    delete view_;
    view_ = nullptr;
}

void PreviewQuickRuntimeSurface::setRuntime(PreviewRuntime* runtime)
{
    runtime_ = runtime;
    bindQuickItem();
}

QWindow* PreviewQuickRuntimeSurface::hostWindow() const
{
    return view_;
}

void PreviewQuickRuntimeSurface::requestActivate()
{
    if (view_ != nullptr) {
        view_->requestActivate();
    }
}

void PreviewQuickRuntimeSurface::requestFrame()
{
    if (sceneRoot_ != nullptr) {
        sceneRoot_->update();
    }
    if (quickItem_ != nullptr) {
        quickItem_->update();
    }
    if (hudLayer_ != nullptr) {
        hudLayer_->update();
    }
    if (sceneRoot_ == nullptr && quickItem_ == nullptr && hudLayer_ == nullptr && view_ != nullptr) {
        view_->update();
    }
}

void PreviewQuickRuntimeSurface::bindQuickItem()
{
    if (view_ == nullptr || view_->status() != QQuickView::Ready) {
        return;
    }

    QObject* root = view_->rootObject();
    if (root == nullptr) {
        return;
    }

    PreviewQuickItem* item = root->findChild<PreviewQuickItem*>(QStringLiteral("previewQuickItem"));
    if (item == nullptr) {
        return;
    }

    quickItem_ = item;
    if (quickItem_ != nullptr) {
        quickItem_->setVisible(false);
        quickItem_->setRuntime(nullptr);
    }

    sceneRoot_ = root->findChild<PreviewQuickSceneRoot*>(QStringLiteral("previewQuickSceneRoot"));
    if (sceneRoot_ != nullptr) {
        sceneRoot_->setRuntime(runtime_);
    }

    hudLayer_ = root->findChild<PreviewQuickHudLayer*>(QStringLiteral("previewQuickHudLayer"));
    if (hudLayer_ != nullptr) {
        hudLayer_->setVisible(true);
        hudLayer_->setRuntime(runtime_);
    }

    if (runtime_ != nullptr && sceneRoot_ != nullptr) {
        runtime_->setFrameSize(sceneRoot_->boundingRect().size().toSize());
    }
}
