#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>

class MainWindow;
class QBoxLayout;
class QWidget;

// Hosts one v1 editor-stack page (Export / Latency) inside the v2 editor
// area via WindowContainer. Sidebar stays pure QML; the full QWidget pages
// keep their existing MainWindow lifecycle (switchTo* / onPageEntered).
class QmlEditorPageHost final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activePageId READ activePageId NOTIFY activePageIdChanged)
    Q_PROPERTY(bool overlayActive READ overlayActive NOTIFY activePageIdChanged)
    Q_PROPERTY(QWindow* pageWindow READ pageWindow NOTIFY pageWindowChanged)

public:
    explicit QmlEditorPageHost(MainWindow& backend, QObject* parent = nullptr);
    ~QmlEditorPageHost() override;

    QString activePageId() const { return activePageId_; }
    bool overlayActive() const { return !activePageId_.isEmpty(); }
    QWindow* pageWindow() const;

    Q_INVOKABLE bool openExportPage();
    Q_INVOKABLE bool openLatencyPage();
    Q_INVOKABLE bool leaveOverlayPage();
    Q_INVOKABLE void openMediaProcessingTools();
    Q_INVOKABLE void openNormalizeWholeChart();
    Q_INVOKABLE void openNetBatchDownload();
    Q_INVOKABLE void openNetBatchUpload();
    Q_INVOKABLE void openBatchExport();
    Q_INVOKABLE void syncPageSize(int width, int height);

signals:
    void activePageIdChanged();
    void pageWindowChanged();

private:
    void ensureSurface();
    void setSurfaceVisible(bool visible);
    bool attachPageWidget(QWidget* page, const QString& pageId);
    void detachCurrentPage(bool restoreToEditorStack);
    void rememberResumeDifficulty();
    bool resumeChartOrMetadata();

    MainWindow* backend_ = nullptr;
    QWidget* surfaceWidget_ = nullptr;
    QBoxLayout* surfaceLayout_ = nullptr;
    QPointer<QWindow> pageWindow_;
    QPointer<QWidget> attachedPage_;
    QString activePageId_;
    int resumeDifficultyId_ = 0;
};
