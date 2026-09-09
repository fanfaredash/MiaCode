#include "app/ui/AppBackgroundPainter.h"

#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOption>
#include <QUrl>
#include <QVariant>
#include <QtMath>

#include "common/DebugLog.h"
#include "common/AdoptedWidgetCoordinates.h"

namespace miacode::ui {

namespace {

constexpr auto kAppBackgroundPainterProperty = "miacode.appBackgroundPainter";
constexpr auto kAppBackgroundActiveProperty = "miacode.appBackgroundActive";
constexpr auto kAppBackgroundEnabledProperty = "miacode.appBackgroundEnabled";
constexpr auto kAppBackgroundImagePathProperty = "miacode.appBackgroundImagePath";
constexpr auto kAppBackgroundSourceUrlProperty = "miacode.appBackgroundSourceUrl";
constexpr auto kAppBackgroundOpacityProperty = "miacode.appBackgroundOpacity";
constexpr auto kAppBackgroundToolbarAlphaDarkProperty = "miacode.appBackgroundToolbarAlphaDark";
constexpr auto kAppBackgroundToolbarAlphaLightProperty = "miacode.appBackgroundToolbarAlphaLight";
constexpr auto kAppBackgroundStatusAlphaDarkProperty = "miacode.appBackgroundStatusAlphaDark";
constexpr auto kAppBackgroundStatusAlphaLightProperty = "miacode.appBackgroundStatusAlphaLight";
constexpr auto kAppBackgroundPanelAlphaDarkProperty = "miacode.appBackgroundPanelAlphaDark";
constexpr auto kAppBackgroundPanelAlphaLightProperty = "miacode.appBackgroundPanelAlphaLight";
constexpr auto kAppBackgroundCardAlphaDarkProperty = "miacode.appBackgroundCardAlphaDark";
constexpr auto kAppBackgroundCardAlphaLightProperty = "miacode.appBackgroundCardAlphaLight";
constexpr auto kAppBackgroundEditorHeaderAlphaDarkProperty = "miacode.appBackgroundEditorHeaderAlphaDark";
constexpr auto kAppBackgroundEditorHeaderAlphaLightProperty = "miacode.appBackgroundEditorHeaderAlphaLight";
constexpr auto kAppBackgroundInputAlphaDarkProperty = "miacode.appBackgroundInputAlphaDark";
constexpr auto kAppBackgroundInputAlphaLightProperty = "miacode.appBackgroundInputAlphaLight";
constexpr auto kAppBackgroundCodeEditorAlphaDarkProperty = "miacode.appBackgroundCodeEditorAlphaDark";
constexpr auto kAppBackgroundCodeEditorAlphaLightProperty = "miacode.appBackgroundCodeEditorAlphaLight";
constexpr auto kAppBackgroundSizeModeProperty = "miacode.appBackgroundSizeMode";
constexpr auto kAppBackgroundPositionProperty = "miacode.appBackgroundPosition";

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

QString backgroundSettingsSignature(const AppBackgroundSettings& settings)
{
    const AppBackgroundOverlaySettings& overlays = settings.overlays;
    return QStringLiteral(
               "enabled=%1 image=%2 opacity=%3 overlays=%4,%5,%6,%7,%8,%9,"
               "%10,%11,%12,%13,%14,%15 size_mode=%16 position=%17")
        .arg(settings.enabled ? 1 : 0)
        .arg(settings.imagePath)
        .arg(settings.opacity, 0, 'f', 6)
        .arg(overlays.toolbarAlphaDark)
        .arg(overlays.toolbarAlphaLight)
        .arg(overlays.statusAlphaDark)
        .arg(overlays.statusAlphaLight)
        .arg(overlays.panelAlphaDark)
        .arg(overlays.panelAlphaLight)
        .arg(overlays.editorHeaderAlphaDark)
        .arg(overlays.editorHeaderAlphaLight)
        .arg(overlays.inputAlphaDark)
        .arg(overlays.inputAlphaLight)
        .arg(overlays.codeEditorAlphaDark)
        .arg(overlays.codeEditorAlphaLight)
        .arg(static_cast<int>(settings.sizeMode))
        .arg(static_cast<int>(settings.position));
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
        && normalized.overlays.toolbarAlphaDark == settings_.overlays.toolbarAlphaDark
        && normalized.overlays.toolbarAlphaLight == settings_.overlays.toolbarAlphaLight
        && normalized.overlays.statusAlphaDark == settings_.overlays.statusAlphaDark
        && normalized.overlays.statusAlphaLight == settings_.overlays.statusAlphaLight
        && normalized.overlays.panelAlphaDark == settings_.overlays.panelAlphaDark
        && normalized.overlays.panelAlphaLight == settings_.overlays.panelAlphaLight
        && normalized.overlays.editorHeaderAlphaDark == settings_.overlays.editorHeaderAlphaDark
        && normalized.overlays.editorHeaderAlphaLight == settings_.overlays.editorHeaderAlphaLight
        && normalized.overlays.inputAlphaDark == settings_.overlays.inputAlphaDark
        && normalized.overlays.inputAlphaLight == settings_.overlays.inputAlphaLight
        && normalized.overlays.codeEditorAlphaDark == settings_.overlays.codeEditorAlphaDark
        && normalized.overlays.codeEditorAlphaLight == settings_.overlays.codeEditorAlphaLight
        && normalized.sizeMode == settings_.sizeMode
        && normalized.position == settings_.position) {
        if (unchangedSettingsLogGate_.shouldEmit(backgroundSettingsSignature(normalized))) {
            logBackgroundDiag(QStringLiteral(
                "action=set_settings_unchanged enabled=%1 opacity=%2 image_path=%3")
                .arg(settings_.enabled ? 1 : 0)
                .arg(settings_.opacity)
                .arg(quotedDiag(settings_.imagePath)));
        }
        return;
    }

    unchangedSettingsLogGate_.reset();
    logBackgroundDiag(QStringLiteral(
        "action=set_settings enabled=%1 opacity=%2 image_path=%3")
        .arg(normalized.enabled ? 1 : 0)
        .arg(normalized.opacity)
        .arg(quotedDiag(normalized.imagePath)));
    const bool imageChanged = normalized.imagePath != settings_.imagePath
        || normalized.sizeMode != settings_.sizeMode
        || normalized.position != settings_.position;
    settings_ = normalized;
    if (imageChanged) {
        invalidateCache();
    }
    ensureSourceLoaded();
    updateApplicationActiveFlag();
    requestSurfaceUpdates();
}

void AppBackgroundPainter::reloadSource()
{
    sourceImage_ = QImage();
    loadedPath_.clear();
    sourceLoadAttempted_ = false;
    invalidateCache();
    ensureSourceLoaded();
    ++sourceRevision_;
    updateApplicationActiveFlag();
    requestSurfaceUpdates();
}

void AppBackgroundPainter::setCanvasGeometryGlobal(const QRect& geometry)
{
    const QRect normalized = geometry.isValid() ? geometry : QRect();
    if (canvasGeometryGlobal_ == normalized) {
        return;
    }
    const bool sizeChanged = canvasGeometryGlobal_.size() != normalized.size();
    canvasGeometryGlobal_ = normalized;
    if (sizeChanged) {
        invalidateCache();
    }
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
        sourceLoadAttempted_ = false;
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

    const QPixmap pixmap = renderedPixmap(canvasSize(), surface->devicePixelRatioF());
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

    const QPoint canvasTopLeft = canvasGeometryGlobal_.isValid()
        ? canvasGeometryGlobal_.topLeft()
        : mapWidgetPointToGlobal(window_, QPoint());
    const QPoint sourceTopLeft = mapWidgetPointToGlobal(surface, QPoint()) - canvasTopLeft;
    const QRect sourceRect(sourceTopLeft, surface->size());
    const QRect clippedSource = sourceRect.intersected(QRect(QPoint(), canvasSize()));
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
    painter.save();
    // Each native surface reconstructs the same window backdrop. Start from the
    // window color so nested surfaces do not accumulate the image opacity.
    painter.fillRect(targetRect, qApp->palette().color(QPalette::Window));
    painter.setOpacity(painter.opacity() * settings_.opacity);
    const qreal dpr = pixmap.devicePixelRatio();
    painter.drawPixmap(QRectF(targetRect), pixmap,
                       // All surfaces sample the same physical-pixel grid, including
                       // odd logical origins at fractional display scales.
                       QRectF(qRound(clippedSource.x() * dpr), qRound(clippedSource.y() * dpr),
                              clippedSource.width() * dpr, clippedSource.height() * dpr));
    painter.restore();
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
        case QEvent::DevicePixelRatioChange:
            // Raster reuse is keyed by canvas size and DPR, not theme/layout events.
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
    if (sourceLoadAttempted_ && loadedPath_ == settings_.imagePath) {
        return !sourceImage_.isNull();
    }

    sourceLoadAttempted_ = true;
    sourceImage_ = QImage();
    loadedPath_ = settings_.imagePath;
    if (settings_.imagePath.isEmpty() || !QFileInfo::exists(settings_.imagePath)) {
        logBackgroundDiag(QStringLiteral(
            "action=source_load_failed reason=missing_path image_path=%1")
            .arg(quotedDiag(settings_.imagePath)));
        return false;
    }

    QImageReader reader(settings_.imagePath);
    QImage image = reader.read();
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

QSize AppBackgroundPainter::canvasSize() const
{
    if (canvasGeometryGlobal_.isValid()) {
        return canvasGeometryGlobal_.size();
    }
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

QPixmap AppBackgroundPainter::renderedPixmap(const QSize& canvasSize, qreal dpr)
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
        && cachedSettings_.imagePath == settings_.imagePath
        && qFuzzyCompare(cachedPixmap_.devicePixelRatio(), dpr)
        && cachedSettings_.sizeMode == settings_.sizeMode
        && cachedSettings_.position == settings_.position) {
        return cachedPixmap_;
    }

    const QSize pixelSize(qCeil(canvasSize.width() * dpr), qCeil(canvasSize.height() * dpr));
    QImage canvas(pixelSize, QImage::Format_ARGB32_Premultiplied);
    canvas.setDevicePixelRatio(dpr);
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

    const QPixmap result = QPixmap::fromImage(canvas);

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
        !sourceImage_.isNull() && loadedPath_ == settings_.imagePath;
    logBackgroundDiag(QStringLiteral(
        "action=active_flag enabled=%1 opacity=%2 image_path_empty=%3 file_exists=%4 image_decoded=%5 active=%6 image_path=%7")
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
    qApp->setProperty("miacode.appBackgroundSourceRevision", sourceRevision_);
    qApp->setProperty(kAppBackgroundEnabledProperty, settings_.enabled);
    qApp->setProperty(kAppBackgroundImagePathProperty, settings_.imagePath);
    qApp->setProperty(
        kAppBackgroundSourceUrlProperty,
        settings_.imagePath.isEmpty() ? QString() : QUrl::fromLocalFile(settings_.imagePath).toString());
    qApp->setProperty(kAppBackgroundOpacityProperty, settings_.opacity);
    qApp->setProperty(kAppBackgroundToolbarAlphaDarkProperty, settings_.overlays.toolbarAlphaDark);
    qApp->setProperty(kAppBackgroundToolbarAlphaLightProperty, settings_.overlays.toolbarAlphaLight);
    qApp->setProperty(kAppBackgroundStatusAlphaDarkProperty, settings_.overlays.statusAlphaDark);
    qApp->setProperty(kAppBackgroundStatusAlphaLightProperty, settings_.overlays.statusAlphaLight);
    qApp->setProperty(kAppBackgroundPanelAlphaDarkProperty, settings_.overlays.panelAlphaDark);
    qApp->setProperty(kAppBackgroundPanelAlphaLightProperty, settings_.overlays.panelAlphaLight);
    qApp->setProperty(kAppBackgroundCardAlphaDarkProperty, kAppBackgroundOverlayAlphaMax);
    qApp->setProperty(kAppBackgroundCardAlphaLightProperty, kAppBackgroundOverlayAlphaMax);
    qApp->setProperty(kAppBackgroundEditorHeaderAlphaDarkProperty, settings_.overlays.editorHeaderAlphaDark);
    qApp->setProperty(kAppBackgroundEditorHeaderAlphaLightProperty, settings_.overlays.editorHeaderAlphaLight);
    qApp->setProperty(kAppBackgroundInputAlphaDarkProperty, settings_.overlays.inputAlphaDark);
    qApp->setProperty(kAppBackgroundInputAlphaLightProperty, settings_.overlays.inputAlphaLight);
    qApp->setProperty(kAppBackgroundCodeEditorAlphaDarkProperty, settings_.overlays.codeEditorAlphaDark);
    qApp->setProperty(kAppBackgroundCodeEditorAlphaLightProperty, settings_.overlays.codeEditorAlphaLight);
    qApp->setProperty(kAppBackgroundSizeModeProperty, appBackgroundSizeModeToken(settings_.sizeMode));
    qApp->setProperty(kAppBackgroundPositionProperty, appBackgroundPositionToken(settings_.position));
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
    if (appBackgroundIsActiveForTheme()) {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }
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
