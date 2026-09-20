#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace miacode::ui {

class ComicResourceModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentImageUrl READ currentImageUrl NOTIFY currentChanged)
    Q_PROPERTY(double currentAspectRatio READ currentAspectRatio NOTIFY currentChanged)
    Q_PROPERTY(bool currentImageLight READ currentImageLight NOTIFY currentChanged)
    Q_PROPERTY(bool usingFallback READ usingFallback NOTIFY currentChanged)
    Q_PROPERTY(bool fallbackAvailable READ fallbackAvailable NOTIFY currentChanged)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY resourcesChanged)

public:
    explicit ComicResourceModel(QObject* parent = nullptr);

    QString currentImageUrl() const;
    double currentAspectRatio() const;
    bool currentImageLight() const;
    bool usingFallback() const;
    bool fallbackAvailable() const;
    int resourceCount() const;

    Q_INVOKABLE void selectNextResource();
    Q_INVOKABLE void selectPreviousResource();
    Q_INVOKABLE void selectRandomResource();
    Q_INVOKABLE void useFallback();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString imageUrlAt(int index) const;
    Q_INVOKABLE double aspectRatioAt(int index) const;

signals:
    void currentChanged();
    void resourcesChanged();

private:
    struct ResourceEntry {
        QString fileName;
        QString absolutePath;
        QString imageUrl;
        int width = 0;
        int height = 0;
        double aspectRatio = 0.0;
        bool imageLight = false;
    };

    void scheduleRefresh();
    void updateWatcher(const QString& comicsDirectory);
    QString fallbackUrl() const;
    double fallbackAspectRatio() const;
    bool loadFallbackInfo();
    bool isSupportedImageName(const QString& fileName) const;
    bool isSafeRootFile(const QString& fileName,
                        const QString& comicsDirectory,
                        QString* absolutePath = nullptr) const;
    bool readResource(const QString& fileName,
                      const QString& comicsDirectory,
                      ResourceEntry* entry) const;

    QVector<ResourceEntry> resources_;
    int currentIndex_ = -1;
    bool fallbackAvailable_ = false;
    int fallbackWidth_ = 0;
    int fallbackHeight_ = 0;
    bool fallbackImageLight_ = false;
    QFileSystemWatcher watcher_;
    QTimer refreshTimer_;
};

}  // namespace miacode::ui
