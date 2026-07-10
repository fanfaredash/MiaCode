#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "common/DebugLog.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"
#include "PlainCodeEditor.Internal.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

#include <utility>

void PlainCodeEditor::setBatchTransformActions(const QList<QAction*>& actions)
{
    batchTransformActions_ = actions;
}

void PlainCodeEditor::setMoreBatchTransformActions(const QList<QAction*>& actions)
{
    moreBatchTransformActions_ = actions;
}

void PlainCodeEditor::setHalfWidthInputEnabled(bool enabled)
{
    halfWidthInputEnabled_ = enabled;
    Qt::InputMethodHints hints = inputMethodHints();
    if (enabled) {
        hints |= Qt::ImhLatinOnly;
    } else {
        hints &= ~Qt::ImhLatinOnly;
    }
    setInputMethodHints(hints);
}

void PlainCodeEditor::setImeInputDisabled(bool disabled)
{
    // The prior beta51 approach only called setAttribute(WA_InputMethodEnabled,
    // false), which had no effect on Windows CJK IMEs. Root cause: QTextEdit
    // reimplements inputMethodQuery() and delegates Qt::ImEnabled to its
    // internal QWidgetTextControl (which reports enabled based on editability),
    // bypassing the WA attribute entirely. The platform queries inputMethodQuery
    // — not the attribute — so the attribute change was a no-op.
    //
    // The fix is to also override inputMethodQuery(Qt::ImEnabled) below.
    // When it returns false, QWindowsInputContext::update() calls
    // ImmAssociateContext(hwnd, NULL) (IMM32) or the TSF equivalent,
    // suppressing the candidate window. The attribute is still set as an
    // additional signal for other Qt machinery.
    imeInputDisabled_ = disabled;
    setAttribute(Qt::WA_InputMethodEnabled, !disabled);
    if (hasFocus()) {
        QGuiApplication::inputMethod()->update(Qt::ImEnabled);
    }
}

QVariant PlainCodeEditor::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (imeInputDisabled_ && query == Qt::ImEnabled) {
        return false;
    }
    return QTextEdit::inputMethodQuery(query);
}

void PlainCodeEditor::setEditorOverwriteMode(bool enabled)
{
    if (overwriteMode() == enabled) {
        return;
    }
    setOverwriteMode(enabled);
    // Refresh the caret width through the shared cursorPositionChanged
    // code path so the mode visual stays consistent whether the toggle
    // came from Insert, the Preferences dialog, or anywhere else.
    updateCursorVisibility();
    viewport()->update();
    emit editorOverwriteModeChanged(enabled);
}

void PlainCodeEditor::setAutoCompletionEnabled(bool enabled)
{
    autoCompletionEnabled_ = enabled;
    if (!enabled) {
        closeBracketCompletion();
    }
}

void PlainCodeEditor::setWholeBpmCandidate(const QString& bpm)
{
    wholeBpmCandidate_ = bpm.trimmed();
}

void PlainCodeEditor::setBackgroundPainter(BackgroundPainter painter)
{
    backgroundPainter_ = std::move(painter);
    if (lineNumberArea_ != nullptr) {
        lineNumberArea_->update();
    }
    viewport()->update();
}
