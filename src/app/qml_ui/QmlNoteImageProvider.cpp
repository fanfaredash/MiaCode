#include "QmlNoteImageProvider.h"

#include "QmlPreviewModel.h"
#include "timeline/TimelineNoteAssets.h"

#include <QImage>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickImageProvider>

namespace {

QString spriteTypeForKind(const QString& kind)
{
    if (kind == QStringLiteral("break")) {
        return QStringLiteral("tap_break");
    }
    return kind;
}

class NoteImageProvider final : public QQuickImageProvider
{
public:
    explicit NoteImageProvider(QmlPreviewModel* model)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , model_(model)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        QString kind = id;
        const int queryPosition = kind.indexOf(QLatin1Char('?'));
        if (queryPosition >= 0) {
            kind.truncate(queryPosition);
        }

        const QString skinDirectory = model_->currentSkinDirectory();
        if (skinDirectory != cachedSkinDirectory_) {
            cachedSkinDirectory_ = skinDirectory;
            assets_ = miacode::timeline::loadTimelineNoteAssets(skinDirectory);
        }

        QImage image = assets_.noteIcons.value(spriteTypeForKind(kind)).toImage();
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
        } else if (requestedSize.isValid()) {
            image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (size != nullptr) {
            *size = image.size();
        }
        return image;
    }

private:
    QPointer<QmlPreviewModel> model_;
    QString cachedSkinDirectory_;
    miacode::timeline::TimelineNoteAssetSet assets_;
};

} // namespace

void registerQmlNoteImageProvider(QQmlEngine* engine, QmlPreviewModel* model)
{
    engine->addImageProvider(QStringLiteral("noteicon"), new NoteImageProvider(model));
}
