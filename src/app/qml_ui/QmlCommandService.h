#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

#include "app/v2/DocumentBridge.h"

class QmlDocumentModel;

class QmlCommandService final : public QObject
{
    Q_OBJECT
public:
    QmlCommandService(QmlDocumentModel& document,
                      miacode::v2::DocumentBridge*& bridgeSlot,
                      QObject* parent = nullptr);

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
    // 新建: guarded like every other way of leaving the open document, then a
    // folder pick and an empty maidata.txt inside it.
    Q_INVOKABLE void newDocument();
    // 恢复备份: restoring replaces the document, so it is guarded too.
    Q_INVOKABLE void restoreBackupDocument(const QString& path);
    Q_INVOKABLE bool saveDocument();
    Q_INVOKABLE bool saveDocumentAs(const QUrl& fileUrl);
    Q_INVOKABLE void discardDocumentChanges();
    Q_INVOKABLE void validateDocument();
    Q_INVOKABLE void selectDifficulty(int id);
    Q_INVOKABLE bool addDifficulty(int id);
    Q_INVOKABLE bool removeDifficulty(int id);
    Q_INVOKABLE void enableUnifiedDesigner(const QString& canonicalName);
    Q_INVOKABLE void disableUnifiedDesigner();
    // The registry ids v2 binds as window shortcuts, in one place so QML does
    // not carry a second copy of the command table. The shell binds them to the
    // editor, not back to the backend — see QmlDocumentModel::transformChartSelection.
    Q_INVOKABLE QStringList shortcutCommandIds() const;

private:
    void whenDocumentMayBeLeft(std::function<void()> proceed);

    miacode::v2::DocumentBridge** bridgeSlot_ = nullptr;
    miacode::v2::DocumentBridge* bridge() const
    {
        return bridgeSlot_ != nullptr ? *bridgeSlot_ : nullptr;
    }
    QmlDocumentModel* document_ = nullptr;
};
