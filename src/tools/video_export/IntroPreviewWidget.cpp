#include "tools/video_export/IntroPreviewWidget.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QUrl>
#include <QVBoxLayout>

#include "common/IntroConfig.h"

namespace {

// Parse the bundled banner template once per widget — same C++-side injection
// the export session uses (the QML's async XHR loader is unreliable for a
// just-created scene, and the export path never exercises it either).
QVariantMap loadBannerTemplateMap()
{
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (!templateFile.open(QIODevice::ReadOnly)) {
        return QVariantMap();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
    return doc.isObject() ? doc.object().toVariantMap() : QVariantMap();
}

} // namespace

IntroPreviewWidget::IntroPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kBoxWidth, kBoxHeight);

    view_ = new QQuickView();
    view_->setResizeMode(QQuickView::SizeRootObjectToView);
    view_->setColor(Qt::black);
    view_->setSource(QUrl(QString::fromLatin1(miacode::intro::kOverlayQmlUrl)));
    view_->installEventFilter(this);

    // The container takes ownership of view_.
    QWidget* container = QWidget::createWindowContainer(view_, this);
    container->setFocusPolicy(Qt::NoFocus);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container);

    if (QQuickItem* root = view_->rootObject()) {
        root->setProperty("bannerTemplateData", loadBannerTemplateMap());
        root->setProperty("logoImage", QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)));
        root->setProperty("frame", kPreviewFrame);
        // Interpose the framebuffer-stand-in clip item between the window's
        // content item and the overlay root, so the centre-crop overhang of
        // the 16:9 overlay is cut exactly like the export viewport cuts it.
        clipItem_ = new QQuickItem(root->parentItem());
        clipItem_->setClip(true);
        root->setParentItem(clipItem_);
    }
    // SizeRootObjectToView stretches the root to the view on every native
    // resize, which lands asynchronously after the container's layout pass —
    // re-letterbox whenever it fires (the box is fixed, so in practice once).
    connect(view_, &QWindow::widthChanged, this, [this]() { updateRootGeometry(); });
    connect(view_, &QWindow::heightChanged, this, [this]() { updateRootGeometry(); });
    updateRootGeometry();
}

QQuickItem* IntroPreviewWidget::rootObject() const
{
    return view_ != nullptr ? view_->rootObject() : nullptr;
}

bool IntroPreviewWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Display-only preview: a click may still activate the native Quick
    // window, so bounce its key events back to the host window (dialog or
    // embedded panel page) instead of letting them die here.
    if (watched == view_
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        if (QWidget* host = window(); host != nullptr) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            QKeyEvent forwarded(
                keyEvent->type(),
                keyEvent->key(),
                keyEvent->modifiers(),
                keyEvent->text(),
                keyEvent->isAutoRepeat(),
                static_cast<ushort>(keyEvent->count()));
            QCoreApplication::sendEvent(host, &forwarded);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void IntroPreviewWidget::applySpec(const IntroBannerSpec& spec)
{
    QQuickItem* root = rootObject();
    if (root == nullptr) {
        return;
    }
    root->setProperty(
        "backgroundImage",
        spec.jacketPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(spec.jacketPath));
    root->setProperty("bannerTrack", introBannerTrackMap(spec));
    const QVariantMap style = introBannerStyleMap(spec);
    for (auto it = style.constBegin(); it != style.constEnd(); ++it) {
        root->setProperty(it.key().toUtf8().constData(), it.value());
    }
}

void IntroPreviewWidget::setOutputAspectRatio(double ratio)
{
    if (ratio > 0.0 && qIsFinite(ratio)) {
        outputAspectRatio_ = ratio;
    }
    updateRootGeometry();
}

void IntroPreviewWidget::updateRootGeometry()
{
    QQuickItem* root = rootObject();
    if (root == nullptr || clipItem_ == nullptr || view_ == nullptr) {
        return;
    }
    // The native view may not have been resized to the container yet — lay
    // out against the fixed box size, which is what the view ends up at.
    const qreal boxW = width() > 0 ? width() : kBoxWidth;
    const qreal boxH = height() > 0 ? height() : kBoxHeight;
    // Letterboxed "framebuffer" rect at the output aspect, centred in the
    // fixed box; the clear-color black fills the bars.
    qreal w = boxW;
    qreal h = w / outputAspectRatio_;
    if (h > boxH) {
        h = boxH;
        w = h * outputAspectRatio_;
    }
    clipItem_->setPosition(QPointF((boxW - w) / 2.0, (boxH - h) / 2.0));
    clipItem_->setSize(QSizeF(w, h));
    // EXACTLY PreviewQuickExportSession::applyIntroGeometry, with clipItem_
    // playing the framebuffer-viewport role: the overlay keeps its native
    // 16:9 1920x1080 layout, scales to the frame height around its centre,
    // and the sides past the frame edges are clipped (centre crop for
    // 1:1 / 4:3 outputs) — never re-laid-out at the output aspect.
    constexpr double kNativeW = 1920.0;
    constexpr double kNativeH = 1080.0;
    root->setWidth(kNativeW);
    root->setHeight(kNativeH);
    root->setTransformOrigin(QQuickItem::Center);
    root->setScale(h / kNativeH);
    root->setX(w / 2.0 - kNativeW / 2.0);
    root->setY(h / 2.0 - kNativeH / 2.0);
}
