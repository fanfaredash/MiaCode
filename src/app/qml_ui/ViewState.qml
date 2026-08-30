import QtQuick

QtObject {
    signal difficultyEditorActivationRequested(int difficultyId)
    // A closed tab is a view that no longer exists; whatever it accumulated —
    // its undo history above all — goes with it.
    signal editorClosed(string key)

    readonly property string metadataEditorKey: "metadata"
    property var openEditorTabs: []
    property var editorHistory: []
    property string activeEditorKey: ""
    readonly property bool hasActiveEditor: activeEditorKey.length > 0
    readonly property bool metadataEditorActive: activeEditorKey === metadataEditorKey
    readonly property bool difficultyEditorActive: activeEditorKey.startsWith("difficulty:")
    readonly property int activeDifficultyId: difficultyEditorActive
        ? Number(activeEditorKey.substring("difficulty:".length))
        : 0
    property int metadataEditorMode: 0
    property int editorCursorLine: 1
    property int editorCursorColumn: 1
    property bool sidebarVisible: true
    // Activity Bar 的选择属于当前工作台会话；Primary Sidebar 的展开状态
    // 继续由宿主偏好服务持久化。
    property string activeSidebarView: "chart"
    property bool difficultySectionExpanded: true
    // 书签分组的展开状态属于当前工作台会话。未被触碰过的分组跟随活动难度：
    // 当前难度展开，其余折叠——与 v1 的 outlineBookmarkGroupExpanded_ 一致。
    property var bookmarkGroupsExpanded: ({})
    property bool bottomPanelVisible: true
    property bool previewVisible: true
    property string compactPanel: ""

    // 编辑器标签是当前工作台会话中的视图集合。关闭标签只从集合中移除
    // 对应视图，谱面字段、难度对象和正文仍由 documentSession 持有。
    function difficultyEditorKey(id) {
        return "difficulty:" + id
    }

    function containsEditor(key) {
        return openEditorTabs.indexOf(key) >= 0
    }

    function bookmarkGroupExpanded(difficultyId) {
        const stored = bookmarkGroupsExpanded[difficultyId]
        return stored === undefined ? difficultyId === activeDifficultyId : stored
    }

    // Reassignment, not mutation: a QML var property only notifies when it is
    // assigned, so editing the map in place would leave every binding stale.
    function setBookmarkGroupExpanded(difficultyId, expanded) {
        const next = Object.assign({}, bookmarkGroupsExpanded)
        next[difficultyId] = expanded
        bookmarkGroupsExpanded = next
    }

    function recordEditorUse(key) {
        const history = editorHistory.filter(item => item !== key)
        history.push(key)
        editorHistory = history
    }

    // 难度标签激活必须先请求文档 owner 切换正文数据源，再发布活动标签。
    // 这样标题、字段、源码和解析结果在同一轮状态变化中读取同一个难度。
    function setActiveEditor(key, requestDifficultyActivation) {
        const difficultyId = key.startsWith("difficulty:")
            ? Number(key.substring("difficulty:".length))
            : 0
        if (difficultyId > 0 && requestDifficultyActivation !== false)
            difficultyEditorActivationRequested(difficultyId)
        activeEditorKey = key
    }

    function activateEditor(key) {
        if (!containsEditor(key))
            return
        setActiveEditor(key)
        recordEditorUse(key)
    }

    function openEditor(key) {
        if (!containsEditor(key)) {
            const tabs = openEditorTabs.slice()
            tabs.push(key)
            openEditorTabs = tabs
        }
        activateEditor(key)
    }

    function openMetadataEditor() {
        openEditor(metadataEditorKey)
    }

    function openDifficultyEditor(id) {
        if (id > 0)
            openEditor(difficultyEditorKey(id))
    }

    // Tab order is workspace presentation state, not chart structure. Moving
    // an editor view therefore never changes the document; it only exchanges
    // two existing editor tabs in this window.
    function swapEditorTabs(firstKey, secondKey) {
        if (!firstKey || !secondKey || firstKey === secondKey)
            return false
        const firstIndex = openEditorTabs.indexOf(firstKey)
        const secondIndex = openEditorTabs.indexOf(secondKey)
        if (firstIndex < 0 || secondIndex < 0)
            return false
        const tabs = openEditorTabs.slice()
        const first = tabs[firstIndex]
        tabs[firstIndex] = tabs[secondIndex]
        tabs[secondIndex] = first
        openEditorTabs = tabs
        return true
    }

    function closeEditor(key) {
        const closingIndex = openEditorTabs.indexOf(key)
        if (closingIndex < 0)
            return

        const tabs = openEditorTabs.slice()
        tabs.splice(closingIndex, 1)
        openEditorTabs = tabs
        editorHistory = editorHistory.filter(item => item !== key)
        editorClosed(key)

        if (activeEditorKey !== key)
            return

        let nextKey = ""
        for (let index = editorHistory.length - 1; index >= 0; --index) {
            if (tabs.indexOf(editorHistory[index]) >= 0) {
                nextKey = editorHistory[index]
                break
            }
        }
        if (nextKey.length === 0 && tabs.length > 0)
            nextKey = tabs[Math.min(closingIndex, tabs.length - 1)]
        setActiveEditor(nextKey)
        if (nextKey.length > 0)
            recordEditorUse(nextKey)
    }

    function closeActiveEditor() {
        closeEditor(activeEditorKey)
    }

    function resetEditorTabs(currentDifficultyId) {
        const tabs = currentDifficultyId > 0
            ? [difficultyEditorKey(currentDifficultyId)]
            : []
        openEditorTabs = tabs
        editorHistory = tabs.slice()
        setActiveEditor(currentDifficultyId > 0
            ? difficultyEditorKey(currentDifficultyId)
            : "", false)
    }

    // Filtering alone is not enough: this must also be able to put a tab back.
    // The document projection arrives on a queued connection, so a replacement
    // can be observed while the active difficulty is momentarily unset, and
    // resetEditorTabs(0) then empties the tab set. Without a way to recover,
    // the editor is left with nothing to show and no route back — which is the
    // shape of the intermittent "editor still shows the old chart" report.
    function syncDifficultyEditors(difficulties, activeDifficultyId) {
        const validKeys = {}
        const difficultyKeys = []
        for (let index = 0; index < difficulties.length; ++index) {
            const key = difficultyEditorKey(difficulties[index].id)
            validKeys[key] = true
            difficultyKeys.push(key)
        }

        const activeKey = activeDifficultyId > 0
            ? difficultyEditorKey(activeDifficultyId)
            : ""
        const preferredKey = validKeys[activeKey]
            ? activeKey
            : (difficultyKeys.length > 0 ? difficultyKeys[0] : "")

        let tabs = openEditorTabs.filter(key => key === metadataEditorKey || validKeys[key])
        if (tabs.length === 0 && preferredKey.length > 0)
            tabs = [preferredKey]
        else if (preferredKey.length > 0 && tabs.indexOf(preferredKey) < 0
                 && activeKey.length > 0 && preferredKey === activeKey)
            tabs = tabs.concat([preferredKey])

        const unchanged = tabs.length === openEditorTabs.length
            && tabs.every((key, index) => key === openEditorTabs[index])
        if (unchanged && (activeEditorKey.length > 0 || tabs.length === 0))
            return

        const previousActive = activeEditorKey
        openEditorTabs = tabs
        editorHistory = editorHistory.filter(key => tabs.indexOf(key) >= 0)
        if (tabs.indexOf(previousActive) >= 0)
            return

        setActiveEditor(tabs.indexOf(preferredKey) >= 0
            ? preferredKey
            : (editorHistory.length > 0
                ? editorHistory[editorHistory.length - 1]
                : (tabs.length > 0 ? tabs[0] : "")))
    }
}
