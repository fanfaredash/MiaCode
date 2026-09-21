#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace miacode::ui {

// Discovers the readable comic images shipped under the shared assets root
// (assets/comics) and exposes the minimal interface the random carousel needs.
// Scanning is deferred until the carousel is first shown: construction leaves
// the resource set empty and the caller drives refresh() when it needs the
// images. An absent or empty directory yields an empty resource set; individual
// unreadable images are skipped so the remaining ones still rotate.
class ComicResourceModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentImageUrl READ currentImageUrl NOTIFY currentChanged)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY resourcesChanged)

public:
    explicit ComicResourceModel(QObject* parent = nullptr);

    QString currentImageUrl() const;
    int resourceCount() const;

    Q_INVOKABLE void selectNextResource();
    Q_INVOKABLE void selectPreviousResource();
    Q_INVOKABLE void selectRandomResource();
    Q_INVOKABLE void selectResource(int index);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString imageUrlAt(int index) const;

signals:
    void currentChanged();
    void resourcesChanged();

private:
    struct ResourceEntry {
        QString fileName;
        QString imageUrl;
    };

    QVector<ResourceEntry> resources_;
    int currentIndex_ = -1;
};

}  // namespace miacode::ui
