#pragma once

#include "editor/SimaiTextEditPolicy.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QVector>

namespace miacode::qml_ui {

// Stateless with respect to the QML document: TextArea remains the sole text
// owner and applies the one replacement transaction returned for each event.
class QmlEditorController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool halfWidthInputEnabled READ halfWidthInputEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool overwriteMode READ overwriteMode NOTIFY settingsChanged)
    Q_PROPERTY(bool autoCompletionEnabled READ autoCompletionEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool imeInputDisabled READ imeInputDisabled NOTIFY settingsChanged)
    Q_PROPERTY(QString wholeBpm READ wholeBpm NOTIFY settingsChanged)
    Q_PROPERTY(bool completionActive READ completionActive NOTIFY completionChanged)
    Q_PROPERTY(QStringList completionCandidates READ completionCandidates NOTIFY completionChanged)
    Q_PROPERTY(int completionIndex READ completionIndex NOTIFY completionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoAvailabilityChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoAvailabilityChanged)

public:
    struct FindResult {
        bool found = false;
        int start = 0;
        int end = 0;
    };
    explicit QmlEditorController(QObject* parent = nullptr);

    bool halfWidthInputEnabled() const;
    bool overwriteMode() const;
    bool autoCompletionEnabled() const;
    bool imeInputDisabled() const;
    QString wholeBpm() const;
    bool completionActive() const;
    QStringList completionCandidates() const;
    int completionIndex() const;
    bool canUndo() const;
    bool canRedo() const;

    void setHalfWidthInputEnabled(bool enabled);
    void setOverwriteMode(bool enabled);
    void setAutoCompletionEnabled(bool enabled);
    void setImeInputDisabled(bool disabled);
    void setWholeBpm(const QString& bpm);

    miacode::editor::SimaiTextEditResult processKey(
        const QString& text, int anchor, int position, const QString& input, int key, int modifiers);
    miacode::editor::SimaiTextEditResult processImeCommit(
        const QString& text, int anchor, int position, const QString& committedText);
    miacode::editor::SimaiTextEditResult processPaste(
        const QString& text, int anchor, int position, const QString& pastedText);
    miacode::editor::SimaiTextEditResult acceptCompletion(const QString& text, int anchor, int position);
    FindResult find(const QString& text, int anchor, int position, const QString& needle,
                    bool caseSensitive, bool wholeWord, bool backwards) const;
    miacode::editor::SimaiTextEditResult replaceSelection(
        const QString& text, int anchor, int position, const QString& needle,
        const QString& replacement, bool caseSensitive, bool wholeWord) const;
    miacode::editor::SimaiTextEditResult replaceAll(
        const QString& text, const QString& needle, const QString& replacement,
        bool caseSensitive, bool wholeWord) const;
    void setDocumentContext(int difficultyId, quint64 revision);
    bool acceptsCaret(int difficultyId, quint64 revision, bool imeComposing) const;
    bool acceptsTouchAuthoring(int difficultyId, quint64 revision, bool imeComposing,
                               bool editorHasFocus) const;

    Q_INVOKABLE QVariantMap processKeyForQml(
        const QString& text, int anchor, int position, const QString& input, int key, int modifiers);
    Q_INVOKABLE QVariantMap processImeCommitForQml(
        const QString& text, int anchor, int position, const QString& committedText);
    Q_INVOKABLE QVariantMap processPasteForQml(
        const QString& text, int anchor, int position, const QString& pastedText);
    Q_INVOKABLE QVariantMap acceptCompletionForQml(const QString& text, int anchor, int position);
    Q_INVOKABLE QVariantMap findForQml(const QString& text, int anchor, int position,
                                       const QString& needle, bool caseSensitive,
                                       bool wholeWord, bool backwards) const;
    Q_INVOKABLE QVariantMap replaceSelectionForQml(
        const QString& text, int anchor, int position, const QString& needle,
        const QString& replacement, bool caseSensitive, bool wholeWord) const;
    Q_INVOKABLE QVariantMap replaceAllForQml(const QString& text, const QString& needle,
                                             const QString& replacement, bool caseSensitive,
                                             bool wholeWord) const;
    Q_INVOKABLE void setDocumentContextForQml(int difficultyId, qulonglong revision);
    Q_INVOKABLE bool publishCaretForQml(int difficultyId, qulonglong revision, int anchor,
                                        int position, bool imeComposing);
    Q_INVOKABLE bool acceptsTouchAuthoringForQml(int difficultyId, qulonglong revision,
                                                 bool imeComposing, bool editorHasFocus) const;
    Q_INVOKABLE QVariantList bookmarksForQml(const QString& text) const;
    Q_INVOKABLE QVariantMap createBookmarkForQml(const QString& text, int line, const QString& title) const;
    Q_INVOKABLE QVariantMap renameBookmarkForQml(const QString& text, int line, const QString& title) const;
    Q_INVOKABLE QVariantMap deleteBookmarkForQml(const QString& text, int line) const;
    // A preview touch-pad click is still a text transaction.  Returning the
    // exact replacement lets SourceEditor own document mutation and one-step
    // undo, while the token start is used to park preview follow safely.
    Q_INVOKABLE QVariantMap touchPadAuthoringForQml(const QString& text, int anchor,
                                                    int position, const QString& pad,
                                                    bool useBacktickSeparator) const;
    Q_INVOKABLE void resetQmlHistory(const QString& text, int anchor, int position);
    Q_INVOKABLE void recordQmlTransaction(const QString& before, const QString& after,
                                          int beforeAnchor, int beforePosition,
                                          int afterAnchor, int afterPosition);
    Q_INVOKABLE QVariantMap undoQmlTransaction();
    Q_INVOKABLE QVariantMap redoQmlTransaction();
    Q_INVOKABLE void updateCompletionForQml(const QString& text, int position);
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
    // One undo/redo step, expressed as the minimal replacement that turns the
    // document the editor currently holds into the one the step restores.
    QVariantMap restoreTransaction(const QString& current, const QString& restored) const;

    bool halfWidthInputEnabled_ = true;
    bool overwriteMode_ = false;
    bool autoCompletionEnabled_ = true;
    bool imeInputDisabled_ = true;
    QString wholeBpm_;
    miacode::editor::SimaiCompletionSession completion_;
    QStringList visibleCandidates_;
    int completionIndex_ = -1;
    bool canUndo_ = false;
    bool canRedo_ = false;
    int activeDifficultyId_ = -1;
    quint64 documentRevision_ = 0;
    struct QmlUndoEntry {
        QString before;
        QString after;
        int beforeAnchor = 0;
        int beforePosition = 0;
        int afterAnchor = 0;
        int afterPosition = 0;
    };
    QVector<QmlUndoEntry> qmlUndo_;
    QVector<QmlUndoEntry> qmlRedo_;
};

} // namespace miacode::qml_ui
