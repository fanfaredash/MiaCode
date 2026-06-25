#pragma once

#include <QAbstractListModel>

namespace miacode::cover_export {

class CoverLayer;
class CoverLayoutModel;

class CoverLayerListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        KindRole,
        LabelRole,
        VisibleRole,
        LockedRole,
        FrameSecondsRole,
        OpacityRole,
        SubtitleRole,
        ThumbnailRole,
    };

    explicit CoverLayerListModel(QObject* parent = nullptr);

    void setLayoutModel(CoverLayoutModel* model);
    CoverLayoutModel* layoutModel() const { return model_; }
    CoverLayer* layerAt(int row) const;
    int rowForKey(const QString& key) const;
    void setNextDropRowOverride(int row);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    // Drag-to-reorder (InternalMove). The move is applied by reordering z on the
    // CoverLayoutModel (view-row based, §12.12); dropMimeData returns false so the
    // view's default move/removeRows machinery doesn't also touch the rows.
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action,
                         int row, int column, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action,
                      int row, int column, const QModelIndex& parent) override;

public slots:
    void refresh();

private:
    void connectLayerSignals(CoverLayer* layer);

    CoverLayoutModel* model_ = nullptr;
    int nextDropRowOverride_ = -1;
};

}  // namespace miacode::cover_export
