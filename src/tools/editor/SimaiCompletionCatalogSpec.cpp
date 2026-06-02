#include "SimaiCompletionCatalog.h"

#include <QCoreApplication>
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

    using namespace miacode::editor;

    // Static lists keep their product-decided order.
    expect(
        squareDurationCandidates()
            == QStringList({QStringLiteral("8:1]"), QStringLiteral("4:1]"),
                            QStringLiteral("16:3]"), QStringLiteral("384:1]")}),
        QStringLiteral("'[' duration candidates are 8:1 4:1 16:3 384:1 in order"),
        out, &failed);
    expect(
        braceDivisionCandidates()
            == QStringList({QStringLiteral("16}"), QStringLiteral("24}"),
                            QStringLiteral("32}")}),
        QStringLiteral("'{' subdivision candidates are 16 24 32 in order"),
        out, &failed);

    // Opening / closing helpers.
    expect(isBracketOpening(QLatin1Char('[')) && isBracketOpening(QLatin1Char('('))
               && isBracketOpening(QLatin1Char('{')),
           QStringLiteral("isBracketOpening true for ( [ {"), out, &failed);
    expect(!isBracketOpening(QLatin1Char('h')) && !isBracketOpening(QLatin1Char(']')),
           QStringLiteral("isBracketOpening false for non-openers"), out, &failed);
    expect(isBracketClosing(QLatin1Char(']')) && isBracketClosing(QLatin1Char(')'))
               && isBracketClosing(QLatin1Char('}')),
           QStringLiteral("isBracketClosing true for ) ] }"), out, &failed);
    expect(!isBracketClosing(QLatin1Char('h')) && !isBracketClosing(QLatin1Char('[')),
           QStringLiteral("isBracketClosing false for non-closers"), out, &failed);
    expect(closingBracketFor(QLatin1Char('[')) == QLatin1Char(']')
               && closingBracketFor(QLatin1Char('(')) == QLatin1Char(')')
               && closingBracketFor(QLatin1Char('{')) == QLatin1Char('}'),
           QStringLiteral("closingBracketFor maps each opener"), out, &failed);
    expect(closingBracketFor(QLatin1Char('x')).isNull(),
           QStringLiteral("closingBracketFor is null for non-openers"), out, &failed);

    // BPM token normalization.
    expect(normalizeBpmToken(QStringLiteral("120.000")) == QLatin1String("120"),
           QStringLiteral("normalizeBpmToken trims 120.000 -> 120"), out, &failed);
    expect(normalizeBpmToken(QStringLiteral(" 184.50 ")) == QLatin1String("184.5"),
           QStringLiteral("normalizeBpmToken trims trailing zero + whitespace"), out, &failed);
    expect(normalizeBpmToken(QStringLiteral("abc")) == QLatin1String("abc"),
           QStringLiteral("normalizeBpmToken passes through non-numeric"), out, &failed);

    // Appeared-BPM scan: first-seen order, deduplicated, normalized.
    const QString chart = QStringLiteral("(160){8}1,2,(120)3,4,(160.0)5,");
    expect(appearedBpmValues(chart)
               == QStringList({QStringLiteral("160"), QStringLiteral("120")}),
           QStringLiteral("appearedBpmValues finds distinct BPMs in first-seen order"),
           out, &failed);

    // '(' candidates: wholebpm first, then appeared, deduped, each closing.
    expect(parenBpmCandidates(QStringLiteral("120"), chart)
               == QStringList({QStringLiteral("120)"), QStringLiteral("160)")}),
           QStringLiteral("parenBpmCandidates puts wholebpm first then appeared"),
           out, &failed);
    expect(parenBpmCandidates(QString(), QString()).isEmpty(),
           QStringLiteral("parenBpmCandidates empty when nothing is known"), out, &failed);

    // Dispatch.
    expect(candidatesForOpening(QLatin1Char('['), QString(), QString())
               == squareDurationCandidates(),
           QStringLiteral("candidatesForOpening dispatches '[' to durations"), out, &failed);
    expect(candidatesForOpening(QLatin1Char('h'), QString(), QString()).isEmpty(),
           QStringLiteral("candidatesForOpening empty for non-openers"), out, &failed);

    if (failed != 0) {
        out << "SimaiCompletionCatalog spec failed: " << failed << '\n';
        return 1;
    }

    out << "SimaiCompletionCatalog spec passed.\n";
    return 0;
}
