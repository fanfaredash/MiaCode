#pragma once

#include "editor/SimaiTextEditPolicy.h"

#include <QObject>
#include <QVariantMap>

namespace miacode::qml_ui {

// Stateless with respect to the QML document: TextArea remains the sole text
// owner and applies the one replacement transaction returned for each event.
class QmlEditorController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool halfWidthInputEnabled READ halfWidthInputEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool overwriteMode READ overwriteMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoCompletionEnabled READ autoCompletionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QString wholeBpm READ wholeBpm NOTIFY settingsChanged)
    Q_PROPERTY(bool completionActive READ completionActive NOTIFY completionChanged)
    Q_PROPERTY(QStringList completionCandidates READ completionCandidates NOTIFY completionChanged)
    Q_PROPERTY(int completionIndex READ completionIndex NOTIFY completionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoAvailabilityChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoAvailabilityChanged)

public:
    explicit QmlEditorController(QObject* parent = nullptr);

    bool halfWidthInputEnabled() const;
    bool overwriteMode() const;
    bool autoCompletionEnabled() const;
    QString wholeBpm() const;
    bool completionActive() const;
    QStringList completionCandidates() const;
    int completionIndex() const;
    bool canUndo() const;
    bool canRedo() const;

    void setHalfWidthInputEnabled(bool enabled);
    void setOverwriteMode(bool enabled);
    void setAutoCompletionEnabled(bool enabled);
    void setWholeBpm(const QString& bpm);

    miacode::editor::SimaiTextEditResult processKey(
        const QString& text, int anchor, int position, const QString& input, int key, int modifiers);
    miacode::editor::SimaiTextEditResult processImeCommit(
        const QString& text, int anchor, int position, const QString& committedText);
    miacode::editor::SimaiTextEditResult processPaste(
        const QString& text, int anchor, int position, const QString& pastedText);
    miacode::editor::SimaiTextEditResult acceptCompletion(const QString& text, int anchor, int position);

    Q_INVOKABLE QVariantMap processKeyForQml(
        const QString& text, int anchor, int position, const QString& input, int key, int modifiers);
    Q_INVOKABLE QVariantMap processImeCommitForQml(
        const QString& text, int anchor, int position, const QString& committedText);
    Q_INVOKABLE QVariantMap processPasteForQml(
        const QString& text, int anchor, int position, const QString& pastedText);
    Q_INVOKABLE QVariantMap acceptCompletionForQml(const QString& text, int anchor, int position);
    Q_INVOKABLE void moveCompletionSelection(int delta);
    Q_INVOKABLE void selectCompletionIndex(int index);
    Q_INVOKABLE void closeCompletion();
    Q_INVOKABLE void setUndoAvailability(bool canUndo, bool canRedo);
    Q_INVOKABLE QString clipboardText() const;

signals:
    void settingsChanged();
    void completionChanged();
    void undoAvailabilityChanged();

private:
    miacode::editor::SimaiTextEditResult process(const miacode::editor::SimaiTextEditRequest& request);
    void setCompletion(const miacode::editor::SimaiCompletionSession& completion);
    void filterCompletion(const QString& text, int position);
    QVariantMap toQmlTransaction(const miacode::editor::SimaiTextEditResult& result) const;

    bool halfWidthInputEnabled_ = true;
    bool overwriteMode_ = false;
    bool autoCompletionEnabled_ = true;
    QString wholeBpm_;
    miacode::editor::SimaiCompletionSession completion_;
    QStringList visibleCandidates_;
    int completionIndex_ = -1;
    bool canUndo_ = false;
    bool canRedo_ = false;
};

} // namespace miacode::qml_ui
