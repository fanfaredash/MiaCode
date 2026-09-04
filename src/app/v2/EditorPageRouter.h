#pragma once

#include <functional>

namespace miacode::v2 {

// Switching the editor area between its full-page surfaces.
//
// Stage 3.5 items 2-3: QmlEditorPageHost holds the QML page id while this
// interface keeps the runtime page-state transitions narrow. The host owns the
// asynchronous QML leave guard; the router only commits the domain transition
// after that guard succeeds.
//
// The interface remains deliberately small so page navigation does not expose
// the old window/widget implementation or recreate a second page owner.
//
// Deliberately Qt-free — not even QObject. `editor_page_router_spec` links
// Qt6::Core+Test only.
class EditorPageRouter
{
public:
    virtual ~EditorPageRouter() = default;

    // Whether a difficulty is currently being edited. The page host records it
    // before leaving, so returning from an overlay page lands back on the same
    // difficulty rather than on the metadata page.
    virtual bool hasActiveDifficulty() const = 0;
    virtual int activeDifficultyId() const = 0;

    // Each of these returns false when the domain switch cannot be committed.
    // The page host only calls them after its QML leave decision succeeds.
    virtual bool enterDifficultyPage(int difficultyId) = 0;
    virtual bool enterMetadataPage() = 0;
    virtual bool enterLatencyPage() = 0;
    virtual bool enterExportPage() = 0;

    // 打包为 ZIP. Routed here rather than through a command bus because it is
    // reached from the page host's own menu entry.
    virtual void packChartAsZip() = 0;

    // 偏好设置. The page host closes any overlay page first, so the entry point
    // belongs with the other page-scope actions.
    virtual void openPreferences() = 0;

    // "May the window close?" — the same unsaved-changes question the page
    // switches ask, at window scope. The continuation runs once; declining and
    // dismissing are the same answer.
    virtual void requestShellClose(std::function<void(bool)> onDecided) = 0;

protected:
    EditorPageRouter() = default;
    EditorPageRouter(const EditorPageRouter&) = default;
    EditorPageRouter& operator=(const EditorPageRouter&) = default;
};

}  // namespace miacode::v2
