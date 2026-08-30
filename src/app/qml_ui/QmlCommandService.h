#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

class MainWindow;
class QmlDocumentModel;

class QmlCommandService final : public QObject
{
    Q_OBJECT
public:
    QmlCommandService(MainWindow& backend, QmlDocumentModel& document, QObject* parent = nullptr);

    // Everything that would discard the open document goes through one guard,
    // and the guard lives here rather than in each caller: an entry point that
    // forgets to ask is how work gets lost, and there is no way to forget if
    // asking is what opening *is*.
    //
    // These do not return a verdict — the answer arrives from a dialog, so
    // there is none to return yet. The action happens later, or not at all.
    Q_INVOKABLE void openDocument(const QUrl& fileUrl);
    Q_INVOKABLE void openRecentDocument(const QString& path);
    Q_INVOKABLE void closeDocument();
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
    // The registry ids v2 binds as window shortcuts, in one place so QML does
    // not carry a second copy of the command table. The shell binds them to the
    // editor, not back to the backend — see QmlDocumentModel::transformChartSelection.
    Q_INVOKABLE QStringList shortcutCommandIds() const;

private:
    void whenDocumentMayBeLeft(std::function<void()> proceed);

    MainWindow* backend_ = nullptr;
    QmlDocumentModel* document_ = nullptr;
};
