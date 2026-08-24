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

} // namespace miacode::qml_ui
