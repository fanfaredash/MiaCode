#include "OutlineListModel.h"

#include <QListWidget>
#include <QListWidgetItem>

OutlineListModel::OutlineListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int OutlineListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : entries_.size();
}

QVariant OutlineListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
        return {};
    }

    const Entry& entry = entries_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case LabelRole:
        return entry.label;
    case KindRole:
        return entry.kind;
    case DifficultyIdRole:
        return entry.difficultyId;
    case SelectedRole:
        return entry.selected;
    case CanDeleteRole:
        return entry.canDelete;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> OutlineListModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {KindRole, "kind"},
        {DifficultyIdRole, "difficultyId"},
        {SelectedRole, "selected"},
        {CanDeleteRole, "canDelete"},
    };
}

void OutlineListModel::refreshFromList(QListWidget* list, int activeDifficultyId)
{
    QVector<Entry> nextEntries;
    if (list != nullptr) {
        nextEntries.reserve(list->count());
        QListWidgetItem* currentItem = list->currentItem();
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem* item = list->item(row);
            if (item == nullptr) {
                continue;
            }
            Entry entry;
            entry.label = item->text();
            entry.kind = item->data(Qt::UserRole).toString();
            entry.difficultyId = item->data(Qt::UserRole + 1).toInt();
            entry.selected = (item == currentItem);
            entry.canDelete = (entry.kind == QStringLiteral("difficulty_chart")
                && entry.difficultyId == activeDifficultyId
                && entry.difficultyId > 0);
            nextEntries.push_back(entry);
        }
    }

    beginResetModel();
    entries_ = nextEntries;
    endResetModel();
}
