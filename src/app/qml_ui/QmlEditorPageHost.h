#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>

class MainWindow;
class QBoxLayout;
class QWidget;

// Hosts v1 LatencyDetectionPage inside the v2 editor area via WindowContainer.
// Video export uses pure QML (QmlExportSession + ExportVideoPage) — no Widgets page
// is never attached in the QML shell.
class QmlEditorPageHost final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activePageId READ activePageId NOTIFY activePageIdChanged)
    Q_PROPERTY(bool overlayActive READ overlayActive NOTIFY activePageIdChanged)
    Q_PROPERTY(QWindow* pageWindow READ pageWindow NOTIFY pageWindowChanged)
    Q_PROPERTY(QObject* exportSession READ exportSession CONSTANT)

public:
    explicit QmlEditorPageHost(MainWindow& backend, QObject* parent = nullptr);
    ~QmlEditorPageHost() override;

    QString activePageId() const { return activePageId_; }
    bool overlayActive() const { return !activePageId_.isEmpty(); }
    QWindow* pageWindow() const;
    QObject* exportSession() const;

    Q_INVOKABLE bool openVideoExportPage(const QString& tab = QStringLiteral("export"));
    Q_INVOKABLE bool openExportPage();
    Q_INVOKABLE bool openLatencyPage();
    Q_INVOKABLE bool leaveOverlayPage();
    Q_INVOKABLE void openMediaProcessingTools();
    Q_INVOKABLE void openNormalizeWholeChart();
    Q_INVOKABLE void openNetBatchDownload();
    Q_INVOKABLE void openNetBatchUpload();
    Q_INVOKABLE void openBatchExport();
    Q_INVOKABLE void openCoverExport();
    Q_INVOKABLE void packAsZip();
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
    void markExportPageActive();

    MainWindow* backend_ = nullptr;
    QWidget* surfaceWidget_ = nullptr;
    QBoxLayout* surfaceLayout_ = nullptr;
    QPointer<QWindow> pageWindow_;
    QPointer<QWidget> attachedPage_;
    QString activePageId_;
    int resumeDifficultyId_ = 0;
};
