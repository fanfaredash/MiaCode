import QtQuick
import QtQuick.Controls
import MiaCode.UI

Item {
    id: root

    clip: true

    property var resources: null
    property bool active: false
    property bool chartExportActive: false
    property int slideIntervalMs: 5000
    property bool switching: false
    property int currentIndex: -1
    property int pendingIndex: -1
    property int pendingDirection: 0
    property bool visibleCommitPending: false
    property var navigationHistory: []
    property int navigationCursor: -1
    property bool chartExportInitialized: false
    property bool resourcesScanned: false
    property bool scanningResources: false
    visible: root.active && root.chartExportActive && root.hasResources

    readonly property bool hasResources: root.resources !== null
        && root.resources.resourceCount > 0
    readonly property bool hasVisibleImage: visibleImage.status === Image.Ready
    readonly property bool canGoBack: root.navigationCursor > 0
    readonly property bool canSwitch: root.chartExportActive && root.active
        && root.resources !== null && root.resources.resourceCount >= 2
        && !root.switching && incomingImage.status !== Image.Loading

    function clearNavigationHistory() {
        root.navigationHistory = []
        root.navigationCursor = -1
    }

    function normalizeNavigationHistory() {
        const count = root.resources ? root.resources.resourceCount : 0
        const validHistory = []
        const oldCursor = root.navigationCursor
        let normalizedCursor = -1
        for (let historyIndex = 0; historyIndex < root.navigationHistory.length; ++historyIndex) {
            const index = root.navigationHistory[historyIndex]
            if (index >= 0 && index < count) {
                if (historyIndex === oldCursor)
                    normalizedCursor = validHistory.length
                validHistory.push(index)
            }
        }
        root.navigationHistory = validHistory
        root.navigationCursor = normalizedCursor >= 0
            ? normalizedCursor : Math.min(Math.max(oldCursor, -1), validHistory.length - 1)
    }

    function currentResourceIndex() {
        if (!root.resources)
            return -1

        return root.modelIndexForUrl(root.resources.currentImageUrl)
    }

    function recordCurrentResource(index) {
        if (!root.resources || index < 0 || index >= root.resources.resourceCount)
            return
        root.normalizeNavigationHistory()
        if (root.navigationCursor >= 0
                && root.navigationHistory[root.navigationCursor] === index) {
            return
        }

        const historyPrefix = root.navigationCursor >= 0
            ? root.navigationHistory.slice(0, root.navigationCursor + 1)
            : []
        root.navigationHistory = historyPrefix.concat([index])
        root.navigationCursor = root.navigationHistory.length - 1
    }

    function appendRandomResource() {
        if (!root.canSwitch || !root.resources
                || root.resources.resourceCount < 2) {
            return
        }

        root.pendingDirection = 1
        root.resources.selectRandomResource()
    }

    function selectNextFromHistoryOrRandom() {
        if (!root.canSwitch || !root.resources)
            return

        root.normalizeNavigationHistory()
        if (root.navigationCursor + 1 < root.navigationHistory.length) {
            const nextIndex = root.navigationHistory[root.navigationCursor + 1]
            if (nextIndex < 0 || nextIndex >= root.resources.resourceCount)
                return
            root.navigationCursor += 1
            root.pendingDirection = 1
            root.resources.selectResource(nextIndex)
            return
        }

        root.appendRandomResource()
    }

    function selectRandomNext() {
        root.selectNextFromHistoryOrRandom()
    }

    function selectPreviousFromHistory() {
        if (!root.canSwitch || !root.canGoBack || !root.resources)
            return

        root.normalizeNavigationHistory()
        const previousIndex = root.navigationHistory[root.navigationCursor - 1]

        if (previousIndex < 0 || previousIndex >= root.resources.resourceCount)
            return
        if (previousIndex === root.currentResourceIndex())
            return

        root.navigationCursor -= 1
        root.pendingDirection = -1
        root.resources.selectResource(previousIndex)
    }

    function updateTimer() {
        slideTimer.running = root.visible && root.active && root.chartExportActive
            && root.resources !== null && root.resources.resourceCount >= 2
            && !root.switching && incomingImage.status !== Image.Loading
    }

    function modelUrlAt(index) {
        if (!root.resources || index < 0 || index >= root.resources.resourceCount)
            return ""
        return root.resources.imageUrlAt(index)
    }

    function modelIndexForUrl(url) {
        if (!root.resources)
            return -1
        for (let index = 0; index < root.resources.resourceCount; ++index) {
            if (modelUrlAt(index).toString() === url.toString())
                return index
        }
        return -1
    }

    function startImageTransition(direction) {
        if (direction !== 1 && direction !== -1)
            return
        root.pendingDirection = direction
        incomingImage.x = direction < 0 ? -root.width : root.width
        root.switching = true
        root.updateTimer()
        slideAnimation.start()
    }

    function syncFromModel() {
        if (!root.active || !root.chartExportActive || !root.resources || root.switching
                || root.scanningResources)
            return
        const modelUrl = root.resources.currentImageUrl
        if (modelUrl.length === 0)
            return
        const modelIndex = root.modelIndexForUrl(modelUrl)
        root.recordCurrentResource(modelIndex)
        if (visibleImage.source.toString() === modelUrl.toString()) {
            root.currentIndex = modelIndex
            updateTimer()
            return
        }
        const transitionDirection = root.pendingDirection
        root.pendingIndex = modelIndex
        root.pendingDirection = transitionDirection
        incomingImage.x = transitionDirection < 0 ? -root.width : root.width
        incomingImage.source = modelUrl
        updateTimer()
    }

    function finishVisibleCommit() {
        if (!root.visibleCommitPending || visibleImage.status !== Image.Ready)
            return
        root.visibleCommitPending = false
        incomingImage.source = ""
        incomingImage.x = root.width
        if (root.pendingIndex >= 0)
            root.currentIndex = root.pendingIndex
        root.pendingIndex = -1
        root.pendingDirection = 0
        root.switching = false
        root.updateTimer()
    }

    function stageVisibleCommit() {
        if (incomingImage.status !== Image.Ready)
            return
        root.visibleCommitPending = true
        visibleImage.source = incomingImage.source
        visibleImage.x = 0
    }

    function finishSlide() {
        root.stageVisibleCommit()
    }

    function handleIncomingReady() {
        if (!root.active || !root.chartExportActive) {
            incomingImage.source = ""
            root.pendingIndex = -1
            root.pendingDirection = 0
            root.switching = false
            root.updateTimer()
            return
        }
        if (!root.hasVisibleImage) {
            stageVisibleCommit()
            return
        }
        if (root.switching)
            return
        root.startImageTransition(root.pendingDirection === 0 ? 1 : root.pendingDirection)
    }

    function handleIncomingError() {
        if (!root.active || !root.chartExportActive) {
            incomingImage.source = ""
            root.pendingIndex = -1
            root.pendingDirection = 0
            root.switching = false
            root.updateTimer()
            return
        }
        incomingImage.source = ""
        root.pendingIndex = -1
        root.pendingDirection = 0
        root.switching = false
        root.updateTimer()
    }

    function handleVisibleError() {
        if (!root.active || !root.chartExportActive || !root.resources)
            return
        if (root.visibleCommitPending) {
            root.visibleCommitPending = false
            incomingImage.source = ""
            root.pendingIndex = -1
            root.pendingDirection = 0
            root.switching = false
        }
        visibleImage.source = ""
        root.currentIndex = -1
        root.updateTimer()
    }

    function stopBanner() {
        slideTimer.stop()
        if (slideAnimation.running)
            slideAnimation.stop()
        root.visibleCommitPending = false
        root.switching = false
        root.pendingIndex = -1
        root.pendingDirection = 0
        incomingImage.source = ""
        incomingImage.x = root.width
        visibleImage.source = ""
        root.currentIndex = -1
    }

    // The comic directory is probed at most once per session, the first time the
    // chart-export comic area is shown, so startup never scans it. The scan's own
    // notifications are held off the carousel state machine so the first image is
    // only committed by the explicit selection below.
    function ensureResourcesScanned() {
        if (root.resourcesScanned || !root.resources)
            return
        root.resourcesScanned = true
        root.scanningResources = true
        root.resources.refresh()
        root.scanningResources = false
    }

    function initializeChartExport() {
        root.clearNavigationHistory()
        root.currentIndex = -1
        root.pendingDirection = 0
        root.ensureResourcesScanned()

        if (!root.resources || root.resources.resourceCount === 0)
            return
        if (root.resources.resourceCount > 1)
            root.resources.selectRandomResource()
        else
            root.resources.selectResource(0)
        root.recordCurrentResource(root.currentResourceIndex())
        if (root.navigationHistory.length > 0)
            root.navigationCursor = 0
    }

    Component.onCompleted: syncFromModel()
    onActiveChanged: {
        if (root.active) {
            if (root.chartExportActive && !root.chartExportInitialized) {
                root.chartExportInitialized = true
                root.initializeChartExport()
            }
            root.syncFromModel()
        } else {
            root.chartExportInitialized = false
            root.clearNavigationHistory()
            stopBanner()
        }
        updateTimer()
    }
    onChartExportActiveChanged: {
        if (!root.chartExportActive) {
            root.chartExportInitialized = false
            root.clearNavigationHistory()
            stopBanner()
            return
        }
        if (!root.chartExportInitialized) {
            root.chartExportInitialized = true
            root.initializeChartExport()
        }
        if (root.active)
            root.syncFromModel()
        updateTimer()
    }
    onVisibleChanged: updateTimer()
    onWidthChanged: {
        if (!root.switching)
            incomingImage.x = root.width
        updateTimer()
    }

    Connections {
        target: root.resources
        function onCurrentChanged() { root.syncFromModel() }
        function onResourcesChanged() {
            root.normalizeNavigationHistory()
            root.syncFromModel()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colors.background.control
        radius: Theme.controlRadius
        border.color: Theme.colors.text.heading
        border.width: 0
        clip: true

        Image {
            id: visibleImage
            objectName: "jobProgressComicVisibleImage"
            width: parent.width
            height: parent.height
            y: 0
            x: 0
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            cache: false
            retainWhileLoading: true
            z: 1
            onStatusChanged: {
                if (status === Image.Ready)
                    root.finishVisibleCommit()
                else if (status === Image.Error)
                    root.handleVisibleError()
            }
        }

        Image {
            id: incomingImage
            objectName: "jobProgressComicIncomingImage"
            width: parent.width
            height: parent.height
            y: 0
            x: root.width
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            cache: false
            retainWhileLoading: true
            z: 2
            onStatusChanged: {
                if (status === Image.Ready)
                    root.handleIncomingReady()
                else if (status === Image.Error)
                    root.handleIncomingError()
                root.updateTimer()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Theme.controlRadius
            border.color: Theme.colors.text.heading
            border.width: 1
            z: 2.5
            enabled: false
        }

        Text {
            anchors.centerIn: parent
            visible: !root.hasVisibleImage && incomingImage.status !== Image.Loading
            text: qsTrId("qml.comic_resource_unavailable")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Accessible.name: text
            Accessible.description: qsTrId("qml.comic_resource_unavailable")
        }

    }

    Timer {
        id: slideTimer
        interval: root.slideIntervalMs
        repeat: true
        triggeredOnStart: false
        onTriggered: root.selectRandomNext()
    }

    ParallelAnimation {
        id: slideAnimation
        NumberAnimation {
            target: visibleImage
            property: "x"
            to: root.pendingDirection > 0 ? -root.width : root.width
            duration: 180
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: incomingImage
            property: "x"
            to: 0
            duration: 180
            easing.type: Easing.OutCubic
        }
        onFinished: root.finishSlide()
    }
}
