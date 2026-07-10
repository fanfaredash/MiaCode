#include "app/ui/AppBackgroundLayer.h"

#include <QApplication>
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPaintEvent>

namespace miacode::ui {

namespace {

AppBackgroundLayer* findBackgroundLayer(QWidget* widget)
{
    for (QWidget* current = widget != nullptr ? widget->parentWidget() : nullptr;
         current != nullptr;
         current = current->parentWidget()) {
        if (auto* layer = dynamic_cast<AppBackgroundLayer*>(current); layer != nullptr) {
            return layer;
        }
    }
    if (QWidget* window = widget != nullptr ? widget->window() : nullptr; window != nullptr) {
        const QList<QWidget*> hosts = window->findChildren<QWidget*>(QStringLiteral("AppBackgroundHost"));
        for (QWidget* host : hosts) {
            if (auto* layer = dynamic_cast<AppBackgroundLayer*>(host); layer != nullptr) {
                return layer;
            }
        }
    }
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* candidate : widgets) {
        if (candidate != nullptr && candidate->objectName() == QStringLiteral("AppBackgroundHost")) {
            if (auto* layer = dynamic_cast<AppBackgroundLayer*>(candidate); layer != nullptr) {
                return layer;
            }
        }
    }
    return nullptr;
}

}  // namespace

void paintAppBackgroundForWidget(QWidget* surface, QPainter& painter)
{
    if (surface == nullptr) {
        return;
    }
    if (AppBackgroundLayer* layer = findBackgroundLayer(surface); layer != nullptr) {
        layer->paintBackgroundForSurface(surface, painter);
    }
}

namespace {

void paintSurfaceBackdrop(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    QPainter painter(widget);
    paintAppBackgroundForWidget(widget, painter);
}

}  // namespace

AppBackgroundLayer::AppBackgroundLayer(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
}

AppBackgroundSettings AppBackgroundLayer::settings() const
{
    return settings_;
}

void AppBackgroundLayer::setSettings(const AppBackgroundSettings& settings)
{
    const AppBackgroundSettings normalized = normalizedAppBackgroundSettings(settings);
    if (normalized.enabled == settings_.enabled
        && normalized.imagePath == settings_.imagePath
        && qFuzzyCompare(normalized.opacity + 1.0, settings_.opacity + 1.0)
        && normalized.blur == settings_.blur
        && normalized.sizeMode == settings_.sizeMode
        && normalized.position == settings_.position) {
        return;
    }
    settings_ = normalized;
    invalidateCache();
    update();
}

void AppBackgroundLayer::paintBackgroundForSurface(QWidget* surface, QPainter& painter)
{
    if (surface == nullptr) {
        return;
    }

    const QSize pixmapCanvasSize = canvasSize();
    const QPixmap pixmap = renderedPixmap(pixmapCanvasSize);
    if (pixmap.isNull()) {
        return;
    }

    QPoint sourceTopLeft(0, 0);
    if (QWidget* window = surface->window(); window != nullptr && window != surface) {
        sourceTopLeft = surface->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0));
    }

    const QRect sourceRect(sourceTopLeft, surface->size());
    const QRect clippedSource = sourceRect.intersected(pixmap.rect());
    if (clippedSource.isEmpty()) {
        return;
    }

    const QRect targetRect(clippedSource.topLeft() - sourceTopLeft, clippedSource.size());
    painter.drawPixmap(targetRect, pixmap, clippedSource);
}

void AppBackgroundLayer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    paintBackgroundForSurface(this, painter);
}

void AppBackgroundLayer::invalidateCache()
{
    cachedPixmap_ = QPixmap();
    cachedWidgetSize_ = QSize();
    cachedSettings_ = AppBackgroundSettings();
    if (settings_.imagePath != loadedPath_) {
        sourceImage_ = QImage();
        loadedPath_.clear();
    }
}

bool AppBackgroundLayer::ensureSourceLoaded()
{
    if (!sourceImage_.isNull() && loadedPath_ == settings_.imagePath) {
        return true;
    }
    sourceImage_ = QImage();
    loadedPath_.clear();
    if (settings_.imagePath.isEmpty() || !QFileInfo::exists(settings_.imagePath)) {
        return false;
    }
    QImage image(settings_.imagePath);
    if (image.isNull()) {
        return false;
    }
    sourceImage_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    loadedPath_ = settings_.imagePath;
    return true;
}

QImage AppBackgroundLayer::blurredImage(const QImage& image) const
{
    if (settings_.blur <= 0 || image.isNull()) {
        return image;
    }

    QGraphicsScene scene;
    QGraphicsPixmapItem item(QPixmap::fromImage(image));
    QGraphicsBlurEffect effect;
    effect.setBlurRadius(static_cast<qreal>(settings_.blur));
    effect.setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    item.setGraphicsEffect(&effect);
    scene.addItem(&item);

    QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    scene.render(&painter, QRectF(QPointF(0, 0), image.size()), QRectF(QPointF(0, 0), image.size()));
    scene.removeItem(&item);
    item.setGraphicsEffect(nullptr);
    return result;
}

QSize AppBackgroundLayer::canvasSize() const
{
    return size();
}

QPoint AppBackgroundLayer::alignedTopLeft(const QSize& drawSize, const QSize& canvasSize) const
{
    int x = (canvasSize.width() - drawSize.width()) / 2;
    int y = (canvasSize.height() - drawSize.height()) / 2;

    switch (settings_.position) {
    case AppBackgroundPosition::Left:
    case AppBackgroundPosition::LeftTop:
    case AppBackgroundPosition::LeftBottom:
        x = 0;
        break;
    case AppBackgroundPosition::Right:
    case AppBackgroundPosition::RightTop:
    case AppBackgroundPosition::RightBottom:
        x = canvasSize.width() - drawSize.width();
        break;
    case AppBackgroundPosition::Center:
    case AppBackgroundPosition::Top:
    case AppBackgroundPosition::Bottom:
    default:
        break;
    }

    switch (settings_.position) {
    case AppBackgroundPosition::Top:
    case AppBackgroundPosition::LeftTop:
    case AppBackgroundPosition::RightTop:
        y = 0;
        break;
    case AppBackgroundPosition::Bottom:
    case AppBackgroundPosition::LeftBottom:
    case AppBackgroundPosition::RightBottom:
        y = canvasSize.height() - drawSize.height();
        break;
    case AppBackgroundPosition::Center:
    case AppBackgroundPosition::Left:
    case AppBackgroundPosition::Right:
    default:
        break;
    }

    return QPoint(x, y);
}

QRect AppBackgroundLayer::targetRectForImage(const QSize& imageSize, const QSize& canvasSize) const
{
    if (imageSize.isEmpty() || canvasSize.isEmpty()) {
        return QRect();
    }

    QSize drawSize = imageSize;
    switch (settings_.sizeMode) {
    case AppBackgroundSizeMode::Stretch:
        return QRect(QPoint(0, 0), canvasSize);
    case AppBackgroundSizeMode::Contain:
        drawSize.scale(canvasSize, Qt::KeepAspectRatio);
        break;
    case AppBackgroundSizeMode::Cover:
        drawSize.scale(canvasSize, Qt::KeepAspectRatioByExpanding);
        break;
    case AppBackgroundSizeMode::Center:
    case AppBackgroundSizeMode::Repeat:
        break;
    }

    return QRect(alignedTopLeft(drawSize, canvasSize), drawSize);
}

QPixmap AppBackgroundLayer::renderedPixmap(const QSize& canvasSize)
{
    if (!settings_.enabled
        || settings_.opacity <= 0.0
        || canvasSize.isEmpty()
        || !ensureSourceLoaded()) {
        return QPixmap();
    }

    if (!cachedPixmap_.isNull()
        && cachedWidgetSize_ == canvasSize
        && cachedSettings_.enabled == settings_.enabled
        && cachedSettings_.imagePath == settings_.imagePath
        && qFuzzyCompare(cachedSettings_.opacity + 1.0, settings_.opacity + 1.0)
        && cachedSettings_.blur == settings_.blur
        && cachedSettings_.sizeMode == settings_.sizeMode
        && cachedSettings_.position == settings_.position) {
        return cachedPixmap_;
    }

    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter imagePainter(&canvas);
    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect target = targetRectForImage(sourceImage_.size(), canvasSize);
    if (settings_.sizeMode == AppBackgroundSizeMode::Repeat) {
        const QPoint start = alignedTopLeft(sourceImage_.size(), canvasSize);
        for (int y = start.y() % qMax(1, sourceImage_.height()) - sourceImage_.height(); y < canvasSize.height(); y += sourceImage_.height()) {
            for (int x = start.x() % qMax(1, sourceImage_.width()) - sourceImage_.width(); x < canvasSize.width(); x += sourceImage_.width()) {
                imagePainter.drawImage(QPoint(x, y), sourceImage_);
            }
        }
    } else if (!target.isEmpty()) {
        imagePainter.drawImage(target, sourceImage_);
    }
    imagePainter.end();

    canvas = blurredImage(canvas);

    QPixmap result(canvasSize);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setOpacity(settings_.opacity);
    painter.drawImage(0, 0, canvas);
    painter.end();

    cachedPixmap_ = result;
    cachedWidgetSize_ = canvasSize;
    cachedSettings_ = settings_;
    return cachedPixmap_;
}

AppBackgroundSurfaceWidget::AppBackgroundSurfaceWidget(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceWidget::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QWidget::paintEvent(event);
}

AppBackgroundSurfaceFrame::AppBackgroundSurfaceFrame(QWidget* parent)
    : QFrame(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceFrame::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QFrame::paintEvent(event);
}

AppBackgroundSurfaceTabWidget::AppBackgroundSurfaceTabWidget(QWidget* parent)
    : QTabWidget(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceTabWidget::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QTabWidget::paintEvent(event);
}

AppBackgroundSurfaceMenuBar::AppBackgroundSurfaceMenuBar(QWidget* parent)
    : QMenuBar(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceMenuBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QMenuBar::paintEvent(event);
}

AppBackgroundSurfaceToolBar::AppBackgroundSurfaceToolBar(const QString& title, QWidget* parent)
    : QToolBar(title, parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceToolBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QToolBar::paintEvent(event);
}

AppBackgroundSurfaceStatusBar::AppBackgroundSurfaceStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void AppBackgroundSurfaceStatusBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QStatusBar::paintEvent(event);
}

}  // namespace miacode::ui
