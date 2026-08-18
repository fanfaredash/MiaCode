#include "editor/PlainCodeEditor.h"
#include "editor/BracketCompletionPopup.h"
#include "editor/BookmarkCommentSyntax.h"
#include "editor/TouchPadAuthoringEdit.h"
#include "common/AdoptedSurfaceDragAutoScroll.h"
#include "common/AdoptedWidgetCoordinates.h"

#include <QApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextStream>
#include <QWindow>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    if (condition) {
        out << "[PASS] " << message << '\n';
        return true;
    }
    out << "[FAIL] " << message << '\n';
    if (failed != nullptr) {
        ++(*failed);
    }
    return false;
}

void expectClearCompleteElements(
    const QString& input,
    int selectionStart,
    int selectionEnd,
    const QString& expected,
    int expectedChanged,
    const QString& message,
    QTextStream& out,
    int* failed)
{
    int changed = -1;
    const QString actual = miacode::editor::clearCompleteElementsInSelection(
        input,
        selectionStart,
        selectionEnd,
        &changed);
    expect(
        actual == expected && changed == expectedChanged,
        QStringLiteral("%1 (actual='%2', changed=%3)")
            .arg(message, actual)
            .arg(changed),
        out,
        failed);
}

void expectClearCompleteElementsReplacement(
    const QString& input,
    int selectionStart,
    int selectionEnd,
    const QString& expectedReplacement,
    const QString& message,
    QTextStream& out,
    int* failed)
{
    int changed = -1;
    const QString transformedFull = miacode::editor::clearCompleteElementsInSelection(
        input,
        selectionStart,
        selectionEnd,
        &changed);
    const int unchangedSuffixLength = input.size() - selectionEnd;
    const int transformedSelectionEnd = transformedFull.size() - unchangedSuffixLength;
    const QString replacement = transformedFull.mid(selectionStart, transformedSelectionEnd - selectionStart);
    expect(
        replacement == expectedReplacement,
        QStringLiteral("%1 (replacement='%2', changed=%3)")
            .arg(message, replacement)
            .arg(changed),
        out,
        failed);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    Q_UNUSED(app);

    QTextStream out(stdout);
    int failed = 0;

    {
        QWidget adoptedSurface;
        QWidget nestedWidget(&adoptedSurface);
        nestedWidget.move(17, 29);
        auto* adoptedWindow = new QWindow;
        const QPoint localPoint(5, 7);
        miacode::ui::bindAdoptedSurfaceWindow(&adoptedSurface, adoptedWindow);
        const auto route = miacode::ui::adoptedWidgetCoordinateRoute(&nestedWidget, localPoint);
        expect(route.window == adoptedWindow
                   && route.surfacePoint == QPoint(22, 36),
               QStringLiteral("adopted widget coordinates resolve through the bridge surface"),
               out,
               &failed);
        expect(
            miacode::ui::mapGlobalPointToWidget(
                &nestedWidget,
                adoptedWindow->mapToGlobal(route.surfacePoint)) == localPoint,
            QStringLiteral("adopted global coordinates resolve back into the nested widget"),
            out,
            &failed);
        delete adoptedWindow;
        expect(miacode::ui::adoptedWidgetCoordinateRoute(&nestedWidget, localPoint).window == nullptr,
               QStringLiteral("destroying the adopted window clears the bridge coordinate route"),
               out,
               &failed);
    }

    {
        // Drag-selection autoscroll geometry (the brain of the macOS takeover —
        // no scroll area on an adopted surface may let Qt re-derive the held
        // pointer from QCursor::pos()).
        const QRect viewportRect(0, 0, 400, 300);
        const auto inside = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(120, 90));
        expect(inside.intervalMs == 0
                   && inside.horizontalStep == 0
                   && inside.verticalStep == 0
                   && inside.clampedPosition == QPoint(120, 90),
               QStringLiteral("pointer inside the viewport plans no autoscroll"),
               out,
               &failed);

        const auto gutter = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(-30, 90));
        expect(gutter.clampedPosition == QPoint(0, 90)
                   && gutter.horizontalStep == -1
                   && gutter.verticalStep == 0
                   && gutter.intervalMs >= 16 && gutter.intervalMs <= 100,
               QStringLiteral("pointer over the line-number gutter clamps back and scrolls left"),
               out,
               &failed);

        const auto belowRight =
            miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(480, 360));
        expect(belowRight.clampedPosition == QPoint(399, 299)
                   && belowRight.horizontalStep == 1
                   && belowRight.verticalStep == 1,
               QStringLiteral("pointer past the bottom-right corner scrolls on both axes"),
               out,
               &failed);

        const auto nudge = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(0, -2));
        const auto lunge = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(0, -200));
        expect(nudge.intervalMs == 100 && lunge.intervalMs == 16
                   && nudge.verticalStep == -1 && lunge.verticalStep == -1,
               QStringLiteral("autoscroll cadence accelerates with the overshoot, floored at a frame"),
               out,
               &failed);

        expect(miacode::ui::planDragAutoScrollStep(QRect(), QPoint(-30, 90)).intervalMs == 0,
               QStringLiteral("an invalid viewport rect plans no autoscroll"),
               out,
               &failed);
    }

    const auto expectTouchPlan = [&out, &failed](const QString& text, int pos, bool backtick,
                                                 int expectedStart, int expectedInsert,
                                                 const QString& expectedText, const QString& message) {
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(text, pos, QStringLiteral("A1"), backtick);
        expect(plan.valid && plan.tokenStart == expectedStart && plan.insertionPosition == expectedInsert
                   && plan.insertionText == expectedText,
               message, out, &failed);
    };
    expectTouchPlan(QStringLiteral(",,"), 1, false, 1, 1, QStringLiteral("A1"),
                    QStringLiteral("empty comma token inserts pad directly"));
    expectTouchPlan(QStringLiteral(",  ,"), 2, false, 1, 1, QStringLiteral("A1"),
                    QStringLiteral("whitespace-only token inserts before preserved whitespace"));
    expectTouchPlan(QStringLiteral("1,2  ,3"), 3, false, 2, 3, QStringLiteral("/A1"),
                    QStringLiteral("non-empty token appends slash before trailing whitespace"));
    expectTouchPlan(QStringLiteral("1,2,"), 3, true, 2, 3, QStringLiteral("`A1"),
                    QStringLiteral("right-click authoring appends backtick"));
    expectTouchPlan(QStringLiteral("1,2,"), 1, false, 0, 1, QStringLiteral("/A1"),
                    QStringLiteral("caret immediately before comma belongs to left token"));
    expectTouchPlan(QStringLiteral("1,2,"), 2, false, 2, 3, QStringLiteral("/A1"),
                    QStringLiteral("caret immediately after comma belongs to right token"));
    expectTouchPlan(QString(), 0, false, 0, 0, QStringLiteral("A1"),
                    QStringLiteral("empty document boundary inserts directly"));
    expectTouchPlan(QStringLiteral("1,2,3"), 0, false, 0, 1, QStringLiteral("/A1"),
                    QStringLiteral("document-start caret stays in the first comma token"));
    expectTouchPlan(QStringLiteral("1,2"), 3, false, 2, 3, QStringLiteral("/A1"),
                    QStringLiteral("document-end caret appends to the final token"));

    const auto expectTouchPadEdit = [&out, &failed](const QString& text, int pos, bool backtick,
                                                    const QString& pad, const QString& expected,
                                                    const QString& message) {
        QTextDocument document(text);
        QTextCursor cursor(&document);
        cursor.setPosition(pos);
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            document.toPlainText(), cursor.position(), pad, backtick);
        const bool applied = miacode::editor::applyTouchPadAuthoringEdit(&document, &cursor, plan);
        expect(applied && document.toPlainText() == expected, message, out, &failed);
    };
    const auto expectTouchEdit = [&expectTouchPadEdit](const QString& text, int pos, bool backtick,
                                                       const QString& expected, const QString& message) {
        expectTouchPadEdit(text, pos, backtick, QStringLiteral("A1"), expected, message);
    };
    expectTouchEdit(QStringLiteral("(160){16}"), 0, false, QStringLiteral("(160){16} A1"),
                    QStringLiteral("timing-only token receives a pad without a touch separator"));
    expectTouchEdit(QStringLiteral("{16}"), 0, false, QStringLiteral("{16} A1"),
                    QStringLiteral("meter-only token receives a pad without a touch separator"));
    expectTouchEdit(QStringLiteral("<HS*1.5>"), 0, false, QStringLiteral("<HS*1.5> A1"),
                    QStringLiteral("HS-only token receives a pad without a touch separator"));
    expectTouchEdit(QStringLiteral("(160){16} || lead-in"), 0, false,
                    QStringLiteral("(160){16} A1 || lead-in"),
                    QStringLiteral("comment-only token content does not require a touch separator"));
    expectTouchEdit(QStringLiteral("1/A1/B2"), 2, false, QStringLiteral("1/B2"),
                    QStringLiteral("existing middle pad removes its preceding separator"));
    expectTouchEdit(QStringLiteral("A1/B2"), 0, false, QStringLiteral("B2"),
                    QStringLiteral("existing first pad removes its following separator"));
    expectTouchEdit(QStringLiteral("A1"), 0, true, QString(),
                    QStringLiteral("existing sole pad is removed regardless of mouse separator"));
    expectTouchEdit(QStringLiteral("1`A1/B2"), 2, false, QStringLiteral("1/B2"),
                    QStringLiteral("mixed separators remove the separator before the matched pad"));
    expectTouchEdit(QStringLiteral("1/A1/A1"), 2, false, QStringLiteral("1/A1"),
                    QStringLiteral("duplicate pad toggle removes only the first match"));
    expectTouchEdit(QStringLiteral("(120){4}A1"), 0, false, QStringLiteral("(120){4}"),
                    QStringLiteral("sole first pad removal preserves timing controls"));
    expectTouchEdit(QStringLiteral("(120){4}A1/B2"), 0, false, QStringLiteral("(120){4}B2"),
                    QStringLiteral("first pad removal keeps timing controls on the next item"));
    expectTouchEdit(QStringLiteral("A10"), 0, false, QStringLiteral("A10/A1"),
                    QStringLiteral("prefix-like area number is not an exact pad match"));
    expectTouchEdit(QStringLiteral("A1h[4:1]"), 0, false, QStringLiteral("A1h[4:1]/A1"),
                    QStringLiteral("touch hold is not removed as an ordinary touch"));
    expectTouchEdit(QStringLiteral("A1f"), 0, false, QString(),
                    QStringLiteral("firework touch toggles as its base pad"));
    expectTouchEdit(QStringLiteral("A1f/B2"), 0, false, QStringLiteral("B2"),
                    QStringLiteral("firework first pad removal keeps the next item"));
    expectTouchEdit(QStringLiteral("1/A1  "), 2, false, QStringLiteral("1  "),
                    QStringLiteral("toggle deletion preserves trailing token whitespace"));
    expectTouchEdit(QStringLiteral(" A1/B2"), 0, false, QStringLiteral(" B2"),
                    QStringLiteral("first pad match preserves leading token whitespace"));
    expectTouchEdit(QStringLiteral("(120){4} A1/B2"), 0, false, QStringLiteral("(120){4} B2"),
                    QStringLiteral("first pad match allows whitespace after timing controls"));
    expectTouchEdit(QStringLiteral(" (120) {4}A1/B2"), 0, false, QStringLiteral(" (120) {4}B2"),
                    QStringLiteral("first pad match allows whitespace around timing controls"));
    expectTouchEdit(QStringLiteral("1/ A1 /B2"), 2, false, QStringLiteral("1/B2"),
                    QStringLiteral("non-first pad match ignores item whitespace"));

    // A `||` comment runs to the end of ITS line: commas inside it are prose,
    // and chart text after the terminating newline is still the same token.
    expectTouchPlan(QStringLiteral("1,2, ||note,here\n3,5,"), 17, false, 4, 18, QStringLiteral("/A1"),
                    QStringLiteral("a comma inside a comment is not a beat separator"));
    expectTouchEdit(QStringLiteral("1,2, ||note,here\n,5,"), 17, false,
                    QStringLiteral("1,2, ||note,here\nA1,5,"),
                    QStringLiteral("a pad is never authored into a comment"));
    expectTouchEdit(QStringLiteral("1,2,3, ||a,b"), 12, false, QStringLiteral("1,2,3,A1 ||a,b"),
                    QStringLiteral("end-of-text caret after a comment stays in the chart token"));
    expectTouchEdit(QStringLiteral("1,2,3, ||8,16\n4,5,6,"), 14, false,
                    QStringLiteral("1,2,3, ||8,16\n4/A1,5,6,"),
                    QStringLiteral("a numeric comment does not shift the token"));
    expectTouchEdit(QStringLiteral("1, ||lead in\n2,"), 13, false, QStringLiteral("1, ||lead in\n2/A1,"),
                    QStringLiteral("content after a comment belongs to the token"));
    expectTouchPadEdit(QStringLiteral("1, ||x\nA1,"), 9, false, QStringLiteral("A1"),
                       QStringLiteral("1, ||x\n,"),
                       QStringLiteral("a pad living after a comment toggles off"));
    expectTouchPadEdit(QStringLiteral("1, ||x\nA1/B2,"), 9, false, QStringLiteral("B2"),
                       QStringLiteral("1, ||x\nA1,"),
                       QStringLiteral("the second pad after a comment toggles off"));
    expectTouchPadEdit(QStringLiteral("1,2, ||x\n3/A1,"), 12, false, QStringLiteral("A1"),
                       QStringLiteral("1,2, ||x\n3,"),
                       QStringLiteral("a pad after a mid-token comment toggles off instead of duplicating"));

    // An empty token that reaches a line break belongs to the LAST line it
    // covers, not to the trailing edge of the previous one.
    expectTouchEdit(QStringLiteral("1,2,3,4,\n,6,7,8,"), 9, false, QStringLiteral("1,2,3,4,\nA1,6,7,8,"),
                    QStringLiteral("empty first beat of a line keeps its pad on that line"));
    expectTouchEdit(QStringLiteral("(120){8}\n,2,3,4,"), 9, false, QStringLiteral("(120){8}\nA1,2,3,4,"),
                    QStringLiteral("empty first beat under a controls-only line stays on the note line"));
    expectTouchEdit(QStringLiteral("1,2,3,4, ||measure 1\n,6,7,8,"), 21, false,
                    QStringLiteral("1,2,3,4, ||measure 1\nA1,6,7,8,"),
                    QStringLiteral("empty first beat after a commented line stays on the note line"));
    expectTouchEdit(QStringLiteral("1,2,\n\n,5,"), 6, false, QStringLiteral("1,2,\n\nA1,5,"),
                    QStringLiteral("a multi-line empty token uses its last line"));
    expectTouchEdit(QStringLiteral("(120)\n{16},,,,"), 6, false, QStringLiteral("(120)\n{16} A1,,,,"),
                    QStringLiteral("controls opening the token's last line still precede the pad"));
    expectTouchEdit(QStringLiteral("(120)\n{16} ,,,,"), 6, false, QStringLiteral("(120)\n{16} A1 ,,,,"),
                    QStringLiteral("controls on the last line precede the pad, trailing space kept"));
    expectTouchEdit(QStringLiteral("1,2,\n(180){16},3,"), 5, false,
                    QStringLiteral("1,2,\n(180){16} A1,3,"),
                    QStringLiteral("bpm+meter opening the last line still precede the pad"));

    // NOTE: `findWhitespaceSeparatedNote` lets `A1 B2` toggle each half on its
    // own, because both of MiaCode's parsers flush the pending token on
    // whitespace. That is deliberately NOT asserted here: plain simai does not
    // define whitespace as an each separator, so the behaviour tracks our
    // parsers rather than a guaranteed language rule and must not be frozen
    // into the spec.

    {
        QTextDocument document(QStringLiteral("1,2,"));
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.setPosition(3, QTextCursor::KeepAnchor);
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            document.toPlainText(), cursor.position(), QStringLiteral("B2"), false);
        expect(miacode::editor::applyTouchPadAuthoringEdit(&document, &cursor, plan)
                   && document.toPlainText() == QLatin1String("1,2/B2,"),
               QStringLiteral("active selection uses position and does not delete selected text"), out, &failed);
        document.undo();
        expect(document.toPlainText() == QLatin1String("1,2,"),
               QStringLiteral("touch authoring insertion is one undo step"), out, &failed);
    }

    {
        QTextDocument document(QStringLiteral("1/A1/B2"));
        QTextCursor cursor(&document);
        cursor.setPosition(2);
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            document.toPlainText(), cursor.position(), QStringLiteral("A1"), false);
        expect(miacode::editor::applyTouchPadAuthoringEdit(&document, &cursor, plan)
                   && document.toPlainText() == QLatin1String("1/B2"),
               QStringLiteral("touch authoring deletion applies successfully"), out, &failed);
        document.undo();
        expect(document.toPlainText() == QLatin1String("1/A1/B2"),
               QStringLiteral("touch authoring deletion is one undo step"), out, &failed);
    }

    expect(
        miacode::editor::isBookmarkCommentMarker(QStringLiteral("1 || note"), 2),
        QStringLiteral("ordinary double-pipe comment starts a bookmark"),
        out,
        &failed);
    expect(
        !miacode::editor::isBookmarkCommentMarker(QStringLiteral("1 ||| annotation"), 2),
        QStringLiteral("triple-pipe annotation does not start a bookmark"),
        out,
        &failed);

    expect(
        miacode::editor::normalizedHalfWidthText(QStringLiteral("、")) == QLatin1String("/"),
        QStringLiteral("ideographic comma normalizes to slash each separator"),
        out,
        &failed);
    expect(
        miacode::editor::normalizedHalfWidthText(QStringLiteral("，")) == QLatin1String(","),
        QStringLiteral("full-width comma still normalizes to comma beat separator"),
        out,
        &failed);
    expect(
        miacode::editor::normalizedHalfWidthText(QStringLiteral("／＃：")) == QLatin1String("/#:"),
        QStringLiteral("full-width slash, hash, and colon keep half-width conversion"),
        out,
        &failed);

    QKeyEvent shiftSix(QEvent::KeyPress, Qt::Key_6, Qt::ShiftModifier, QStringLiteral("^"));
    expect(
        miacode::editor::normalizedHalfWidthKeyText(&shiftSix, QStringLiteral("＾")) == QLatin1String("^"),
        QStringLiteral("shift-6 key path still forces caret"),
        out,
        &failed);

    const QString measure = QStringLiteral("{16}1,1,1,1-3[9:1],");
    expectClearCompleteElements(
        measure,
        0,
        measure.size(),
        QStringLiteral("{16},,,,"),
        4,
        QStringLiteral("Ctrl+Q clears every complete selected element and keeps subdivision prefix"),
        out,
        &failed);
    expectClearCompleteElements(
        measure,
        0,
        QStringLiteral("{16}1,1,1").size(),
        QStringLiteral("{16},,1,1-3[9:1],"),
        2,
        QStringLiteral("Ctrl+Q leaves an incomplete selected trailing element unchanged"),
        out,
        &failed);
    expectClearCompleteElements(
        measure,
        0,
        QStringLiteral("{16}1,1,1,1-3[9:").size(),
        QStringLiteral("{16},,,1-3[9:1],"),
        3,
        QStringLiteral("Ctrl+Q does not clear an incomplete bracketed selected element"),
        out,
        &failed);
    expectClearCompleteElements(
        QStringLiteral("1,1"),
        0,
        QStringLiteral("1,1").size(),
        QStringLiteral(",1"),
        1,
        QStringLiteral("Ctrl+Q requires element plus comma, so final unterminated element stays"),
        out,
        &failed);
    expectClearCompleteElements(
        QStringLiteral("1,|| 2,3,\n4,"),
        0,
        QStringLiteral("1,|| 2,3,\n4,").size(),
        QStringLiteral(",|| 2,3,\n,"),
        2,
        QStringLiteral("Ctrl+Q skips comment text for now"),
        out,
        &failed);
    {
        const QString compound = QStringLiteral("4^6[8:1],,,3h[16:3],");
        const int start = compound.indexOf(QStringLiteral("6[8:1]"));
        expectClearCompleteElements(
            compound,
            start,
            compound.size(),
            QStringLiteral("4^6[8:1],,,,"),
            1,
            QStringLiteral("Ctrl+Q does not clear from the middle of a compound element"),
            out,
            &failed);
        expectClearCompleteElementsReplacement(
            compound,
            start,
            compound.size(),
            QStringLiteral("6[8:1],,,,"),
            QStringLiteral("Ctrl+Q document-aware replacement keeps the unselected compound prefix"),
            out,
            &failed);
    }
    {
        const QString prefixed = QStringLiteral("{16}3,4,3,5h[16:5],");
        const int start = prefixed.indexOf(QStringLiteral("6}3"));
        expectClearCompleteElements(
            prefixed,
            start,
            prefixed.size(),
            QStringLiteral("{16},,,,"),
            4,
            QStringLiteral("Ctrl+Q preserves a partially-selected subdivision prefix"),
            out,
            &failed);
        expectClearCompleteElementsReplacement(
            prefixed,
            start,
            prefixed.size(),
            QStringLiteral("6},,,,"),
            QStringLiteral("Ctrl+Q document-aware replacement keeps the unselected subdivision prefix"),
            out,
            &failed);
    }
    {
        // A passage spanning several {} subdivisions must keep every directive
        // and line break — clearing must NOT collapse it into one {} (the bug).
        const QString multi = QStringLiteral("{8}1,2,\n{16}3,4,");
        expectClearCompleteElements(
            multi,
            0,
            multi.size(),
            QStringLiteral("{8},,\n{16},,"),
            4,
            QStringLiteral("Ctrl+Q keeps every subdivision across line breaks"),
            out,
            &failed);
    }
    {
        // A subdivision directive after whitespace (not tight against the prior
        // comma) must still survive the clear.
        const QString spaced = QStringLiteral("{8}1, {16}2,");
        expectClearCompleteElements(
            spaced,
            0,
            spaced.size(),
            QStringLiteral("{8}, {16},"),
            2,
            QStringLiteral("Ctrl+Q keeps a space-preceded subdivision prefix"),
            out,
            &failed);
    }
    {
        // BPM marks and subdivisions both survive across a line break.
        const QString bpm = QStringLiteral("(120){8}1,\n(140){4}2,");
        expectClearCompleteElements(
            bpm,
            0,
            bpm.size(),
            QStringLiteral("(120){8},\n(140){4},"),
            2,
            QStringLiteral("Ctrl+Q keeps BPM marks and subdivisions across line breaks"),
            out,
            &failed);
    }

    PlainCodeEditor editor;
    int clearShortcutCount = 0;
    int raiseHalfShortcutCount = 0;
    int lowerHalfShortcutCount = 0;
    QObject::connect(
        &editor,
        &PlainCodeEditor::clearCompleteElementsShortcutRequested,
        [&clearShortcutCount]() {
            ++clearShortcutCount;
        });
    QObject::connect(
        &editor,
        &PlainCodeEditor::raiseSubdivisionHalfStepShortcutRequested,
        [&raiseHalfShortcutCount]() {
            ++raiseHalfShortcutCount;
        });
    QObject::connect(
        &editor,
        &PlainCodeEditor::lowerSubdivisionHalfStepShortcutRequested,
        [&lowerHalfShortcutCount]() {
            ++lowerHalfShortcutCount;
        });
    QKeyEvent clearKey(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier, QStringLiteral("q"));
    QApplication::sendEvent(&editor, &clearKey);
    expect(
        clearShortcutCount == 1 && clearKey.isAccepted(),
        QStringLiteral("Ctrl+Q key press is forwarded by PlainCodeEditor"),
        out,
        &failed);
    QKeyEvent raiseHalfKey(QEvent::KeyPress, Qt::Key_Equal, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("+"));
    QApplication::sendEvent(&editor, &raiseHalfKey);
    expect(
        raiseHalfShortcutCount == 1 && raiseHalfKey.isAccepted(),
        QStringLiteral("Ctrl+Shift+= key press is forwarded by PlainCodeEditor"),
        out,
        &failed);
    QKeyEvent lowerHalfKey(QEvent::KeyPress, Qt::Key_Minus, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("_"));
    QApplication::sendEvent(&editor, &lowerHalfKey);
    expect(
        lowerHalfShortcutCount == 1 && lowerHalfKey.isAccepted(),
        QStringLiteral("Ctrl+Shift+- key press is forwarded by PlainCodeEditor"),
        out,
        &failed);
    QKeyEvent lowerHalfUnderscoreKey(QEvent::KeyPress, Qt::Key_Underscore, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("_"));
    QApplication::sendEvent(&editor, &lowerHalfUnderscoreKey);
    expect(
        lowerHalfShortcutCount == 2 && lowerHalfUnderscoreKey.isAccepted(),
        QStringLiteral("Ctrl+Shift+_ key press is forwarded as Ctrl+Shift+- by PlainCodeEditor"),
        out,
        &failed);
    {
        PlainCodeEditor gutterEditor;
        gutterEditor.resize(480, 240);
        gutterEditor.setPlainText(QStringLiteral("1,"));
        gutterEditor.show();
        QApplication::processEvents();

        QWidget* gutter = gutterEditor.childAt(QPoint(2, 4));
        QMouseEvent gutterDoubleClick(
            QEvent::MouseButtonDblClick,
            QPointF(2, 4),
            QPointF(2, 4),
            QPointF(gutter != nullptr ? gutter->mapToGlobal(QPoint(2, 4)) : QPoint()),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
        if (gutter != nullptr) {
            QApplication::sendEvent(gutter, &gutterDoubleClick);
        }
        expect(
            gutter != nullptr
                && gutterEditor.metaObject()->indexOfSignal("lineNumberBookmarkCreateRequested(int)") < 0,
            QStringLiteral("double-click bookmark creation is removed from the gutter API"),
            out,
            &failed);
    }
    {
        PlainCodeEditor completionEditor;
        completionEditor.resize(480, 240);
        completionEditor.show();
        completionEditor.setFocus();
        QApplication::processEvents();

        QKeyEvent openBracketKey(QEvent::KeyPress, Qt::Key_BracketLeft, Qt::NoModifier, QStringLiteral("["));
        QApplication::sendEvent(&completionEditor, &openBracketKey);
        QApplication::processEvents();

        auto* popup = completionEditor.findChild<BracketCompletionPopup*>();
        const bool popupReady = popup != nullptr && popup->isVisible() && popup->count() > 0;
        if (expect(
                popupReady,
                QStringLiteral("typing '[' opens a clickable completion popup"),
                out,
                &failed)) {
            const QRect firstItemRect = popup->visualItemRect(popup->item(0));
            const QPoint clickPos = firstItemRect.center();
            const QPoint globalClickPos = popup->viewport()->mapToGlobal(clickPos);
            QCursor::setPos(globalClickPos);

            QFocusEvent focusOut(QEvent::FocusOut, Qt::MouseFocusReason);
            QApplication::sendEvent(&completionEditor, &focusOut);
            QApplication::processEvents();

            QMouseEvent press(
                QEvent::MouseButtonPress,
                QPointF(clickPos),
                QPointF(globalClickPos),
                Qt::LeftButton,
                Qt::LeftButton,
                Qt::NoModifier);
            QApplication::sendEvent(popup->viewport(), &press);
            QApplication::processEvents();

            QMouseEvent release(
                QEvent::MouseButtonRelease,
                QPointF(clickPos),
                QPointF(globalClickPos),
                Qt::LeftButton,
                Qt::NoButton,
                Qt::NoModifier);
            QApplication::sendEvent(popup->viewport(), &release);
            QApplication::processEvents();

            expect(
                completionEditor.toPlainText() == QStringLiteral("[8:1]"),
                QStringLiteral("mouse release on completion candidate commits clicked row after editor focus-out"),
                out,
                &failed);
        }
    }
    {
        const auto expectBracketSelectionReplace = [&](Qt::Key key, const QString& text, const QString& expected, const QString& message) {
            PlainCodeEditor bracketEditor;
            bracketEditor.setPlainText(QStringLiteral("8,selected,8,"));
            QTextCursor cursor = bracketEditor.textCursor();
            const int selectedStart = QStringLiteral("8,").size();
            cursor.setPosition(selectedStart);
            cursor.setPosition(selectedStart + QStringLiteral("selected").size(), QTextCursor::KeepAnchor);
            bracketEditor.setTextCursor(cursor);

            QKeyEvent bracketKey(QEvent::KeyPress, key, Qt::NoModifier, text);
            QApplication::sendEvent(&bracketEditor, &bracketKey);
            expect(
                bracketEditor.toPlainText() == expected
                    && bracketEditor.textCursor().position() == selectedStart + 1
                    && bracketKey.isAccepted(),
                message,
                out,
                &failed);
        };

        expectBracketSelectionReplace(
            Qt::Key_BracketLeft,
            QStringLiteral("["),
            QStringLiteral("8,[],8,"),
            QStringLiteral("typing '[' replaces selected text with [] instead of surrounding it"));
        expectBracketSelectionReplace(
            Qt::Key_BraceLeft,
            QStringLiteral("{"),
            QStringLiteral("8,{},8,"),
            QStringLiteral("typing '{' replaces selected text with {} instead of surrounding it"));
        expectBracketSelectionReplace(
            Qt::Key_ParenLeft,
            QStringLiteral("("),
            QStringLiteral("8,(),8,"),
            QStringLiteral("typing '(' replaces selected text with () instead of surrounding it"));
    }
    {
        PlainCodeEditor bracketEditor;
        bracketEditor.setPlainText(QStringLiteral("8,h[8:1],"));
        QTextCursor cursor = bracketEditor.textCursor();
        const int bracketStart = QStringLiteral("8,h").size();
        cursor.setPosition(bracketStart);
        bracketEditor.setTextCursor(cursor);

        QKeyEvent bracketKey(QEvent::KeyPress, Qt::Key_BracketLeft, Qt::NoModifier, QStringLiteral("["));
        QApplication::sendEvent(&bracketEditor, &bracketKey);
        expect(
            bracketEditor.toPlainText() == QStringLiteral("8,h[8:1],")
                && bracketEditor.textCursor().position() == bracketStart + 1
                && bracketKey.isAccepted(),
            QStringLiteral("typing '[' immediately before an existing '[' steps over it"),
            out,
            &failed);
    }
    {
        PlainCodeEditor holdEditor;
        holdEditor.resize(480, 240);
        holdEditor.show();
        holdEditor.setFocus();
        holdEditor.setPlainText(QStringLiteral("8,8,8,"));
        QTextCursor cursor = holdEditor.textCursor();
        const int selectedStart = QStringLiteral("8,").size();
        cursor.setPosition(selectedStart);
        cursor.setPosition(selectedStart + 1, QTextCursor::KeepAnchor);
        holdEditor.setTextCursor(cursor);
        QApplication::processEvents();

        QKeyEvent holdKey(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
        QApplication::sendEvent(&holdEditor, &holdKey);
        expect(
            holdEditor.toPlainText() == QStringLiteral("8,h,8,") && holdKey.isAccepted(),
            QStringLiteral("typing h replaces selected text instead of wrapping or appending it"),
            out,
            &failed);

        QKeyEvent tabKey(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QString());
        QApplication::sendEvent(&holdEditor, &tabKey);
        expect(
            holdEditor.toPlainText() == QStringLiteral("8,h[8:1],8,") && tabKey.isAccepted(),
            QStringLiteral("typing h on selected text still opens hold-duration completion"),
            out,
            &failed);
    }

    if (failed != 0) {
        out << "PlainCodeEditor spec failed: " << failed << '\n';
        return 1;
    }

    out << "PlainCodeEditor spec passed.\n";
    return 0;
}
