#include "BracketScopeHighlighter.h"
#include "UiTheme.h"

#include <QColor>
#include <QTextBlock>
#include <QTextDocument>

namespace {
QColor parenColor()
{
    return UiTheme::isDarkTheme() ? QColor(QStringLiteral("#F29A83")) : QColor(QStringLiteral("#A23B2A"));
}

QColor braceColor()
{
    return UiTheme::isDarkTheme() ? QColor(QStringLiteral("#F29A83")) : QColor(QStringLiteral("#A23B2A"));
}

QColor squareColor()
{
    return UiTheme::isDarkTheme() ? QColor(QStringLiteral("#88A4FF")) : QColor(QStringLiteral("#1D4ED8"));
}

QColor commentMarkerColor()
{
    return UiTheme::isDarkTheme() ? QColor(QStringLiteral("#71B77A")) : QColor(QStringLiteral("#15803D"));
}

int commentStartIndex(const QString& text)
{
    return text.indexOf(QStringLiteral("||"));
}
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
        format.setForeground(parenColor());
        break;
    case BracketKind::Brace:
        format.setForeground(braceColor());
        break;
    case BracketKind::Square:
        format.setForeground(squareColor());
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

    const int commentStart = commentStartIndex(text);
    const int scanEnd = commentStart >= 0 ? commentStart : text.size();

    for (int index = 0; index < scanEnd; ++index) {
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

    if (commentStart >= 0) {
        QTextCharFormat commentFormat;
        commentFormat.setForeground(commentMarkerColor());
        setFormat(commentStart, text.size() - commentStart, commentFormat);
    }

    auto* data = new BlockData();
    data->stackAtEnd = stack;
    setCurrentBlockUserData(data);

    // Mirror the carry-over stack into the integer block state. QSyntaxHighlighter
    // only re-highlights the following block when a block's userState() changes
    // between runs (the internal forceHighlightOfNextBlock signal) — the BlockData
    // above is invisible to that machinery. Without this, opening or closing a
    // bracket on one line never re-colors the lines its scope spans: they stay
    // stale until each is independently edited (the "next line stays highlighted
    // until you type on it" bug). Fold both depth and the kind sequence into the
    // signature so any change to the end-of-line scope is observed. Empty stack
    // maps to 0; we never emit -1, which Qt reserves for "no state".
    unsigned signature = 0;
    for (const StackEntry& entry : stack) {
        signature = signature * 31u + static_cast<unsigned>(entry.kind) + 1u;
    }
    setCurrentBlockState(static_cast<int>(signature & 0x7fffffffu));
}
