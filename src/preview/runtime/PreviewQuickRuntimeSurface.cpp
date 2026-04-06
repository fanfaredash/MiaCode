#include "preview/runtime/PreviewQuickRuntimeSurface.h"

#include "common/DebugLog.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/quick_scene/PreviewTextureRepository.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtQml/qqml.h>

namespace {

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

QString quickViewStatusName(QQuickView::Status status)
{
    switch (status) {
    case QQuickView::Null:
        return QStringLiteral("Null");
    case QQuickView::Ready:
        return QStringLiteral("Ready");
    case QQuickView::Loading:
        return QStringLiteral("Loading");
    case QQuickView::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Status(%1)").arg(static_cast<int>(status));
}

QString windowVisibilityName(QWindow::Visibility visibility)
{
    switch (visibility) {
    case QWindow::Hidden:
        return QStringLiteral("Hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("Automatic");
    case QWindow::Windowed:
        return QStringLiteral("Windowed");
    case QWindow::Minimized:
        return QStringLiteral("Minimized");
    case QWindow::Maximized:
        return QStringLiteral("Maximized");
    case QWindow::FullScreen:
        return QStringLiteral("FullScreen");
    }
    return QStringLiteral("Visibility(%1)").arg(static_cast<int>(visibility));
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Unknown:
        return QStringLiteral("Unknown");
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG:
        return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Null:
        return QStringLiteral("Null");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("Direct3D12");
    }
    return QStringLiteral("GraphicsApi(%1)").arg(static_cast<int>(api));
}

QString resolvedQuickGraphicsApiName(const QQuickWindow* window)
{
    if (window == nullptr || window->rendererInterface() == nullptr) {
        return QStringLiteral("Unavailable");
    }
    return graphicsApiName(window->rendererInterface()->graphicsApi());
}

QString describeQuickWindow(const QQuickWindow* window)
{
    if (window == nullptr) {
        return QStringLiteral("window=null");
    }
    return QStringLiteral(
        "window=%1 visible=%2 exposed=%3 active=%4 visibility=%5 size=%6x%7 dpr=%8"
    )
        .arg(pointerHex(window))
        .arg(window->isVisible() ? 1 : 0)
        .arg(window->isExposed() ? 1 : 0)
        .arg(window->isActive() ? 1 : 0)
        .arg(windowVisibilityName(window->visibility()))
        .arg(window->width())
        .arg(window->height())
        .arg(window->devicePixelRatio(), 0, 'f', 3);
}

void appendQuickRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/quick_runtime"),
        text
    );
}

void ensurePreviewQuickTypeRegistered()
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

PreviewQuickRuntimeSurface::PreviewQuickRuntimeSurface(QObject* parent)
    : QObject(parent)
{
    ensurePreviewQuickTypeRegistered();

    view_ = new QQuickView();
    appendQuickRuntimeLog(
        QStringLiteral("construct"),
        QString("view=%1").arg(pointerHex(view_))
    );
    view_->setColor(Qt::transparent);
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    view_->setPersistentGraphics(true);
    view_->setPersistentSceneGraph(true);
    view_->setSource(QUrl(QStringLiteral("qrc:/preview/runtime/qml/PreviewRuntimeView.qml")));
    appendQuickRuntimeLog(
        QStringLiteral("source_set"),
        QString("status=%1 %2")
            .arg(quickViewStatusName(view_->status()))
            .arg(describeQuickWindow(view_))
    );

    connect(view_, &QQuickView::statusChanged, this, [this](QQuickView::Status status) {
        appendQuickRuntimeLog(
            QStringLiteral("status_changed"),
            QString("status=%1 root_object=%2")
                .arg(quickViewStatusName(status))
                .arg(view_ != nullptr && view_->rootObject() != nullptr ? 1 : 0)
        );
        if (status == QQuickView::Ready) {
            bindQuickItem();
        }
    });
    frameSwapWatchdog_ = new QTimer(this);
    frameSwapWatchdog_->setInterval(100);
    connect(frameSwapWatchdog_, &QTimer::timeout, this, [this]() {
        if (view_ == nullptr || !frameSwapElapsed_.isValid()) {
            return;
        }
        if (!view_->isVisible() || !view_->isExposed()) {
            frameStallActive_ = false;
            lastLoggedFrameStallBucket_ = -1;
            return;
        }

        const qint64 elapsedMs = frameSwapElapsed_.elapsed();
        if (elapsedMs < 250) {
            return;
        }

        const qint64 bucket = elapsedMs / 250;
        if (!frameStallActive_ || bucket != lastLoggedFrameStallBucket_) {
            frameStallActive_ = true;
            lastLoggedFrameStallBucket_ = bucket;
            appendQuickRuntimeLog(
                QStringLiteral("frame_stall"),
                QString("elapsed_ms=%1 frame_count=%2 %3")
                    .arg(elapsedMs)
                    .arg(framePresentedCount_)
                    .arg(describeQuickWindow(view_))
            );
        }
    });
    frameSwapWatchdog_->start();
    connect(view_, &QQuickWindow::frameSwapped, this, [this]() {
        qint64 intervalMs = -1;
        const bool resumedFromStall = frameStallActive_;
        const qint64 stallBucket = lastLoggedFrameStallBucket_;
        if (frameSwapElapsed_.isValid()) {
            intervalMs = frameSwapElapsed_.restart();
        } else {
            frameSwapElapsed_.start();
        }
        frameStallActive_ = false;
        lastLoggedFrameStallBucket_ = -1;
        ++framePresentedCount_;
        if (framePresentedCount_ <= 5
            || (framePresentedCount_ % 120) == 0
            || resumedFromStall
            || intervalMs >= 120) {
            appendQuickRuntimeLog(
                QStringLiteral("frame_swapped"),
                QString("count=%1 interval_ms=%2 resumed_from_stall=%3 last_stall_bucket=%4 %5")
                    .arg(framePresentedCount_)
                    .arg(intervalMs)
                    .arg(resumedFromStall ? 1 : 0)
                    .arg(stallBucket)
                    .arg(describeQuickWindow(view_))
            );
        }
        emit framePresented();
    });
    connect(view_, &QQuickWindow::sceneGraphInvalidated, this, [this]() {
        appendQuickRuntimeLog(
            QStringLiteral("scene_graph_invalidated"),
            QString("scene_root=%1 hud=%2 %3")
                .arg(sceneRoot_ != nullptr ? 1 : 0)
                .arg(hudLayer_ != nullptr ? 1 : 0)
                .arg(describeQuickWindow(view_))
        );
        if (sceneRoot_ != nullptr) {
            sceneRoot_->invalidateTextureCache();
        }
    });
    connect(view_, &QQuickWindow::sceneGraphInitialized, this, [this]() {
        appendQuickRuntimeLog(
            QStringLiteral("scene_graph_initialized"),
            QString("scene_root=%1 hud=%2 graphics_api=%3 %4")
                .arg(sceneRoot_ != nullptr ? 1 : 0)
                .arg(hudLayer_ != nullptr ? 1 : 0)
                .arg(resolvedQuickGraphicsApiName(view_))
                .arg(describeQuickWindow(view_))
        );
        if (sceneRoot_ != nullptr) {
            sceneRoot_->invalidateTextureCache();
        }
        requestFrame();
    });

    if (view_->status() == QQuickView::Ready) {
        bindQuickItem();
    }
}

PreviewQuickRuntimeSurface::~PreviewQuickRuntimeSurface()
{
    appendQuickRuntimeLog(
        QStringLiteral("destruct"),
        QString("frame_count=%1").arg(framePresentedCount_)
    );
    delete view_;
    view_ = nullptr;
}

void PreviewQuickRuntimeSurface::setRuntime(PreviewRuntime* runtime)
{
    runtime_ = runtime;
    appendQuickRuntimeLog(
        QStringLiteral("set_runtime"),
        QString("runtime=%1 scene_root=%2")
            .arg(pointerHex(runtime_))
            .arg(sceneRoot_ != nullptr ? 1 : 0)
    );
    bindQuickItem();
}

QWindow* PreviewQuickRuntimeSurface::hostWindow() const
{
    return view_;
}

PreviewTextureStats PreviewQuickRuntimeSurface::textureStats() const
{
    if (sceneRoot_ == nullptr) {
        return PreviewTextureStats();
    }
    return sceneRoot_->textureStats();
}

void PreviewQuickRuntimeSurface::requestActivate()
{
    if (view_ != nullptr) {
        appendQuickRuntimeLog(
            QStringLiteral("request_activate"),
            describeQuickWindow(view_)
        );
        view_->requestActivate();
    }
}

void PreviewQuickRuntimeSurface::requestFrame()
{
    if (sceneRoot_ != nullptr) {
        sceneRoot_->update();
    }
    if (hudLayer_ != nullptr) {
        hudLayer_->update();
    }
    if (sceneRoot_ == nullptr && hudLayer_ == nullptr && view_ != nullptr) {
        view_->update();
    }
}

void PreviewQuickRuntimeSurface::bindQuickItem()
{
    if (view_ == nullptr || view_->status() != QQuickView::Ready) {
        appendQuickRuntimeLog(
            QStringLiteral("bind_quick_item_skipped"),
            QString("view=%1 status=%2")
                .arg(pointerHex(view_))
                .arg(view_ != nullptr ? quickViewStatusName(view_->status()) : QStringLiteral("null"))
        );
        return;
    }

    QObject* root = view_->rootObject();
    if (root == nullptr) {
        appendQuickRuntimeLog(
            QStringLiteral("bind_quick_item_no_root"),
            describeQuickWindow(view_)
        );
        return;
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

    appendQuickRuntimeLog(
        QStringLiteral("bind_quick_item"),
        QString("root=%1 scene_root=%2 hud=%3 runtime=%4 %5")
            .arg(pointerHex(root))
            .arg(pointerHex(sceneRoot_))
            .arg(pointerHex(hudLayer_))
            .arg(pointerHex(runtime_))
            .arg(describeQuickWindow(view_))
    );
}
