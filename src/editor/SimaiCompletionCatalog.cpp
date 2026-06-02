#include "SimaiCompletionCatalog.h"

#include <QRegularExpression>

namespace miacode::editor {

bool isBracketOpening(QChar ch)
{
    return ch == QLatin1Char('(') || ch == QLatin1Char('[') || ch == QLatin1Char('{');
}

bool isBracketClosing(QChar ch)
{
    return ch == QLatin1Char(')') || ch == QLatin1Char(']') || ch == QLatin1Char('}');
}

QChar closingBracketFor(QChar opening)
{
    switch (opening.unicode()) {
    case '(': return QLatin1Char(')');
    case '[': return QLatin1Char(']');
    case '{': return QLatin1Char('}');
    default: return QChar();
    }
}

QStringList squareDurationCandidates()
{
    // Order fixed per product decision — do not sort.
    return {
        QStringLiteral("8:1]"),
        QStringLiteral("4:1]"),
        QStringLiteral("16:3]"),
        QStringLiteral("384:1]"),
    };
}

QStringList braceDivisionCandidates()
{
    // Order fixed per product decision — do not sort.
    return {
        QStringLiteral("16}"),
        QStringLiteral("24}"),
        QStringLiteral("32}"),
    };
}

QString normalizeBpmToken(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    bool ok = false;
    const double value = trimmed.toDouble(&ok);
    if (!ok || value <= 0.0) {
        return trimmed;
    }
    QString text = QString::number(value, 'f', 3);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

QStringList appearedBpmValues(const QString& chartText)
{
    static const QRegularExpression bpmMarker(QStringLiteral("\\(\\s*([0-9]+(?:\\.[0-9]+)?)\\s*\\)"));
    QStringList seen;
    auto it = bpmMarker.globalMatch(chartText);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString token = normalizeBpmToken(match.captured(1));
        if (!token.isEmpty() && !seen.contains(token)) {
            seen.append(token);
        }
    }
    return seen;
}

QStringList parenBpmCandidates(const QString& wholeBpm, const QString& chartText)
{
    QStringList out;
    QStringList seen;
    const auto push = [&](const QString& rawToken) {
        const QString token = normalizeBpmToken(rawToken);
        if (token.isEmpty() || seen.contains(token)) {
            return;
        }
        seen.append(token);
        out.append(token + QLatin1Char(')'));
    };
    if (!wholeBpm.trimmed().isEmpty()) {
        push(wholeBpm);
    }
    const QStringList appeared = appearedBpmValues(chartText);
    for (const QString& token : appeared) {
        push(token);
    }
    return out;
}

QStringList candidatesForOpening(QChar opening, const QString& wholeBpm, const QString& chartText)
{
    switch (opening.unicode()) {
    case '[': return squareDurationCandidates();
    case '{': return braceDivisionCandidates();
    case '(': return parenBpmCandidates(wholeBpm, chartText);
    default: return {};
    }
}

}  // namespace miacode::editor
