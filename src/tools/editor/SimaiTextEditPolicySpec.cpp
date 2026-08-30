#include "editor/SimaiTextEditPolicy.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

using miacode::editor::SimaiCompletionSession;
using miacode::editor::SimaiTextEditRequest;
using miacode::editor::SimaiTextEditResult;
using miacode::editor::SimaiTextEditTransaction;

struct ExpectedTransaction {
    QString text;
    int anchor = 0;
    int position = 0;
    bool hasEdit = false;
    bool undoGroup = false;
    int replacementStart = 0;
    int replacementEnd = 0;
    QString replacementText;
    bool insertsBlock = false;
};

struct ExpectedResult {
    bool consumed = false;
    ExpectedTransaction transaction;
    SimaiCompletionSession completion;
};

struct PolicyCase {
    QString label;
    SimaiTextEditRequest request;
    ExpectedResult expected;
};

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    if (condition) {
        out << "[PASS] " << message << '\n';
        return true;
    }
    out << "[FAIL] " << message << '\n';
    ++(*failed);
    return false;
}

void expectResult(const SimaiTextEditResult& actual, const ExpectedResult& expected,
                  const QString& label, QTextStream& out, int* failed)
{
    expect(actual.consumed == expected.consumed, label + QStringLiteral(" consumed"), out, failed);
    expect(actual.transaction.hasEdit == expected.transaction.hasEdit,
           label + QStringLiteral(" transaction hasEdit"), out, failed);
    expect(actual.transaction.undoGroup == expected.transaction.undoGroup,
           label + QStringLiteral(" transaction undoGroup"), out, failed);
    expect(actual.transaction.text == expected.transaction.text,
           label + QStringLiteral(" transaction text"), out, failed);
    expect(actual.transaction.anchor == expected.transaction.anchor
               && actual.transaction.position == expected.transaction.position,
           label + QStringLiteral(" transaction selection/caret"), out, failed);
    expect(actual.transaction.replacementStart == expected.transaction.replacementStart
               && actual.transaction.replacementEnd == expected.transaction.replacementEnd
               && actual.transaction.replacementText == expected.transaction.replacementText
               && actual.transaction.insertsBlock == expected.transaction.insertsBlock,
           label + QStringLiteral(" transaction local replacement span"), out, failed);
    expect(actual.completion.active == expected.completion.active
               && actual.completion.opening == expected.completion.opening
               && actual.completion.closingPresent == expected.completion.closingPresent
               && actual.completion.startPosition == expected.completion.startPosition
               && actual.completion.candidates == expected.completion.candidates,
           label + QStringLiteral(" completion session"), out, failed);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    // Each independent v1 event supplies every input and every expected output
    // field. A new result field cannot silently escape the shared runner.
    const QList<PolicyCase> cases = {
        // Half-width normalization, carried over from the Widgets editor's spec:
        // that editor had its own copy of the conversion, and the policy — the
        // one v2 actually runs — had no coverage for these characters.
        {QStringLiteral("ideographic comma normalizes to a slash separator"),
         {QString(), 0, 0, QStringLiteral("、"), Qt::Key_unknown, Qt::NoModifier,
          true, true, false, true, QString()},
         {true, {QStringLiteral("/"), 1, 1, true, true, 0, 0, QStringLiteral("/"), false}, {}}},
        {QStringLiteral("full-width comma normalizes to the beat separator"),
         {QString(), 0, 0, QStringLiteral("，"), Qt::Key_unknown, Qt::NoModifier,
          true, true, false, true, QString()},
         {true, {QStringLiteral(","), 1, 1, true, true, 0, 0, QStringLiteral(","), false}, {}}},
        {QStringLiteral("full-width slash, hash and colon convert to half width"),
         {QString(), 0, 0, QStringLiteral("／＃："), Qt::Key_unknown, Qt::NoModifier,
          true, true, false, true, QString()},
         {true, {QStringLiteral("/#:"), 3, 3, true, true, 0, 0, QStringLiteral("/#:"), false}, {}}},
        {QStringLiteral("shift-6 forces a caret regardless of the IME's glyph"),
         {QString(), 0, 0, QStringLiteral("＾"), Qt::Key_6, Qt::ShiftModifier,
          false, true, false, true, QString()},
         {true, {QStringLiteral("^"), 1, 1, true, true, 0, 0, QStringLiteral("^"), false}, {}}},
        {QStringLiteral("half-width conversion is off when the preference is off"),
         {QString(), 0, 0, QStringLiteral("，"), Qt::Key_unknown, Qt::NoModifier,
          true, false, false, true, QString()},
         {true, {QStringLiteral("，"), 1, 1, true, true, 0, 0, QStringLiteral("，"), false}, {}}},
        {QStringLiteral("IME half-width bracket conversion"),
         {QString(), 0, 0, QStringLiteral("【"), Qt::Key_BracketLeft, Qt::NoModifier,
          true, true, false, true, QString()},
         {true, {QStringLiteral("[]"), 1, 1, true, true, 0, 0, QStringLiteral("[]"), false},
          {true, QLatin1Char('['), true, 1,
           {QStringLiteral("8:1]"), QStringLiteral("4:1]"), QStringLiteral("16:3]"),
            QStringLiteral("384:1]")}}}},
        {QStringLiteral("selection replacement"),
         {QStringLiteral("abc"), 1, 2, QStringLiteral("["), Qt::Key_BracketLeft,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("a[]c"), 2, 2, true, true, 1, 2, QStringLiteral("[]"), false},
          {true, QLatin1Char('['), true, 2,
           {QStringLiteral("8:1]"), QStringLiteral("4:1]"), QStringLiteral("16:3]"),
            QStringLiteral("384:1]")}}}},
        {QStringLiteral("repeated text replacement stays local"),
         {QStringLiteral("xx"), 1, 2, QStringLiteral("["), Qt::Key_BracketLeft,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("x[]"), 2, 2, true, true, 1, 2, QStringLiteral("[]"), false},
          {true, QLatin1Char('['), true, 2,
           {QStringLiteral("8:1]"), QStringLiteral("4:1]"), QStringLiteral("16:3]"),
            QStringLiteral("384:1]")}}}},
        {QStringLiteral("bracket pair insert"),
         {QStringLiteral("x"), 1, 1, QStringLiteral("{"), Qt::Key_BraceLeft,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("x{}"), 2, 2, true, true, 1, 1, QStringLiteral("{}"), false},
          {true, QLatin1Char('{'), true, 2,
           {QStringLiteral("16}"), QStringLiteral("24}"), QStringLiteral("32}")}}}},
        {QStringLiteral("whole BPM completion entry"),
         {QString(), 0, 0, QStringLiteral("("), 0, Qt::NoModifier,
          false, true, false, true, QStringLiteral("180")},
         {true, {QStringLiteral("()"), 1, 1, true, true, 0, 0, QStringLiteral("()"), false},
          {true, QLatin1Char('('), true, 1, {QStringLiteral("180)")}}}},
        {QStringLiteral("existing left bracket advances"),
         {QStringLiteral("[8:1]"), 0, 0, QStringLiteral("["), Qt::Key_BracketLeft,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("[8:1]"), 1, 1, false, false, 0, 0, QString(), false},
          {true, QLatin1Char('['), false, 1,
           {QStringLiteral("8:1]"), QStringLiteral("4:1]"), QStringLiteral("16:3]"),
            QStringLiteral("384:1]")}}}},
        {QStringLiteral("right bracket skips"),
         {QStringLiteral("[]"), 1, 1, QStringLiteral("]"), Qt::Key_BracketRight,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("[]"), 2, 2, false, false, 0, 0, QString(), false}, {}}},
        {QStringLiteral("empty pair single delete"),
         {QStringLiteral("[]"), 1, 1, QStringLiteral("\b"), Qt::Key_Backspace,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QString(), 0, 0, true, true, 0, 2, QString(), false}, {}}},
        {QStringLiteral("ordinary Backspace stays native"),
         {QStringLiteral("abc"), 2, 2, QStringLiteral("\b"), Qt::Key_Backspace,
          Qt::NoModifier, false, true, false, true, QString()},
         {false, {QStringLiteral("abc"), 2, 2, false, false, 0, 0, QString(), false}, {}}},
        {QStringLiteral("ordinary Delete stays native"),
         {QStringLiteral("abc"), 1, 1, QStringLiteral("\x7f"), Qt::Key_Delete,
          Qt::NoModifier, false, true, false, true, QString()},
         {false, {QStringLiteral("abc"), 1, 1, false, false, 0, 0, QString(), false}, {}}},
        {QStringLiteral("h hold completion entry"),
         {QString(), 0, 0, QStringLiteral("h"), Qt::Key_H,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("h"), 1, 1, true, true, 0, 0, QStringLiteral("h"), false},
          {true, QLatin1Char('['), false, 1,
           {QStringLiteral("[8:1]"), QStringLiteral("[4:1]"), QStringLiteral("[16:3]"),
            QStringLiteral("[384:1]")}}}},
        {QStringLiteral("active bracket completion treats h as a filter character"),
         {QStringLiteral("[]"), 1, 1, QStringLiteral("h"), Qt::Key_H,
          Qt::NoModifier, false, true, false, true, QString(), true},
         {true, {QStringLiteral("[h]"), 2, 2, true, true, 1, 1, QStringLiteral("h"), false}, {}}},
        {QStringLiteral("Enter line break"),
         {QStringLiteral("x"), 1, 1, QString(), Qt::Key_Return,
          Qt::NoModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("x\n"), 2, 2, true, true, 1, 1, QStringLiteral("\n"), true}, {}}},
        {QStringLiteral("Ctrl+Enter line break"),
         {QStringLiteral("x"), 1, 1, QString(), Qt::Key_Return,
          Qt::ControlModifier, false, true, false, true, QString()},
         {true, {QStringLiteral("x\n"), 2, 2, true, true, 1, 1, QStringLiteral("\n"), true}, {}}},
        {QStringLiteral("overwrite mode falls through"),
         {QStringLiteral("x"), 1, 1, QStringLiteral("["), Qt::Key_BracketLeft,
          Qt::NoModifier, false, true, true, true, QString()},
         {false, {QStringLiteral("x"), 1, 1, false, false, 0, 0, QString(), false}, {}}},
    };

    for (const PolicyCase& policyCase : cases) {
        expectResult(miacode::editor::applySimaiTextEditPolicy(policyCase.request),
                     policyCase.expected, policyCase.label, out, &failed);
    }

    // A refused command-modified key must tell its adapter whether Qt's text
    // control would still type the literal character. Qt drops Ctrl-modified
    // characters itself, and Alt/Option is a real composition modifier, but a
    // Meta/Super modifier (macOS 物理 Control) reaches the raw insert path.
    struct FallbackCase {
        QString label;
        QString input;
        int key;
        Qt::KeyboardModifiers modifiers;
        bool suppress;
    };
    const FallbackCase fallbackCases[] = {
        {QStringLiteral("Meta+Z is suppressed instead of typing z"), QStringLiteral("z"),
         Qt::Key_Z, Qt::MetaModifier, true},
        {QStringLiteral("Meta+C is suppressed instead of typing c"), QStringLiteral("c"),
         Qt::Key_C, Qt::MetaModifier, true},
        {QStringLiteral("Ctrl+Z stays a native shortcut"), QStringLiteral("z"),
         Qt::Key_Z, Qt::ControlModifier, false},
        {QStringLiteral("Ctrl+Meta+Z stays a native shortcut"), QStringLiteral("z"),
         Qt::Key_Z, Qt::ControlModifier | Qt::MetaModifier, false},
        {QStringLiteral("Alt composition still types its character"), QStringLiteral("\u00f8"),
         Qt::Key_O, Qt::AltModifier, false},
        {QStringLiteral("Meta with no text is not suppressed"), QString(),
         Qt::Key_Left, Qt::MetaModifier, false},
    };
    for (const FallbackCase& fallbackCase : fallbackCases) {
        miacode::editor::SimaiTextEditRequest request;
        request.text = QStringLiteral("abc");
        request.anchor = request.position = 3;
        request.input = fallbackCase.input;
        request.key = fallbackCase.key;
        request.modifiers = fallbackCase.modifiers;
        const auto result = miacode::editor::applySimaiTextEditPolicy(request);
        const bool ok = !result.consumed
            && result.suppressFallbackInsert == fallbackCase.suppress
            && result.transaction.text == QStringLiteral("abc");
        out << (ok ? "[PASS] " : "[FAIL] ") << fallbackCase.label << '\n';
        if (!ok) ++failed;
    }

    // Keys whose text() is a control character. The policy's insert fallback
    // used to gate on "is there text", so Escape typed U+001B into the chart —
    // a character Qt's own controls would have refused. Declining leaves the
    // key to Qt, which refuses it too.
    struct NonTextKeyCase {
        QString label;
        QString input;
        int key;
    };
    const NonTextKeyCase nonTextCases[] = {
        {QStringLiteral("Escape types nothing"), QStringLiteral("\u001B"), Qt::Key_Escape},
        {QStringLiteral("Backspace types nothing"), QStringLiteral("\b"), Qt::Key_Backspace},
        {QStringLiteral("Delete types nothing"), QStringLiteral("\u007F"), Qt::Key_Delete},
        {QStringLiteral("Cancel types nothing"), QStringLiteral("\u0018"), Qt::Key_Cancel},
    };
    for (const NonTextKeyCase& nonTextCase : nonTextCases) {
        miacode::editor::SimaiTextEditRequest request;
        request.text = QStringLiteral("abc");
        request.anchor = request.position = 3;
        request.input = nonTextCase.input;
        request.key = nonTextCase.key;
        const auto result = miacode::editor::applySimaiTextEditPolicy(request);
        const bool ok = !result.consumed && !result.transaction.hasEdit
            && result.transaction.text == QStringLiteral("abc");
        out << (ok ? "[PASS] " : "[FAIL] ") << nonTextCase.label << '\n';
        if (!ok) ++failed;
    }

    // The other half of the same rule: real text still gets typed, including a
    // tab and a zero-width joiner, which are not printable but are content.
    struct TextKeyCase {
        QString label;
        QString input;
        int key;
        QString expected;
    };
    const TextKeyCase textCases[] = {
        {QStringLiteral("a letter still types"), QStringLiteral("d"), Qt::Key_D,
         QStringLiteral("abcd")},
        {QStringLiteral("a tab still types"), QStringLiteral("\t"), Qt::Key_Tab,
         QStringLiteral("abc\t")},
        {QStringLiteral("a zero-width joiner still types"), QStringLiteral("\u200D"), 0,
         QStringLiteral("abc\u200D")},
    };
    for (const TextKeyCase& textCase : textCases) {
        miacode::editor::SimaiTextEditRequest request;
        request.text = QStringLiteral("abc");
        request.anchor = request.position = 3;
        request.input = textCase.input;
        request.key = textCase.key;
        const auto result = miacode::editor::applySimaiTextEditPolicy(request);
        const bool ok = result.consumed && result.transaction.text == textCase.expected;
        out << (ok ? "[PASS] " : "[FAIL] ") << textCase.label << '\n';
        if (!ok) ++failed;
    }

    if (failed != 0) {
        out << "SimaiTextEditPolicy spec failed: " << failed << '\n';
        return 1;
    }
    out << "SimaiTextEditPolicy spec passed.\n";
    return 0;
}
