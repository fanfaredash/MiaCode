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
    property bool visibleCommitPending: false
    // Identity of the job the comic surface is serving. A new begin() always
    // raises it, so re-initialization does not depend on a task-type edge.
    property real taskToken: 0
    property real initializedTaskToken: 0
    property bool resourcesScanned: false
    property bool scanningResources: false
    visible: root.active && root.chartExportActive && root.hasResources

    readonly property bool hasResources: root.resources !== null
        && root.resources.resourceCount > 0
    readonly property bool hasVisibleImage: visibleImage.status === Image.Ready
    readonly property bool canSwitch: root.chartExportActive && root.active
        && root.resources !== null && root.resources.resourceCount >= 2
        && !root.switching && incomingImage.status !== Image.Loading

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

    // The timer and the next button share this single entry point. The model
    // picks a resource other than the current one, so a rotation never repeats
    // the picture already on screen.
    function selectRandomNext() {
        if (!root.canSwitch || !root.resources)
            return

        root.resources.selectRandomResource()
    }

    function updateTimer() {
        slideTimer.running = root.visible && root.active && root.chartExportActive
            && root.resources !== null && root.resources.resourceCount >= 2
            && !root.switching && incomingImage.status !== Image.Loading
    }

    function startImageTransition() {
        incomingImage.x = root.width
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
        if (visibleImage.source.toString() === modelUrl.toString()) {
            root.currentIndex = modelIndex
            updateTimer()
            return
        }
        root.pendingIndex = modelIndex
        incomingImage.x = root.width
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
        root.startImageTransition()
    }

    function handleIncomingError() {
        if (!root.active || !root.chartExportActive) {
            incomingImage.source = ""
            root.pendingIndex = -1
            root.switching = false
            root.updateTimer()
            return
        }
        incomingImage.source = ""
        root.pendingIndex = -1
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

    // Initializes the carousel once per job. Keying on the job token rather than
    // on a chartExportActive edge also covers a chart export that replaces
    // another one without an idle gap, where the task type never changes and no
    // edge would arrive. Re-entry therefore still clears the previous image,
    // animation, and pending state before selecting the new one.
    function ensureChartExportInitialized() {
        if (!root.active || !root.chartExportActive || root.taskToken <= 0)
            return
        if (root.initializedTaskToken === root.taskToken)
            return
        root.initializedTaskToken = root.taskToken
        stopBanner()
        root.ensureResourcesScanned()

        if (!root.resources || root.resources.resourceCount === 0)
            return
        if (root.resources.resourceCount > 1)
            root.resources.selectRandomResource()
        else
            root.resources.selectResource(0)
        root.syncFromModel()
    }

    Component.onCompleted: syncFromModel()
    // A new job always re-initializes, including one that replaces a running
    // chart export without an idle gap.
    onTaskTokenChanged: ensureChartExportInitialized()
    onActiveChanged: {
        if (root.active) {
            root.ensureChartExportInitialized()
            root.syncFromModel()
        } else {
            stopBanner()
        }
        updateTimer()
    }
    onChartExportActiveChanged: {
        if (root.chartExportActive) {
            root.ensureChartExportInitialized()
            root.syncFromModel()
        } else {
            stopBanner()
        }
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
        function onResourcesChanged() { root.syncFromModel() }
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

    // The outgoing image always exits to the left while the freshly picked one
    // enters from the right.
    ParallelAnimation {
        id: slideAnimation
        NumberAnimation {
            target: visibleImage
            property: "x"
            to: -root.width
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
