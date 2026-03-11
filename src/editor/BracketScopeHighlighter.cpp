#include "BracketScopeHighlighter.h"

#include <QColor>
#include <QTextBlock>
#include <QTextDocument>

namespace {
const QColor kParenColor(QStringLiteral("#2B5BFF"));
const QColor kBraceColor(QStringLiteral("#2F7EA7"));
const QColor kSquareColor(QStringLiteral("#4F8F6A"));
}

BracketScopeHighlighter::BracketScopeHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
}

bool BracketScopeHighlighter::isOpeningBracket(QChar ch, BracketKind* kindOut, QChar* closingOut)
{
    if (ch == QLatin1Char('(')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Paren;
        }
        if (closingOut != nullptr) {
            *closingOut = QLatin1Char(')');
        }
        return true;
    }
    if (ch == QLatin1Char('{')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Brace;
        }
        if (closingOut != nullptr) {
            *closingOut = QLatin1Char('}');
        }
        return true;
    }
    if (ch == QLatin1Char('[')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Square;
        }
        if (closingOut != nullptr) {
            *closingOut = QLatin1Char(']');
        }
        return true;
    }
    return false;
}

bool BracketScopeHighlighter::isClosingBracket(QChar ch, BracketKind* kindOut)
{
    if (ch == QLatin1Char(')')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Paren;
        }
        return true;
    }
    if (ch == QLatin1Char('}')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Brace;
        }
        return true;
    }
    if (ch == QLatin1Char(']')) {
        if (kindOut != nullptr) {
            *kindOut = BracketKind::Square;
        }
        return true;
    }
    return false;
}

QTextCharFormat BracketScopeHighlighter::formatForKind(BracketKind kind)
{
    QTextCharFormat format;
    switch (kind) {
    case BracketKind::Paren:
        format.setForeground(kParenColor);
        break;
    case BracketKind::Brace:
        format.setForeground(kBraceColor);
        break;
    case BracketKind::Square:
        format.setForeground(kSquareColor);
        break;
    }
    return format;
}

void BracketScopeHighlighter::highlightBlock(const QString& text)
{
    QVector<StackEntry> stack;
    const QTextBlock previous = currentBlock().previous();
    if (previous.isValid()) {
        if (const auto* previousData = dynamic_cast<const BlockData*>(previous.userData()); previousData != nullptr) {
            stack = previousData->stackAtEnd;
        }
    }

    for (int index = 0; index < text.size(); ++index) {
        const QChar ch = text.at(index);

        if (!stack.isEmpty()) {
            setFormat(index, 1, formatForKind(stack.constLast().kind));
        }

        BracketKind bracketKind = BracketKind::Paren;
        QChar closing;
        if (isOpeningBracket(ch, &bracketKind, &closing)) {
            setFormat(index, 1, formatForKind(bracketKind));
            stack.append({bracketKind, closing});
            continue;
        }

        if (!isClosingBracket(ch, &bracketKind)) {
            continue;
        }

        setFormat(index, 1, formatForKind(bracketKind));
        if (stack.isEmpty()) {
            continue;
        }

        if (stack.constLast().closing == ch) {
            stack.removeLast();
            continue;
        }

        int matchingIndex = -1;
        for (int i = stack.size() - 1; i >= 0; --i) {
            if (stack.at(i).closing == ch) {
                matchingIndex = i;
                break;
            }
        }
        if (matchingIndex >= 0) {
            stack.resize(matchingIndex);
        }
    }

    auto* data = new BlockData();
    data->stackAtEnd = stack;
    setCurrentBlockUserData(data);
}
