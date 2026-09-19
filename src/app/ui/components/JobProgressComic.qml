import QtQuick
import QtQuick.Controls
import MiaCode.UI

Item {
    id: root

    property var resources: null
    property bool active: false
    property bool chartExportActive: false
    property int slideIntervalMs: 5000
    property bool switching: false
    property int currentIndex: -1
    property int pendingIndex: -1
    property int pendingDirection: 0

    readonly property bool hasVisibleImage: visibleImage.status === Image.Ready
    readonly property bool canSwitch: root.chartExportActive && root.active
        && root.resources !== null && root.resources.resourceCount >= 2
        && !root.switching && incomingImage.status !== Image.Loading

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
        if (!root.resources || root.resources.usingFallback)
            return -1
        for (let index = 0; index < root.resources.resourceCount; ++index) {
            if (modelUrlAt(index).toString() === url.toString())
                return index
        }
        return -1
    }

    function syncFromModel() {
        if (!root.resources || root.switching)
            return
        const modelUrl = root.resources.currentImageUrl
        if (modelUrl.length === 0)
            return
        if (visibleImage.source.toString() === modelUrl.toString()) {
            root.currentIndex = modelIndexForUrl(modelUrl)
            updateTimer()
            return
        }
        root.pendingIndex = modelIndexForUrl(modelUrl)
        root.pendingDirection = 0
        incomingImage.source = modelUrl
        updateTimer()
    }

    function requestSlide(direction) {
        if (!root.canSwitch || (direction !== 1 && direction !== -1))
            return

        const count = root.resources.resourceCount
        if (count < 2)
            return

        const baseIndex = root.currentIndex >= 0 && root.currentIndex < count
            ? root.currentIndex : 0
        const targetIndex = (baseIndex + direction + count) % count
        const targetUrl = modelUrlAt(targetIndex)
        if (targetUrl.length === 0)
            return

        root.pendingIndex = targetIndex
        root.pendingDirection = direction
        incomingImage.x = direction > 0 ? root.width : -root.width
        incomingImage.source = targetUrl
        root.updateTimer()
    }

    function finishModelSync() {
        if (incomingImage.status !== Image.Ready)
            return
        visibleImage.source = incomingImage.source
        visibleImage.x = 0
        incomingImage.source = ""
        incomingImage.x = root.width
        if (root.pendingIndex >= 0)
            root.currentIndex = root.pendingIndex
        root.pendingIndex = -1
        root.pendingDirection = 0
        root.switching = false
        root.updateTimer()
    }

    function finishSlide() {
        visibleImage.source = incomingImage.source
        visibleImage.x = 0
        incomingImage.source = ""
        incomingImage.x = root.width
        root.currentIndex = root.pendingIndex
        root.pendingIndex = -1
        root.pendingDirection = 0
        root.switching = false
        root.updateTimer()
    }

    function handleIncomingReady() {
        if (root.pendingDirection === 0) {
            finishModelSync()
            return
        }
        if (root.switching)
            return
        root.switching = true
        root.updateTimer()
        slideAnimation.start()
    }

    function handleIncomingError() {
        if (root.resources)
            root.resources.useFallback()
        incomingImage.source = ""
        root.pendingIndex = -1
        root.pendingDirection = 0
        root.switching = false
        root.updateTimer()
    }

    function handleVisibleError() {
        if (!root.resources)
            return
        const fallbackUrl = root.resources.currentImageUrl
        if (visibleImage.source.toString() === fallbackUrl.toString()) {
            root.updateTimer()
            return
        }
        root.resources.useFallback()
        visibleImage.source = fallbackUrl
        root.currentIndex = -1
        root.updateTimer()
    }

    function stopBanner() {
        slideTimer.stop()
        if (slideAnimation.running)
            slideAnimation.stop()
        root.switching = false
        root.pendingIndex = -1
        root.pendingDirection = 0
        incomingImage.source = ""
        incomingImage.x = root.width
    }

    Component.onCompleted: syncFromModel()
    onActiveChanged: {
        if (root.active)
            syncFromModel()
        else
            stopBanner()
        updateTimer()
    }
    onChartExportActiveChanged: {
        if (!root.chartExportActive)
            stopBanner()
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
        border.color: Theme.colors.border.control
        border.width: 1
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
            z: 1
            onStatusChanged: {
                if (status === Image.Error)
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
            z: 2
            onStatusChanged: {
                if (status === Image.Ready)
                    root.handleIncomingReady()
                else if (status === Image.Error)
                    root.handleIncomingError()
                root.updateTimer()
            }
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

        IconButton {
            id: previousButton
            anchors.left: parent.left
            anchors.leftMargin: Theme.panelPadding
            anchors.verticalCenter: parent.verticalCenter
            glyph: "‹"
            tooltip: qsTrId("qml.previous_comic")
            Accessible.name: qsTrId("qml.previous_comic")
            Accessible.description: qsTrId("qml.show_previous_comic")
            visible: root.canSwitch
            enabled: root.canSwitch
            z: 3
            onClicked: root.requestSlide(-1)
        }

        IconButton {
            id: nextButton
            anchors.right: parent.right
            anchors.rightMargin: Theme.panelPadding
            anchors.verticalCenter: parent.verticalCenter
            glyph: "›"
            tooltip: qsTrId("qml.next_comic")
            Accessible.name: qsTrId("qml.next_comic")
            Accessible.description: qsTrId("qml.show_next_comic")
            visible: root.canSwitch
            enabled: root.canSwitch
            z: 3
            onClicked: root.requestSlide(1)
        }
    }

    Timer {
        id: slideTimer
        interval: root.slideIntervalMs
        repeat: true
        triggeredOnStart: false
        onTriggered: root.requestSlide(1)
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
