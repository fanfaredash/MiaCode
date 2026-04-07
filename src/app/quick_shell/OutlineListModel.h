#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QVector>

class QListWidget;

class OutlineListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        KindRole,
        DifficultyIdRole,
        SelectedRole,
        CanDeleteRole,
        BadgeVisibleRole,
        BadgeColorRole,
        GlyphRole,
        GlyphColorRole,
    };

    explicit OutlineListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refreshFromList(QListWidget* list, int activeDifficultyId);

private:
    struct Entry {
        QString label;
        QString kind;
        int difficultyId = 0;
        bool selected = false;
        bool canDelete = false;
        bool badgeVisible = false;
        QColor badgeColor;
        QString glyph;
        QColor glyphColor;
    };

    QVector<Entry> entries_;
};
