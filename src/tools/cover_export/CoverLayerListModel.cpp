#include "tools/cover_export/CoverLayerListModel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "UiText.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QStringList>

#include <algorithm>

namespace miacode::cover_export {
namespace {

constexpr char kRowMimeType[] = "application/x-miacode-cover-layer-row";

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
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
        if (layer->kind() == QStringLiteral("chartFrame")) {
            return QStringLiteral("%1 %2").arg(displayLabel(layer), QString::number(layer->frameSeconds(), 'f', 2));
        }
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
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable
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
    connect(layer, &CoverLayer::zChanged, this, &CoverLayerListModel::refresh);
}

}  // namespace miacode::cover_export
