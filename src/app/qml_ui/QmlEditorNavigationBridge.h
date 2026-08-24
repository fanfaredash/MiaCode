#pragma once

#include <QtGlobal>

#include <utility>

namespace miacode::qml_ui {

// Value-only request passed from the backend timeline to the active QML text
// editor.  The request is deliberately guarded by the same difficulty and
// revision identity as the document projection so a slow/old request can
// never move the current editor caret.
struct QmlEditorNavigationRequest {
    int difficultyId = -1;
    quint64 revision = 0;
    int line = 1;
    int column = 1;
    int endLine = 1;
    int endColumn = 1;
    bool selectToken = false;
    bool focusEditor = false;
    bool centerView = false;

    bool accepts(int activeDifficultyId, quint64 documentRevision) const
    {
        return difficultyId > 0 && difficultyId == activeDifficultyId
            && revision == documentRevision && line > 0 && column > 0
            && endLine >= line && endColumn > 0
            && (endLine > line || endColumn >= column);
    }
};

// The backend may acknowledge a reverse-navigation request only when the
// current chart source is actually available to realize it.  In particular,
// a hidden source or the metadata-source editor must not claim success just
// because the QML signal was emitted.
struct QmlEditorNavigationReadiness {
    int difficultyId = -1;
    quint64 revision = 0;
    bool sourceVisible = false;
    bool metadataMode = false;

    bool accepts(const QmlEditorNavigationRequest& request, int activeDifficultyId,
                 quint64 documentRevision) const
    {
        return sourceVisible && !metadataMode
            && difficultyId == activeDifficultyId && revision == documentRevision
            && difficultyId == request.difficultyId && revision == request.revision
            && request.accepts(activeDifficultyId, documentRevision);
    }
};

// Preview follow has two modes. While playing with 代码跟随 on it MOVES the
// caret, which reaches QML through QmlEditorNavigationRequest. While paused —
// or with 代码跟随 off — it must not move the caret, so v1 paints a decoration
// on PlainCodeEditor instead: the playhead's token span plus a visual follow
// caret, optionally scrolled into view. v2's visible editor is the QML one and
// had no decoration channel at all, so seeking while paused produced no follow
// at all. This carries that decoration as a value under the same identity gate.
struct QmlEditorFollowDecoration {
    bool active = false;
    int difficultyId = -1;
    quint64 revision = 0;
    int startLine = 1;
    int startColumn = 1;
    int endLine = 1;
    int endColumn = 1;
    int cursorLine = 1;
    int cursorColumn = 1;
    bool ensureVisible = false;

    bool accepts(int activeDifficultyId, quint64 documentRevision) const
    {
        if (!active) {
            return true;
        }
        return difficultyId > 0 && difficultyId == activeDifficultyId
            && revision == documentRevision && startLine > 0 && startColumn > 0
            && endLine >= startLine && endColumn > 0 && cursorLine > 0 && cursorColumn > 0
            && (endLine > startLine || endColumn >= startColumn);
    }
};

template <typename Apply>
bool routeQmlEditorNavigation(const QmlEditorNavigationRequest& request,
                              const QmlEditorNavigationReadiness& readiness,
                              int activeDifficultyId, quint64 documentRevision,
                              Apply&& apply)
{
    if (!readiness.accepts(request, activeDifficultyId, documentRevision)) {
        return false;
    }
    std::forward<Apply>(apply)(request);
    return true;
}

// A decoration is only shown on a visible chart source. The metadata-source
// editor and a hidden editor must clear it rather than paint a stale span.
template <typename Apply>
bool routeQmlEditorFollowDecoration(const QmlEditorFollowDecoration& decoration,
                                    const QmlEditorNavigationReadiness& readiness,
                                    int activeDifficultyId, quint64 documentRevision,
                                    Apply&& apply)
{
    QmlEditorFollowDecoration routed = decoration;
    if (!readiness.sourceVisible || readiness.metadataMode
        || !decoration.accepts(activeDifficultyId, documentRevision)) {
        routed = QmlEditorFollowDecoration();
    }
    std::forward<Apply>(apply)(routed);
    return routed.active;
}

} // namespace miacode::qml_ui
