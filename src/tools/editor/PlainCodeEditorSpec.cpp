#include "editor/PlainCodeEditor.h"
#include "editor/BracketCompletionPopup.h"

#include <QApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTextStream>

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

            expect(
                completionEditor.toPlainText() == QStringLiteral("[8:1]"),
                QStringLiteral("mouse press on completion candidate commits clicked row after editor focus-out"),
                out,
                &failed);
        }
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
