#include "app/ui/AppBackgroundPainter.h"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImageReader>
#include <QPainter>
#include <QPaintEvent>
#include <QVariant>

#include "common/DebugLog.h"

namespace miacode::ui {

namespace {

constexpr auto kAppBackgroundPainterProperty = "miacode.appBackgroundPainter";
constexpr auto kAppBackgroundActiveProperty = "miacode.appBackgroundActive";

QString quotedDiag(QString value)
{
    value.replace(QLatin1Char('"'), QLatin1Char('\''));
    return QStringLiteral("\"%1\"").arg(value);
}

void logBackgroundDiag(const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/app_background"),
        payload);
}

void paintSurfaceBackdrop(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    QPainter painter(widget);
    paintAppBackgroundForWidget(widget, painter);
}

void prepareBackgroundSurface(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    widget->setAutoFillBackground(false);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);
}

}  // namespace

AppBackgroundPainter::AppBackgroundPainter(QWidget* window)
    : QObject(window)
    , window_(window)
{
    if (window_ != nullptr) {
        window_->installEventFilter(this);
        installAppBackgroundPainter(window_, this);
    }
    updateApplicationActiveFlag();
}

AppBackgroundPainter::~AppBackgroundPainter()
{
    if (window_ != nullptr) {
        window_->removeEventFilter(this);
        const quintptr installed = window_->property(kAppBackgroundPainterProperty).value<quintptr>();
        if (installed == reinterpret_cast<quintptr>(this)) {
            window_->setProperty(kAppBackgroundPainterProperty, QVariant());
        }
    }
    if (appBackgroundIsActiveForTheme()) {
        qApp->setProperty(kAppBackgroundActiveProperty, false);
    }
}

AppBackgroundSettings AppBackgroundPainter::settings() const
{
    return settings_;
}

void AppBackgroundPainter::setSettings(const AppBackgroundSettings& settings)
{
    const AppBackgroundSettings normalized = normalizedAppBackgroundSettings(settings);
    if (normalized.enabled == settings_.enabled
        && normalized.imagePath == settings_.imagePath
        && qFuzzyCompare(normalized.opacity + 1.0, settings_.opacity + 1.0)
        && normalized.blur == settings_.blur
        && normalized.sizeMode == settings_.sizeMode
        && normalized.position == settings_.position) {
        logBackgroundDiag(QStringLiteral(
            "action=set_settings_unchanged enabled=%1 opacity=%2 image_path=%3")
            .arg(settings_.enabled ? 1 : 0)
            .arg(settings_.opacity)
            .arg(quotedDiag(settings_.imagePath)));
        return;
    }

    logBackgroundDiag(QStringLiteral(
        "action=set_settings enabled=%1 opacity=%2 blur=%3 image_path=%4")
        .arg(normalized.enabled ? 1 : 0)
        .arg(normalized.opacity)
        .arg(normalized.blur)
        .arg(quotedDiag(normalized.imagePath)));
    settings_ = normalized;
    invalidateCache();
    updateApplicationActiveFlag();
    requestSurfaceUpdates();
}

void AppBackgroundPainter::invalidateCache()
{
    cachedPixmap_ = QPixmap();
    cachedWidgetSize_ = QSize();
    cachedSettings_ = AppBackgroundSettings();
    if (settings_.imagePath != loadedPath_) {
        sourceImage_ = QImage();
        loadedPath_.clear();
    }
}

bool AppBackgroundPainter::paintBackgroundForSurface(QWidget* surface, QPainter& painter)
{
    static int logCount = 0;
    if (surface == nullptr || window_ == nullptr) {
        if (logCount < 20) {
            ++logCount;
            logBackgroundDiag(QStringLiteral("action=paint_skip reason=null_surface_or_window"));
        }
        return false;
    }

    const QPixmap pixmap = renderedPixmap(canvasSize());
    if (pixmap.isNull()) {
        if (logCount < 20) {
            ++logCount;
            logBackgroundDiag(QStringLiteral(
                "action=paint_skip reason=null_pixmap surface_class=%1 surface_name=%2 surface_size=%3x%4 window_size=%5x%6")
                .arg(QString::fromLatin1(surface->metaObject()->className()))
                .arg(quotedDiag(surface->objectName()))
                .arg(surface->width())
                .arg(surface->height())
                .arg(canvasSize().width())
                .arg(canvasSize().height()));
        }
        return false;
    }

    const QPoint sourceTopLeft =
        surface->mapToGlobal(QPoint(0, 0)) - window_->mapToGlobal(QPoint(0, 0));
    const QRect sourceRect(sourceTopLeft, surface->size());
    const QRect clippedSource = sourceRect.intersected(pixmap.rect());
    if (clippedSource.isEmpty()) {
        if (logCount < 20) {
            ++logCount;
            logBackgroundDiag(QStringLiteral(
                "action=paint_skip reason=empty_clip surface_class=%1 surface_name=%2 source=%3,%4,%5,%6 pixmap=%7x%8")
                .arg(QString::fromLatin1(surface->metaObject()->className()))
                .arg(quotedDiag(surface->objectName()))
                .arg(sourceRect.x())
                .arg(sourceRect.y())
                .arg(sourceRect.width())
                .arg(sourceRect.height())
                .arg(pixmap.width())
                .arg(pixmap.height()));
        }
        return false;
    }

    const QRect targetRect(clippedSource.topLeft() - sourceTopLeft, clippedSource.size());
    painter.drawPixmap(targetRect, pixmap, clippedSource);
    if (logCount < 20) {
        ++logCount;
        logBackgroundDiag(QStringLiteral(
            "action=paint_ok surface_class=%1 surface_name=%2 source=%3,%4,%5,%6 target=%7,%8,%9,%10 pixmap=%11x%12")
            .arg(QString::fromLatin1(surface->metaObject()->className()))
            .arg(quotedDiag(surface->objectName()))
            .arg(clippedSource.x())
            .arg(clippedSource.y())
            .arg(clippedSource.width())
            .arg(clippedSource.height())
            .arg(targetRect.x())
            .arg(targetRect.y())
            .arg(targetRect.width())
            .arg(targetRect.height())
            .arg(pixmap.width())
            .arg(pixmap.height()));
    }
    return true;
}

bool AppBackgroundPainter::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event != nullptr) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::StyleChange:
        case QEvent::DevicePixelRatioChange:
            invalidateCache();
            requestSurfaceUpdates();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool AppBackgroundPainter::ensureSourceLoaded()
{
    if (!sourceImage_.isNull() && loadedPath_ == settings_.imagePath) {
        return true;
    }

    sourceImage_ = QImage();
    loadedPath_.clear();
    if (settings_.imagePath.isEmpty() || !QFileInfo::exists(settings_.imagePath)) {
        logBackgroundDiag(QStringLiteral(
            "action=source_load_failed reason=missing_path image_path=%1")
            .arg(quotedDiag(settings_.imagePath)));
        return false;
    }

    QImage image(settings_.imagePath);
    if (image.isNull()) {
        logBackgroundDiag(QStringLiteral(
            "action=source_load_failed reason=qimage_null image_path=%1")
            .arg(quotedDiag(settings_.imagePath)));
        return false;
    }

    sourceImage_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    loadedPath_ = settings_.imagePath;
    logBackgroundDiag(QStringLiteral(
        "action=source_loaded image_path=%1 size=%2x%3")
        .arg(quotedDiag(settings_.imagePath))
        .arg(sourceImage_.width())
        .arg(sourceImage_.height()));
    return true;
}

QImage AppBackgroundPainter::blurredImage(const QImage& image) const
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

QSize AppBackgroundPainter::canvasSize() const
{
    return window_ != nullptr ? window_->size() : QSize();
}

QPoint AppBackgroundPainter::alignedTopLeft(const QSize& drawSize, const QSize& canvasSize) const
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

QRect AppBackgroundPainter::targetRectForImage(const QSize& imageSize, const QSize& canvasSize) const
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

QPixmap AppBackgroundPainter::renderedPixmap(const QSize& canvasSize)
{
    if (!settings_.enabled
        || settings_.opacity <= 0.0
        || canvasSize.isEmpty()
        || !ensureSourceLoaded()) {
        static int logCount = 0;
        if (logCount < 10) {
            ++logCount;
            logBackgroundDiag(QStringLiteral(
                "action=render_skip enabled=%1 opacity=%2 canvas=%3x%4 image_path=%5")
                .arg(settings_.enabled ? 1 : 0)
                .arg(settings_.opacity)
                .arg(canvasSize.width())
                .arg(canvasSize.height())
                .arg(quotedDiag(settings_.imagePath)));
        }
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
        const int tileWidth = qMax(1, sourceImage_.width());
        const int tileHeight = qMax(1, sourceImage_.height());
        for (int y = start.y() % tileHeight - tileHeight; y < canvasSize.height(); y += tileHeight) {
            for (int x = start.x() % tileWidth - tileWidth; x < canvasSize.width(); x += tileWidth) {
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
    logBackgroundDiag(QStringLiteral(
        "action=render_ok canvas=%1x%2 source=%3x%4 opacity=%5")
        .arg(canvasSize.width())
        .arg(canvasSize.height())
        .arg(sourceImage_.width())
        .arg(sourceImage_.height())
        .arg(settings_.opacity));
    return cachedPixmap_;
}

void AppBackgroundPainter::updateApplicationActiveFlag() const
{
    if (qApp == nullptr) {
        return;
    }
    const bool imageReadable =
        !settings_.imagePath.isEmpty()
        && QFileInfo::exists(settings_.imagePath)
        && QImageReader(settings_.imagePath).canRead();
    logBackgroundDiag(QStringLiteral(
        "action=active_flag enabled=%1 opacity=%2 image_path_empty=%3 file_exists=%4 image_reader_can_read=%5 active=%6 image_path=%7")
        .arg(settings_.enabled ? 1 : 0)
        .arg(settings_.opacity)
        .arg(settings_.imagePath.isEmpty() ? 1 : 0)
        .arg(QFileInfo::exists(settings_.imagePath) ? 1 : 0)
        .arg(imageReadable ? 1 : 0)
        .arg((settings_.enabled && settings_.opacity > 0.0 && imageReadable) ? 1 : 0)
        .arg(quotedDiag(settings_.imagePath)));
    qApp->setProperty(
        kAppBackgroundActiveProperty,
        settings_.enabled && settings_.opacity > 0.0 && imageReadable);
}

void AppBackgroundPainter::requestSurfaceUpdates() const
{
    if (window_ == nullptr) {
        return;
    }
    window_->update();
    const QList<QWidget*> children = window_->findChildren<QWidget*>();
    for (QWidget* child : children) {
        if (child != nullptr) {
            child->update();
        }
    }
}

void installAppBackgroundPainter(QWidget* window, AppBackgroundPainter* painter)
{
    if (window == nullptr) {
        return;
    }
    window->setProperty(
        kAppBackgroundPainterProperty,
        QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(painter)));
}

AppBackgroundPainter* appBackgroundPainterForWidget(QWidget* surface)
{
    if (surface == nullptr) {
        return nullptr;
    }
    QWidget* window = surface->window();
    if (window == nullptr) {
        return nullptr;
    }
    const quintptr raw = window->property(kAppBackgroundPainterProperty).value<quintptr>();
    return reinterpret_cast<AppBackgroundPainter*>(raw);
}

bool paintAppBackgroundForWidget(QWidget* surface, QPainter& painter)
{
    if (AppBackgroundPainter* backgroundPainter = appBackgroundPainterForWidget(surface);
        backgroundPainter != nullptr) {
        return backgroundPainter->paintBackgroundForSurface(surface, painter);
    }
    return false;
}

bool appBackgroundIsActiveForTheme()
{
    return qApp != nullptr && qApp->property(kAppBackgroundActiveProperty).toBool();
}

AppBackgroundSurfaceWidget::AppBackgroundSurfaceWidget(QWidget* parent)
    : QWidget(parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceWidget::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QWidget::paintEvent(event);
}

AppBackgroundSurfaceFrame::AppBackgroundSurfaceFrame(QWidget* parent)
    : QFrame(parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceFrame::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QFrame::paintEvent(event);
}

AppBackgroundSurfaceTabWidget::AppBackgroundSurfaceTabWidget(QWidget* parent)
    : QTabWidget(parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceTabWidget::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QTabWidget::paintEvent(event);
}

AppBackgroundSurfaceMenuBar::AppBackgroundSurfaceMenuBar(QWidget* parent)
    : QMenuBar(parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceMenuBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QMenuBar::paintEvent(event);
}

AppBackgroundSurfaceToolBar::AppBackgroundSurfaceToolBar(const QString& title, QWidget* parent)
    : QToolBar(title, parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceToolBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QToolBar::paintEvent(event);
}

AppBackgroundSurfaceStatusBar::AppBackgroundSurfaceStatusBar(QWidget* parent)
    : QStatusBar(parent)
{
    prepareBackgroundSurface(this);
}

void AppBackgroundSurfaceStatusBar::paintEvent(QPaintEvent* event)
{
    paintSurfaceBackdrop(this);
    QStatusBar::paintEvent(event);
}

}  // namespace miacode::ui
