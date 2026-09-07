// Touch-pad authoring edits: where a pad lands in a simai token, and what a
// second click on the same pad removes. Split out of the Widgets editor's spec
// when that editor was deleted — the planner and the applier are plain text and
// QTextDocument work with no widget in them, and the QML editor drives the same
// pair through QmlEditorController::touchPadAuthoringForQml.

#include "editor/TouchPadAuthoringEdit.h"

#include <QCoreApplication>
#include <QTextCursor>
#include <QTextDocument>
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

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTextStream out(stdout);
    int failed = 0;

    const auto expectTouchPlan = [&out, &failed](const QString& text, int pos, bool backtick,
                                                 int expectedStart, int expectedInsert,
                                                 const QString& expectedText, const QString& message) {
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            text, pos, QStringLiteral("A1"), backtick ? QLatin1Char('`') : QLatin1Char('/'));
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

    {
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            QStringLiteral("1,2,"), 3, QStringLiteral("A1"), QLatin1Char(','));
        expect(plan.valid && plan.insertionPosition == 3
                   && plan.insertionText == QStringLiteral(",A1"),
               QStringLiteral("comma separator appends even when the beat is empty"), out, &failed);
    }

    const auto expectTouchPadEdit = [&out, &failed](const QString& text, int pos, bool backtick,
                                                    const QString& pad, const QString& expected,
                                                    const QString& message) {
        QTextDocument document(text);
        QTextCursor cursor(&document);
        cursor.setPosition(pos);
        const auto plan = miacode::editor::planTouchPadAuthoringEdit(
            document.toPlainText(), cursor.position(), pad,
            backtick ? QLatin1Char('`') : QLatin1Char('/'));
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
            document.toPlainText(), cursor.position(), QStringLiteral("B2"), QLatin1Char('/'));
        expect(miacode::editor::applyTouchPadAuthoringEdit(&document, &cursor, plan)
                   && document.toPlainText() == QLatin1String("1,2/B2,"),
               QStringLiteral("active selection uses position and does not delete selected text"), out, &failed);
        document.undo();
        expect(document.toPlainText() == QLatin1String("1,2,"),
               QStringLiteral("touch authoring insertion is one undo step"), out, &failed);
    }

    if (failed != 0) {
        out << "TouchPadAuthoringEdit spec failed: " << failed << '\n';
        return 1;
    }

    out << "TouchPadAuthoringEdit spec passed.\n";
    return 0;
}
