#include "IssueListModel.h"

#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QWidget>

namespace {

constexpr int kIssueLineRole = Qt::UserRole;
constexpr int kIssueColRole = Qt::UserRole + 1;
constexpr int kIssueAuxRole = Qt::UserRole + 2;
constexpr int kIssueTypeKeyRole = Qt::UserRole + 3;
constexpr int kIssueTypeLabelRole = Qt::UserRole + 4;
constexpr int kIssueIgnoredRole = Qt::UserRole + 5;

QString richTextForItem(QListWidget* list, QListWidgetItem* item)
{
    if (list == nullptr || item == nullptr) {
        return {};
    }
    QWidget* rowWidget = list->itemWidget(item);
    if (rowWidget == nullptr) {
        return item->text();
    }
    if (QLabel* label = rowWidget->findChild<QLabel*>(QStringLiteral("WrappedListEntryLabel")); label != nullptr) {
        return label->text();
    }
    return item->text();
}

}  // namespace

IssueListModel::IssueListModel(SourceKind sourceKind, QObject* parent)
    : QAbstractListModel(parent)
    , sourceKind_(sourceKind)
{
}

int IssueListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : entries_.size();
}

QVariant IssueListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) {
        return {};
    }

    const Entry& entry = entries_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case PlainTextRole:
        return entry.plainText;
    case RichTextRole:
        return entry.richText;
    case LineRole:
        return entry.line;
    case ColRole:
        return entry.col;
    case SecondRole:
        return entry.second;
    case IssueTypeKeyRole:
        return entry.issueTypeKey;
    case IssueTypeLabelRole:
        return entry.issueTypeLabel;
    case IgnoredRole:
        return entry.ignored;
    case EnabledRole:
        return entry.enabled;
    case WarningRole:
        return entry.warning;
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> IssueListModel::roleNames() const
{
    return {
        {RichTextRole, "richText"},
        {PlainTextRole, "plainText"},
        {LineRole, "line"},
        {ColRole, "col"},
        {SecondRole, "second"},
        {IssueTypeKeyRole, "issueTypeKey"},
        {IssueTypeLabelRole, "issueTypeLabel"},
        {IgnoredRole, "ignored"},
        {EnabledRole, "issueEnabled"},
        {WarningRole, "warning"},
    };
}

void IssueListModel::refreshFromList(QListWidget* list)
{
    QVector<Entry> nextEntries;
    if (list != nullptr) {
        nextEntries.reserve(list->count());
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem* item = list->item(row);
            if (item == nullptr) {
                continue;
            }
            Entry entry;
            entry.richText = richTextForItem(list, item);
            entry.plainText = item->toolTip();
            entry.line = item->data(kIssueLineRole).toInt();
            entry.col = item->data(kIssueColRole).toInt();
            entry.issueTypeKey = item->data(kIssueTypeKeyRole).toString();
            entry.issueTypeLabel = item->data(kIssueTypeLabelRole).toString();
            entry.ignored = item->data(kIssueIgnoredRole).toBool();
            entry.enabled = item->flags().testFlag(Qt::ItemIsEnabled);
            if (sourceKind_ == SourceKind::Muri) {
                entry.second = item->data(kIssueAuxRole).toDouble();
            } else {
                entry.warning = (item->data(kIssueAuxRole).toInt() == 1);
            }
            nextEntries.push_back(entry);
        }
    }

    beginResetModel();
    entries_ = nextEntries;
    endResetModel();
}
