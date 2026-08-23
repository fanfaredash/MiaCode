#include "editor/BookmarkCommentSyntax.h"

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition)
{
    return markerPosition >= 0
        && markerPosition + 1 < lineText.size()
        && lineText.mid(markerPosition, 2) == QStringView(u"||")
        && (markerPosition + 2 >= lineText.size() || lineText.at(markerPosition + 2) != QLatin1Char('|'));
}

namespace {
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

std::optional<BookmarkComment> parseBookmarkComment(QStringView lineText)
{
    const int marker = lineText.indexOf(QStringView(u"||"));
    if (!isBookmarkCommentMarker(lineText, marker)) return std::nullopt;
    return BookmarkComment{marker, normalizedTitle(lineText.mid(marker + 2))};
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
