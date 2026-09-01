#pragma once

#include <functional>

namespace miacode::v2 {

// Switching the editor area between its full-page surfaces.
//
// Stage 3.5 items 2-3: QmlEditorPageHost holds the QML page id, but the switch
// itself still has to run a pile of work only the window can do today — the
// unsaved-changes guard, releasing the export session, stopping playback while
// preserving the playhead, resetting the active difficulty, the bottom-tabs
// mode, validation decorations and the window title.
//
// Those entry points live in MainWindowPrivateMethodsA.inc, so the page host
// needed `friend class` to call them. Publishing them was the wrong fix: they
// also drive a hidden QStackedWidget that exists only because the window has
// not been deleted yet, and writing that into MainWindow's public interface
// would formalize the very thing item 3 removes.
//
// So the page host names these seven operations instead. The window implements
// them; when the widget stack goes, the implementation loses its dead half and
// keeps the domain half, and the page host does not change.
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

    // Each of these returns false when the switch was refused — most often
    // because the unsaved-changes guard was declined. A caller that shows a page
    // on a refused switch would leave the shell and the document disagreeing
    // about where the user is, so the answer is not decorative.
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
