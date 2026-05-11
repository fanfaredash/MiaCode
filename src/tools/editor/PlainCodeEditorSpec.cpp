#include "editor/PlainCodeEditor.h"

#include <QCoreApplication>
#include <QKeyEvent>
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

    if (failed != 0) {
        out << "PlainCodeEditor spec failed: " << failed << '\n';
        return 1;
    }

    out << "PlainCodeEditor spec passed.\n";
    return 0;
}
