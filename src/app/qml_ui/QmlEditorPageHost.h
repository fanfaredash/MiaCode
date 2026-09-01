#pragma once

#include "app/v2/EditorPageRouter.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include "app/v2/ShellNotifications.h"

class QmlExportSession;

// Page routing for the v2 editor area. Every full-page surface is QML now, so
// this only tracks which one is showing; no QWidget is ever adopted.
class QmlEditorPageHost final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activePageId READ activePageId NOTIFY activePageIdChanged)
    Q_PROPERTY(bool overlayActive READ overlayActive NOTIFY activePageIdChanged)
    Q_PROPERTY(QObject* exportSession READ exportSession CONSTANT)

public:
    // `backend` supplies the menu/shortcut signals and the export session;
    // every page switch goes through the router slot instead, so this host no
    // longer needs friend access to the window.
    explicit QmlEditorPageHost(miacode::v2::ShellNotifications& notifications,
                               miacode::v2::EditorPageRouter*& routerSlot,
                               QObject*& exportSessionSlot,
                               QObject* parent = nullptr);

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

    miacode::v2::ShellNotifications* notifications_ = nullptr;
    // Bound to the assembly's slot, not to a snapshot: the window withdraws the
    // router before teardown and that has to be visible here at once.
    miacode::v2::EditorPageRouter** routerSlot_ = nullptr;
    // The export page session, from the assembly rather than the window.
    QObject** exportSessionSlot_ = nullptr;
    QmlExportSession* exportSessionObject() const;
    miacode::v2::EditorPageRouter* router() const
    {
        return routerSlot_ != nullptr ? *routerSlot_ : nullptr;
    }
    QString activePageId_;
    int resumeDifficultyId_ = 0;
};
