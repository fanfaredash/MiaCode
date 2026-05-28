#include "QuickShellPreviewCompositeSurface.h"

#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include <QQuickView>
#include <QSurfaceFormat>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqml.h>

namespace {

void ensureQuickShellPreviewTypesRegistered()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
   qmlRegisterType<PreviewQuickHudLayer>("MiaCode.Preview", 1, 0, "PreviewQuickHudLayer");
   registered = true;
}

}  // namespace

QuickShellPreviewCompositeSurface::QuickShellPreviewCompositeSurface(QObject* parent)
    : QObject(parent)
{
    ensureQuickShellPreviewTypesRegistered();

    view_ = new QQuickView();
    QSurfaceFormat format = view_->format();
    format.setAlphaBufferSize(0);
    view_->setFormat(format);
    view_->setColor(QColor(QStringLiteral("#000000")));
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    view_->setPersistentGraphics(true);
    view_->setPersistentSceneGraph(true);
    connect(view_, &QQuickView::statusChanged, this, [this](QQuickView::Status status) {
        if (status == QQuickView::Ready) {
            bindRootObject();
        }
    });
    view_->setSource(QUrl(QStringLiteral("qrc:/quick_shell/qml/QuickShellPreviewSurface.qml")));
    if (view_->status() == QQuickView::Ready) {
        bindRootObject();
    }
}

QuickShellPreviewCompositeSurface::~QuickShellPreviewCompositeSurface()
{
    delete view_;
    view_ = nullptr;
}

void QuickShellPreviewCompositeSurface::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    runtime_ = runtime;
    applyRootBindings();
}

void QuickShellPreviewCompositeSurface::setMediaHost(PreviewStageMediaHost* mediaHost)
{
    if (mediaHost_ == mediaHost) {
        return;
    }
    mediaHost_ = mediaHost;
    applyRootBindings();
}

void QuickShellPreviewCompositeSurface::setActive(bool active)
{
    if (active_ == active) {
        return;
    }
    active_ = active;
    applyRootBindings();
}

bool QuickShellPreviewCompositeSurface::active() const
{
    return active_;
}

QWindow* QuickShellPreviewCompositeSurface::hostWindow() const
{
    return view_;
}

void QuickShellPreviewCompositeSurface::bindRootObject()
{
    if (view_ == nullptr || view_->status() != QQuickView::Ready) {
        return;
    }

    rootObject_ = view_->rootObject();
    applyRootBindings();
}

void QuickShellPreviewCompositeSurface::applyRootBindings()
{
    if (rootObject_ == nullptr) {
        return;
    }

    QObject* runtimeObject = active_ ? static_cast<QObject*>(runtime_) : nullptr;
    QObject* mediaHostObject = active_ ? static_cast<QObject*>(mediaHost_) : nullptr;
    rootObject_->setProperty("runtime", QVariant::fromValue(runtimeObject));
    rootObject_->setProperty("mediaHost", QVariant::fromValue(mediaHostObject));
}
