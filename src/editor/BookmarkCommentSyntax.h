#pragma once

#include <QStringView>
#include <QString>

#include <optional>

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition);

struct BookmarkComment {
    int markerPosition = -1;
    QString title;
};

// Persisted bookmark comments are parsed and rendered here, never in QML.
std::optional<BookmarkComment> parseBookmarkComment(QStringView lineText);
QString appendBookmarkComment(QStringView lineText, QStringView title);
QString renameBookmarkComment(QStringView lineText, QStringView title);
QString removeBookmarkComment(QStringView lineText);

} // namespace miacode::editor
