#include "tools/cover_export/CoverLayerListModel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "UiText.h"

#include <QDataStream>
#include <QIODevice>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QPen>
#include <QStringList>

#include <algorithm>

namespace miacode::cover_export {
namespace {

constexpr char kRowMimeType[] = "application/x-miacode-cover-layer-row";

QString l10n(const QString& en, const QString& zh)
{
    return UiText::localized(en, zh);
}

QString displayLabel(const CoverLayer* layer)
{
    if (layer == nullptr) {
        return QString();
    }
    if (layer->kind() == QStringLiteral("card")) {
        return l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡片"));
    }
    if (layer->kind() == QStringLiteral("chartFrame")) {
        return l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧"));
    }
    return layer->label();
}

QString formatFrameTime(double seconds)
{
    const int totalCs = qMax(0, qRound(seconds * 100.0));
    const int minutes = totalCs / 6000;
    const int secs = (totalCs / 100) % 60;
    const int cs = totalCs % 100;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(cs, 2, 10, QChar('0'));
}

QImage cardThumbnail(const CoverLayer* layer)
{
    QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor border(92, 114, 146, 180);
    const QColor cardFill(248, 250, 252, layer != nullptr && layer->visible() ? 245 : 95);
    const QColor tabFill(59, 130, 246, layer != nullptr && layer->visible() ? 220 : 95);

    QRectF card(13.0, 7.0, 22.0, 34.0);
    painter.setPen(QPen(border, 1.2));
    painter.setBrush(cardFill);
    painter.drawRoundedRect(card, 4.0, 4.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(tabFill);
    painter.drawRoundedRect(QRectF(16.0, 11.0, 16.0, 7.0), 3.0, 3.0);

    painter.setPen(QPen(QColor(100, 116, 139, layer != nullptr && layer->visible() ? 180 : 80), 1.0));
    painter.drawLine(QPointF(17.0, 24.0), QPointF(31.0, 24.0));
    painter.drawLine(QPointF(17.0, 29.0), QPointF(31.0, 29.0));
    return image;
}

QImage chartFrameThumbnail(const CoverLayer* layer)
{
    if (layer == nullptr) {
        return QImage();
    }
    QImage source = layer->frameImage();
    if (!source.isNull()) {
        return source.scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

    QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int alpha = layer->visible() ? 220 : 85;
    painter.setPen(QPen(QColor(76, 96, 124, alpha), 1.2));
    painter.setBrush(QColor(30, 41, 59, layer->visible() ? 185 : 75));
    painter.drawRoundedRect(QRectF(7.5, 7.5, 33.0, 33.0), 5.0, 5.0);
    painter.setPen(QPen(QColor(148, 163, 184, alpha), 1.0));
    painter.drawEllipse(QPointF(24.0, 24.0), 10.0, 10.0);
    painter.drawLine(QPointF(24.0, 9.0), QPointF(24.0, 39.0));
    painter.drawLine(QPointF(9.0, 24.0), QPointF(39.0, 24.0));
    return image;
}

}  // namespace

CoverLayerListModel::CoverLayerListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void CoverLayerListModel::setLayoutModel(CoverLayoutModel* model)
{
    if (model_ == model) {
        return;
    }
    beginResetModel();
    if (model_ != nullptr) {
        disconnect(model_, nullptr, this, nullptr);
    }
    model_ = model;
    if (model_ != nullptr) {
        connect(model_, &CoverLayoutModel::layersChanged, this, &CoverLayerListModel::refresh);
        for (CoverLayer* layer : model_->layers()) {
            connectLayerSignals(layer);
        }
    }
    endResetModel();
}

CoverLayer* CoverLayerListModel::layerAt(int row) const
{
    if (model_ == nullptr || row < 0 || row >= model_->layers().size()) {
        return nullptr;
    }
    QList<CoverLayer*> layers = model_->layers();
    std::sort(layers.begin(), layers.end(), [](const CoverLayer* a, const CoverLayer* b) {
        return a->z() > b->z();
    });
    return layers.at(row);
}

int CoverLayerListModel::rowForKey(const QString& key) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (CoverLayer* layer = layerAt(row); layer != nullptr && layer->key() == key) {
            return row;
        }
    }
    return -1;
}

void CoverLayerListModel::setNextDropRowOverride(int row)
{
    nextDropRowOverride_ = row;
}

int CoverLayerListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || model_ == nullptr) {
        return 0;
    }
    return model_->layers().size();
}

QVariant CoverLayerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    CoverLayer* layer = layerAt(index.row());
    if (layer == nullptr) {
        return QVariant();
    }
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return displayLabel(layer);
    case Qt::CheckStateRole:
        return layer->visible() ? Qt::Checked : Qt::Unchecked;
    case KeyRole:
        return layer->key();
    case KindRole:
        return layer->kind();
    case VisibleRole:
        return layer->visible();
    case LockedRole:
        return layer->locked();
    case FrameSecondsRole:
        return layer->frameSeconds();
    case OpacityRole:
        return layer->opacity();
    case SubtitleRole:
        if (layer->kind() == QStringLiteral("chartFrame")) {
            return l10n(QStringLiteral("Frame "), QStringLiteral("帧时间 ")) + formatFrameTime(layer->frameSeconds());
        }
        return l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡"));
    case ThumbnailRole:
        return layer->kind() == QStringLiteral("chartFrame")
            ? chartFrameThumbnail(layer)
            : cardThumbnail(layer);
    default:
        return QVariant();
    }
}

Qt::ItemFlags CoverLayerListModel::flags(const QModelIndex& index) const
{
    // Dropping onto empty space (invalid index) is allowed → "send to back".
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable
         | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

Qt::DropActions CoverLayerListModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList CoverLayerListModel::mimeTypes() const
{
    return {QString::fromLatin1(kRowMimeType)};
}

QMimeData* CoverLayerListModel::mimeData(const QModelIndexList& indexes) const
{
    auto* data = new QMimeData();
    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            QByteArray bytes;
            QDataStream stream(&bytes, QIODevice::WriteOnly);
            stream << index.row();
            data->setData(QString::fromLatin1(kRowMimeType), bytes);
            break;   // single-selection list — one row
        }
    }
    return data;
}

bool CoverLayerListModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                          int, int, const QModelIndex&) const
{
    return data != nullptr && data->hasFormat(QString::fromLatin1(kRowMimeType));
}

bool CoverLayerListModel::dropMimeData(const QMimeData* data, Qt::DropAction,
                                       int row, int, const QModelIndex& parent)
{
    if (model_ == nullptr || data == nullptr || !data->hasFormat(QString::fromLatin1(kRowMimeType))) {
        return false;
    }
    QByteArray bytes = data->data(QString::fromLatin1(kRowMimeType));
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    int sourceRow = -1;
    stream >> sourceRow;
    if (sourceRow < 0) {
        return false;
    }
    int targetRow = row;
    if (nextDropRowOverride_ >= 0) {
        targetRow = nextDropRowOverride_;
        nextDropRowOverride_ = -1;
    }
    if (targetRow < 0) {
        targetRow = parent.isValid() ? parent.row() : rowCount();
    }
    model_->moveByViewRows(sourceRow, targetRow);
    // The reorder already triggered a model reset (via layersChanged); return false
    // so the view's own InternalMove machinery doesn't ALSO move/remove the rows.
    return false;
}

bool CoverLayerListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    CoverLayer* layer = layerAt(index.row());
    if (layer == nullptr) {
        return false;
    }
    if (role == Qt::CheckStateRole || role == VisibleRole) {
        layer->setVisible(role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool());
        emit dataChanged(index, index, {role, Qt::CheckStateRole, VisibleRole});
        return true;
    }
    return false;
}

QHash<int, QByteArray> CoverLayerListModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles.insert(KeyRole, "key");
    roles.insert(KindRole, "kind");
    roles.insert(LabelRole, "label");
    roles.insert(VisibleRole, "visible");
    roles.insert(LockedRole, "locked");
    roles.insert(FrameSecondsRole, "frameSeconds");
    roles.insert(OpacityRole, "opacity");
    roles.insert(SubtitleRole, "subtitle");
    roles.insert(ThumbnailRole, "thumbnail");
    return roles;
}

void CoverLayerListModel::refresh()
{
    beginResetModel();
    if (model_ != nullptr) {
        for (CoverLayer* layer : model_->layers()) {
            connectLayerSignals(layer);
        }
    }
    endResetModel();
}

void CoverLayerListModel::connectLayerSignals(CoverLayer* layer)
{
    if (layer == nullptr) {
        return;
    }
    disconnect(layer, nullptr, this, nullptr);
    auto updateLayerRow = [this, layer] {
        const int row = rowForKey(layer->key());
        if (row < 0) {
            return;
        }
        const QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx);
    };
    connect(layer, &CoverLayer::visibleChanged, this, updateLayerRow);
    connect(layer, &CoverLayer::lockedChanged, this, updateLayerRow);
    connect(layer, &CoverLayer::opacityChanged, this, updateLayerRow);
    connect(layer, &CoverLayer::frameSecondsChanged, this, updateLayerRow);
    connect(layer, &CoverLayer::imageRevisionChanged, this, updateLayerRow);
    connect(layer, &CoverLayer::zChanged, this, &CoverLayerListModel::refresh);
}

}  // namespace miacode::cover_export
