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
    };

    explicit CoverLayerListModel(QObject* parent = nullptr);

    void setLayoutModel(CoverLayoutModel* model);
    CoverLayoutModel* layoutModel() const { return model_; }
    CoverLayer* layerAt(int row) const;
    int rowForKey(const QString& key) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void refresh();

private:
    void connectLayerSignals(CoverLayer* layer);

    CoverLayoutModel* model_ = nullptr;
};

}  // namespace miacode::cover_export
