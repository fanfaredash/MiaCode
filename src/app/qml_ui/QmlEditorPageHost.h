#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class MainWindow;

// Page routing for the v2 editor area. Every full-page surface is QML now, so
// this only tracks which one is showing; no QWidget is ever adopted.
class QmlEditorPageHost final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activePageId READ activePageId NOTIFY activePageIdChanged)
    Q_PROPERTY(bool overlayActive READ overlayActive NOTIFY activePageIdChanged)
    Q_PROPERTY(QObject* exportSession READ exportSession CONSTANT)

public:
    explicit QmlEditorPageHost(MainWindow& backend, QObject* parent = nullptr);

    QString activePageId() const { return activePageId_; }
    bool overlayActive() const { return !activePageId_.isEmpty(); }
    QObject* exportSession() const;

    Q_INVOKABLE bool openVideoExportPage(const QString& tab = QStringLiteral("export"));
    Q_INVOKABLE bool openExportPage();
    Q_INVOKABLE bool openLatencyPage();
    Q_INVOKABLE bool leaveOverlayPage();
    Q_INVOKABLE void openMediaProcessingTools();
    // Normalize acts on the editor's live selection, so the host only asks;
    // EditorPane owns the options dialog and the transform.
    Q_INVOKABLE void openNormalizeWholeChart();
    Q_INVOKABLE void openBatchExport();
    Q_INVOKABLE bool openCoverExport(int difficultyId = 0);
    Q_INVOKABLE void packAsZip();

signals:
    void normalizeWholeChartRequested();
    void mediaToolsRequested();
    void preferencesRequested();
    void activePageIdChanged();
    void coverPageRequested(int difficultyId);

private:
    void rememberResumeDifficulty();
    bool resumeChartOrMetadata();
    void markExportPageActive();

    MainWindow* backend_ = nullptr;
    QString activePageId_;
    int resumeDifficultyId_ = 0;
};
