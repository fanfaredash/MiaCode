#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace miacode::ui {

// Discovers images under the configured assets directory. Each refresh creates a new
// shuffled playback order; navigation then loops through that order.
class ImageResourceModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentImageUrl READ currentImageUrl NOTIFY currentChanged)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY resourcesChanged)

public:
    explicit ImageResourceModel(const QString& assetSubdirectory, QObject* parent = nullptr);

    QString currentImageUrl() const;
    int resourceCount() const;

    Q_INVOKABLE void selectNextResource();
    Q_INVOKABLE void selectPreviousResource();
    Q_INVOKABLE void refresh();

signals:
    void currentChanged();
    void resourcesChanged();

private:
    QString assetSubdirectory_;
    QVector<QString> resources_;
    int currentIndex_ = -1;
};

}  // namespace miacode::ui
