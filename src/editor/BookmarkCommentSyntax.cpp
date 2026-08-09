#include "editor/BookmarkCommentSyntax.h"

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition)
{
    return markerPosition >= 0
        && markerPosition + 1 < lineText.size()
        && lineText.mid(markerPosition, 2) == QStringView(u"||")
        && (markerPosition + 2 >= lineText.size() || lineText.at(markerPosition + 2) != QLatin1Char('|'));
}

} // namespace miacode::editor
