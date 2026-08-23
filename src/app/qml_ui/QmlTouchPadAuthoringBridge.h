#pragma once

#include "editor/TouchPadAuthoringEdit.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QtGlobal>

namespace miacode::qml_ui {

enum class TouchPadAuthoringRoute {
    Reject,
    Qml,
    Legacy,
};

// Once a QML handler exists it owns source editing. A rejected request is
// deliberately a no-op: falling through to the hidden QWidget editor could
// mutate a different, stale document. Classic / QuickShell has no handler and
// therefore keeps its established legacy path.
inline TouchPadAuthoringRoute resolveTouchPadAuthoringRoute(bool qmlHandlerInstalled,
                                                            bool qmlRequestAccepted)
{
    if (qmlHandlerInstalled) {
        return qmlRequestAccepted ? TouchPadAuthoringRoute::Qml
                                  : TouchPadAuthoringRoute::Reject;
    }
    return TouchPadAuthoringRoute::Legacy;
}

// Value-only state supplied by SourceEditor.  Keeping the acceptance decision
// outside MainWindow makes the preview's QML path testable without a widget
// window, while MainWindow still owns the actual runtime gate.
struct QmlTouchPadAuthoringContext {
    int activeDifficultyId = -1;
    int caretDifficultyId = -1;
    quint64 documentRevision = 0;
    quint64 caretRevision = 0;
    bool editorFocused = false;
    bool imeComposing = false;

    bool accepts() const
    {
        return editorFocused && !imeComposing && activeDifficultyId > 0
            && caretDifficultyId == activeDifficultyId
            && caretRevision == documentRevision;
    }
};

// The QML bridge edits a detached QTextDocument, then hands the resulting
// source back to QmlDocumentModel.  This uses the same policy object as the
// widget editor and leaves the caller with the post-edit caret position.
inline bool applyQmlTouchPadAuthoringEdit(QString* text, int* caretPosition,
                                          const QString& pad, bool backtickSeparator)
{
    if (text == nullptr || caretPosition == nullptr || pad.trimmed().isEmpty()) return false;
    QTextDocument document(*text);
    QTextCursor cursor(&document);
    cursor.setPosition(qBound(0, *caretPosition, document.characterCount() - 1));
    const auto plan = miacode::editor::planTouchPadAuthoringEdit(
        document.toPlainText(), cursor.position(), pad, backtickSeparator);
    if (!miacode::editor::applyTouchPadAuthoringEdit(&document, &cursor, plan)) return false;
    *text = document.toPlainText();
    *caretPosition = cursor.position();
    return true;
}

} // namespace miacode::qml_ui
