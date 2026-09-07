#include "editor/BookmarkCommentSyntax.h"

#include <QRegularExpression>
#include <QStringList>

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition)
{
    return markerPosition >= 0
        && markerPosition + 1 < lineText.size()
        && lineText.mid(markerPosition, 2) == QStringView(u"||")
        && (markerPosition + 2 >= lineText.size() || lineText.at(markerPosition + 2) != QLatin1Char('|'));
}

namespace {
// Derived titles keep the first delimiter-separated token, truncated when
// overlong — the same rule the Widgets outline used, so the same comment reads
// the same in both sidebars.
constexpr int kDerivedTitleTokenMaxChars = 16;
constexpr int kDerivedTitleMaxChars = 20;

QString collapsedComment(QStringView body)
{
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));
    QString value = body.toString().trimmed();
    value.replace(whitespaceRe, QStringLiteral(" "));
    return value;
}

// `[label]` at the head of the comment names the bookmark outright; anything
// after the closing bracket is body text.
QString explicitLabel(QStringView body)
{
    const QString value = body.toString().trimmed();
    if (value.isEmpty() || value.front() != QLatin1Char('[')) return {};
    const int close = value.indexOf(QLatin1Char(']'), 1);
    return close > 1 ? value.mid(1, close - 1).trimmed() : QString();
}

QString derivedTitle(QStringView body)
{
    const QString normalized = collapsedComment(body);
    if (normalized.isEmpty()) return QStringLiteral("书签");
    static const QRegularExpression separatorRe(
        QStringLiteral("[\\s,\\x{FF0C}\\x{3001};\\x{FF1B}:\\x{FF1A}/\\x{FF0F}]+"));
    const QStringList tokens = normalized.split(separatorRe, Qt::SkipEmptyParts);
    if (!tokens.isEmpty()) return tokens.first().left(kDerivedTitleTokenMaxChars);
    return normalized.left(kDerivedTitleMaxChars);
}

QString displayTitle(QStringView body)
{
    const QString label = explicitLabel(body);
    return label.isEmpty() ? derivedTitle(body) : label;
}

// The spelling written back to the chart. Distinct from displayTitle on
// purpose: what is written is always the caller's full name in brackets, while
// what is listed may be a shortened derivation of a hand-written comment.
QString normalizedTitle(QStringView title)
{
    QString value = title.toString().trimmed();
    if (value.size() >= 2 && value.front() == QLatin1Char('[')
        && value.back() == QLatin1Char(']')) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value.isEmpty() ? QStringLiteral("书签") : value;
}

QString suffix(QStringView title)
{
    return QStringLiteral(" || [%1]").arg(normalizedTitle(title));
}
} // namespace

bool isControlBookmarkComment(QStringView commentBody)
{
    const QString normalized = collapsedComment(commentBody);
    if (normalized.isEmpty()) return true;
    static const QRegularExpression meterRe(QStringLiteral(R"(^\d+\s*/\s*\d+$)"));
    return meterRe.match(normalized).hasMatch();
}

std::optional<BookmarkComment> parseBookmarkComment(QStringView lineText)
{
    const int marker = lineText.indexOf(QStringView(u"||"));
    if (!isBookmarkCommentMarker(lineText, marker)) return std::nullopt;
    const QStringView body = lineText.mid(marker + 2);
    return BookmarkComment{marker, displayTitle(body), isControlBookmarkComment(body)};
}

QString appendBookmarkComment(QStringView lineText, QStringView title)
{
    const QString source = lineText.toString();
    if (parseBookmarkComment(lineText).has_value()) return source;
    return source.trimmed().isEmpty()
        ? QStringLiteral("|| [%1]").arg(normalizedTitle(title))
        : source.trimmed() + suffix(title);
}

QString renameBookmarkComment(QStringView lineText, QStringView title)
{
    const auto bookmark = parseBookmarkComment(lineText);
    if (!bookmark.has_value()) return lineText.toString();
    return lineText.left(bookmark->markerPosition).toString().trimmed() + suffix(title);
}

QString removeBookmarkComment(QStringView lineText)
{
    const auto bookmark = parseBookmarkComment(lineText);
    if (!bookmark.has_value()) return lineText.toString();
    return lineText.left(bookmark->markerPosition).toString().trimmed();
}

} // namespace miacode::editor
