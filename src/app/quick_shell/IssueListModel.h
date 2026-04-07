#pragma once

#include <QAbstractListModel>
#include <QVector>

class QListWidget;

class IssueListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum class SourceKind {
        Validation,
        Muri,
    };
    Q_ENUM(SourceKind)

    enum Role {
        RichTextRole = Qt::UserRole + 1,
        PlainTextRole,
        LineRole,
        ColRole,
        SecondRole,
        IssueTypeKeyRole,
        IssueTypeLabelRole,
        IgnoredRole,
        EnabledRole,
        WarningRole,
    };

    explicit IssueListModel(SourceKind sourceKind, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refreshFromList(QListWidget* list);

private:
    struct Entry {
        QString richText;
        QString plainText;
        int line = 1;
        int col = 1;
        double second = -1.0;
        QString issueTypeKey;
        QString issueTypeLabel;
        bool ignored = false;
        bool enabled = true;
        bool warning = false;
    };

    SourceKind sourceKind_;
    QVector<Entry> entries_;
};
