#include "QuickShellPreviewCompositeSurface.h"

#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewSharedD3D11Device.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include <QQuickGraphicsDevice>
#include <QQuickView>
#include <QSGRendererInterface>
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

    // H2 single-device preview decode: when enabled, render this view's QRhi on the
    // same ID3D11Device the FFmpeg D3D11VA decoder uses, so the per-frame cross-device
    // video-texture bridge disappears. Must run
    // before the scene graph initialises (i.e. before setSource/show). Only meaningful
    // for the Direct3D11 RHI; a null device (feature off / creation failed) leaves Qt
    // to create its own device = legacy two-device path. The device is also published
    // to the decoder inside sharedPreviewQuickGraphicsDevice().
    //
    // `Unknown` is accepted because when no backend was forced (no --rhi / no persisted
    // choice) graphicsApi() stays Unknown here and Qt resolves to the platform default,
    // which on Windows is Direct3D11 — the primary intended path. An explicitly-forced
    // non-D3D11 backend (main.cpp setGraphicsApi for OpenGL/Vulkan/Software) returns a
    // concrete value and is correctly rejected. If a non-D3D11 RHI ever resolved while
    // still Unknown here, Qt ignores the mismatched imported device and handle()'s raw
    // renderDevice==decodeDevice compare fails → legacy bridge (degrade, not crash).
    const QSGRendererInterface::GraphicsApi graphicsApi = QQuickWindow::graphicsApi();
    if (graphicsApi == QSGRendererInterface::Direct3D11
        || graphicsApi == QSGRendererInterface::Unknown) {
        const QQuickGraphicsDevice sharedDevice =
            miacode::preview::sharedPreviewQuickGraphicsDevice();
        if (!sharedDevice.isNull()) {
            view_->setGraphicsDevice(sharedDevice);
        }
    }

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
