#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

class MainWindow;
class QmlDocumentModel;

class QmlCommandService final : public QObject
{
    Q_OBJECT
public:
    QmlCommandService(MainWindow& backend, QmlDocumentModel& document, QObject* parent = nullptr);

    Q_INVOKABLE bool openDocument(const QUrl& fileUrl);
    Q_INVOKABLE bool saveDocument();
    Q_INVOKABLE bool saveDocumentAs(const QUrl& fileUrl);
    Q_INVOKABLE void discardDocumentChanges();
    Q_INVOKABLE void validateDocument();
    Q_INVOKABLE void selectDifficulty(int id);
    Q_INVOKABLE bool addDifficulty(int id);
    Q_INVOKABLE bool removeDifficulty(int id);
    Q_INVOKABLE void enableUnifiedDesigner(const QString& canonicalName);
    Q_INVOKABLE void disableUnifiedDesigner();
    // Reuses MainWindow::onPreferences() — same dialog as v1 Tools/Preferences.
    Q_INVOKABLE void openPreferences();
    // Chart-transform commands reach the backend by their ShortcutRegistry id;
    // see MainWindow::triggerShortcutCommand for why v2 needs this at all.
    Q_INVOKABLE bool triggerShortcutCommand(const QString& id);
    // The registry ids v2 binds as window shortcuts, in one place so QML does
    // not carry a second copy of the command table.
    Q_INVOKABLE QStringList shortcutCommandIds() const;

private:
    MainWindow* backend_ = nullptr;
    QmlDocumentModel* document_ = nullptr;
};
