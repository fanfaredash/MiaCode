#include "OutlineListModel.h"

#include <QColor>
#include <QListWidget>
#include <QListWidgetItem>

namespace {

QColor difficultyColor(int difficultyId)
{
    switch (difficultyId) {
    case 1:
        return QColor(QStringLiteral("#69A6FF"));
    case 2:
        return QColor(QStringLiteral("#78C85A"));
    case 3:
        return QColor(QStringLiteral("#DCC548"));
    case 4:
        return QColor(QStringLiteral("#E35C50"));
    case 5:
        return QColor(QStringLiteral("#7A4FD1"));
    case 6:
        return QColor(QStringLiteral("#D548B6"));
    case 7:
        return QColor(QStringLiteral("#E29A46"));
    default:
        return QColor(QStringLiteral("#8A8F98"));
    }
}

}  // namespace

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
    case BadgeVisibleRole:
        return entry.badgeVisible;
    case BadgeColorRole:
        return entry.badgeColor;
    case GlyphRole:
        return entry.glyph;
    case GlyphColorRole:
        return entry.glyphColor;
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
        {BadgeVisibleRole, "badgeVisible"},
        {BadgeColorRole, "badgeColor"},
        {GlyphRole, "glyph"},
        {GlyphColorRole, "glyphColor"},
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
            if (entry.kind == QStringLiteral("difficulty_chart") && entry.difficultyId > 0) {
                entry.badgeVisible = true;
                entry.badgeColor = difficultyColor(entry.difficultyId);
            } else if (entry.kind == QStringLiteral("metadata")) {
                entry.glyph = QStringLiteral("M");
                entry.glyphColor = QColor(QStringLiteral("#7B8798"));
            } else if (entry.kind == QStringLiteral("add")) {
                entry.glyph = QStringLiteral("+");
                entry.glyphColor = QColor(QStringLiteral("#5D6E83"));
            } else if (entry.kind == QStringLiteral("toolbox")) {
                entry.glyph = QStringLiteral("T");
                entry.glyphColor = QColor(QStringLiteral("#E6B84A"));
            }
            nextEntries.push_back(entry);
        }
    }

    beginResetModel();
    entries_ = nextEntries;
    endResetModel();
}
