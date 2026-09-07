#pragma once

#include <QStringView>
#include <QString>

#include <optional>

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition);

struct BookmarkComment {
    int markerPosition = -1;
    QString title;
    // A `||` comment that carries chart control data — an empty comment, or a
    // bare meter such as `4/4` — rather than a section name. The line is still
    // a comment the edit helpers below can work on; it is simply not something
    // a bookmark listing should show, which is what the sidebar filters on.
    bool control = false;
};

// Persisted bookmark comments are parsed and rendered here, never in QML.
std::optional<BookmarkComment> parseBookmarkComment(QStringView lineText);
// True for the comment body (everything after the `||`) of a control comment.
bool isControlBookmarkComment(QStringView commentBody);
QString appendBookmarkComment(QStringView lineText, QStringView title);
QString renameBookmarkComment(QStringView lineText, QStringView title);
QString removeBookmarkComment(QStringView lineText);

} // namespace miacode::editor
