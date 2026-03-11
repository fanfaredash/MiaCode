#pragma once

#include <QSyntaxHighlighter>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QVector>

class BracketScopeHighlighter final : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit BracketScopeHighlighter(QTextDocument* parent = nullptr);

protected:
    void highlightBlock(const QString& text) override;

private:
    enum class BracketKind {
        Paren = 0,
        Brace = 1,
        Square = 2,
    };

    struct StackEntry {
        BracketKind kind = BracketKind::Paren;
        QChar closing;
    };

    class BlockData final : public QTextBlockUserData
    {
    public:
        QVector<StackEntry> stackAtEnd;
    };

    static bool isOpeningBracket(QChar ch, BracketKind* kindOut, QChar* closingOut);
    static bool isClosingBracket(QChar ch, BracketKind* kindOut);
    static QTextCharFormat formatForKind(BracketKind kind);
};
