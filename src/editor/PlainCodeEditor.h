#pragma once

#include <QList>
#include <QSet>
#include <QPointF>
#include <QTextEdit>

class BracketCompletionPopup;
class LineNumberArea;
class QAction;
class QContextMenuEvent;
class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QMimeData;
class QFontMetrics;

namespace miacode::editor {
QChar normalizedHalfWidthChar(QChar ch);
QString normalizedHalfWidthText(QString text);
QString normalizedHalfWidthKeyText(const QKeyEvent* event, const QString& text);
QString clearCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount = nullptr);
QPair<int, int> lineNumberUnderlineHorizontalBounds(
    const QString& number,
    const QFontMetrics& metrics,
    int textRight);
}

class PlainCodeEditor : public QTextEdit
{
    Q_OBJECT
    friend class LineNumberArea;

public:
    explicit PlainCodeEditor(QWidget* parent = nullptr);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void setBlockSpacingPixels(int px);
    void setTopOverlayInsetPixels(int px);
    void refreshLineNumberAreaLayout();
    void setBatchTransformActions(const QList<QAction*>& actions);
    void setMoreBatchTransformActions(const QList<QAction*>& actions);
    void setPreviewFollowVisualCaret(bool active, int line = 1, int col = 1);
    void setBookmarkedLines(const QSet<int>& lines);
    const QSet<int>& bookmarkedLines() const { return bookmarkedLines_; }
    int lineNumberAtGlobalPosition(const QPoint& globalPos) const;
    void setBookmarkDropPreviewLine(int line);
    bool applyPreviewFollowCursor(const QTextCursor& cursor, bool centerView, bool suppressSignals = true);
    QPointF normalizedViewportHitPosition(const QPointF& position) const;
    void setHalfWidthInputEnabled(bool enabled);
    bool halfWidthInputEnabled() const { return halfWidthInputEnabled_; }
    // beta51+ — hard-disable the platform IME on this editor when the user
    // turns on the "禁止中文輸入法輸入" preference. Sets Qt::WA_InputMethodEnabled
    // = !disabled so the OS routes raw key events directly to the editor
    // (Chinese / Japanese / Korean / Vietnamese IMEs all get bypassed,
    // regardless of whether they honour Qt::ImhLatinOnly). halfWidth's
    // ImhLatinOnly hint is a *suggestion* most CJK IMEs ignore on Windows;
    // this attribute is the OS-level kill-switch.
    void setImeInputDisabled(bool disabled);
    bool imeInputDisabled() const { return imeInputDisabled_; }
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void setEditorOverwriteMode(bool enabled);
    // Auto-completion — a single preference that bundles every smart-typing
    // affordance in the chart editor (controlled via the editor section of the
    // Preferences dialog; default on). When enabled:
    //   * typing { [ ( auto-inserts the matching close and parks the caret
    //     between the pair, replacing any selected text with the empty pair
    //     instead of surrounding it (with type-over and empty-pair backspace to match);
    //   * opening a bracket pops a simai-aware suggestion list (durations /
    //     subdivisions / BPMs) anchored under the caret, non-blocking;
    //   * typing the simai hold letter 'h' offers the full "[8:1]"-style
    //     hold-duration tokens as suggestions (it inserts no bracket itself).
    // Previously these were three separate toggles; they are unified here.
    void setAutoCompletionEnabled(bool enabled);
    bool autoCompletionEnabled() const { return autoCompletionEnabled_; }
    // Feeds the '(' suggestion list. The chart body editor never holds the
    // &wholebpm metadata line, so the owning window pushes it in on load /
    // difficulty switch (see MainWindow::DocumentSection::setEditorText).
    void setWholeBpmCandidate(const QString& bpm);

signals:
    void undoShortcutRequested();
    void redoShortcutRequested();
    void selectionReplacementAboutToEdit(int anchor, int position);
    void clearCompleteElementsShortcutRequested();
    void raiseSubdivisionHalfStepShortcutRequested();
    void lowerSubdivisionHalfStepShortcutRequested();
    void editorOverwriteModeChanged(bool enabled);
    void lineNumberBookmarkMoveRequested(int fromLine, int toLine);
    void lineNumberBookmarkActivated(int line);
    void lineNumberBookmarkCreateRequested(int line);
    // Bookmark redesign — the editor only announces intent; MainWindow owns
    // the actual actions (sidebar inline rename, delete confirmation, and the
    // line-number-gutter context menu).
    void lineNumberBookmarkRenameRequested(int line);
    void lineNumberBookmarkDeleteRequested(int line);
    void lineNumberBookmarkContextMenuRequested(int line, const QPoint& globalPos);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea();

private:
    QRect currentLineHighlightRect() const;
    QRect previewFollowVisualCaretRect() const;
    int lineNumberAtAreaPosition(const QPoint& pos) const;
    void syncCursorVisualState();
    void updateCursorVisibility();
    void updateCurrentLineHighlightRegion(const QRect& previousRect, const QRect& currentRect);
    // Bracket auto-pairing helpers. Each returns true when it consumed the input
    // and performed the insertion. tryAutoCloseBracket is shared by both the
    // keyPressEvent and the IME commit (inputMethodEvent) paths.
    bool tryAutoCloseBracket(const QString& text);
    // Bracket-completion helpers. tryBracketInput() wraps the auto-close path so
    // the suggestion popup opens on the same (normalized) key/IME bracket input.
    bool tryBracketInput(const QString& text);
    // Typing '[' immediately before an existing '[' steps into the existing
    // duration slot instead of inserting a duplicate bracket pair.
    bool tryOverwriteOpeningSquareBracket(const QString& text);
    // simai hold shortcut — typing 'h' inserts a bare 'h' and offers the
    // full-bracket hold-duration tokens ("[8:1]" …) as a completion popup. It
    // inserts no bracket itself, so a following '[' yields the normal "h[]"
    // (not the old "h[[]]"). keyPress-only: CJK IMEs commit stray latin letters
    // mid-composition, so the IME path must not trigger this.
    bool tryHoldExpand(const QString& text);
    // Backspace between an empty matching pair (e.g. `[|]`) removes both glyphs
    // in one undo step. Mirrors auto-close — gated by the same preference.
    bool tryDeleteBracketPair(QKeyEvent* event);
    // Typing a closing bracket when the very same glyph already sits to the right
    // of the caret steps over it instead of inserting a duplicate. The natural
    // counterpart to auto-close (you type the `]` you "see") — gated by the same
    // preference. Shared by the keyPressEvent and IME commit paths.
    bool tryOverwriteClosingBracket(const QString& text);
    void ensureCompletionPopup();
    // Shared popup-opening core: anchors the suggestion list under the caret and
    // records the slot state. opening is the glyph whose matching close governs
    // the filter's "left the slot" check; closingPresent says whether an
    // auto-inserted close sits to the right (so accept swallows it).
    void openCompletionPopup(const QStringList& candidates, QChar opening, bool closingPresent);
    void maybeOpenBracketCompletion(QChar opening, bool closingPresent);
    // Opens the 'h' hold-duration suggestions ("[8:1]" …) anchored after the
    // just-typed 'h'. No bracket is inserted, so closingPresent is false.
    void maybeOpenHoldCompletion();
    bool bracketCompletionActive() const;
    bool handleCompletionPopupKey(QKeyEvent* event);
    void acceptCompletionCandidate();
    void acceptCompletionCandidate(const QString& candidate);
    void updateBracketCompletionFilter();
    void closeBracketCompletion();

    int blockSpacingPixels_ = 0;
    int topOverlayInsetPixels_ = 0;
    bool halfWidthInputEnabled_ = true;
    bool imeInputDisabled_ = false;
    bool autoCompletionEnabled_ = true;
    // Live state for the bracket-completion popup. completionOpening_ is null
    // when no popup is active; completionStartPos_ marks the document position
    // right after the opening bracket (where the user's filter text begins);
    // completionClosingPresent_ records whether auto-close inserted a closing
    // glyph to the right (so accepting a candidate consumes it instead of
    // duplicating it); suppressCompletionFilter_ guards programmatic edits from
    // the cursorPositionChanged re-filter slot.
    QString wholeBpmCandidate_;
    BracketCompletionPopup* completionPopup_ = nullptr;
    QChar completionOpening_;
    bool completionClosingPresent_ = false;
    int completionStartPos_ = -1;
    bool suppressCompletionFilter_ = false;
    QList<QAction*> batchTransformActions_;
    QList<QAction*> moreBatchTransformActions_;
    QRect lastCurrentLineHighlightRect_;
    bool previewFollowVisualCaretActive_ = false;
    int previewFollowVisualCaretLine_ = 1;
    int previewFollowVisualCaretCol_ = 1;
    QSet<int> bookmarkedLines_;
    int hoveredBookmarkDropLine_ = -1;
    int pressedBookmarkLine_ = -1;
    QPoint lineNumberPressPos_;
    LineNumberArea* lineNumberArea_;
};
