import QtQuick

Item {
    id: root

    property var resources: null
    property bool active: false
    property int slideIntervalMs: 8000
    property real imageWidth: 340
    property int pendingStep: 1

    readonly property bool canSwitch: root.active
        && root.resources !== null && root.resources.resourceCount >= 2
        && !slideAnimation.running

    visible: root.active && root.resources !== null
        && root.resources.resourceCount > 0

    readonly property real centeredImageX: (root.width - root.imageWidth) / 2

    function navigate(step) {
        if (!root.canSwitch)
            return
        root.pendingStep = step
        if (step < 0)
            root.resources.selectPreviousResource()
        else
            root.resources.selectNextResource()
        incomingImage.source = root.resources.currentImageUrl
        incomingImage.x = step < 0 ? -root.imageWidth : root.width
        slideAnimation.start()
    }

    function selectPrevious() {
        root.navigate(-1)
    }

    function selectNext() {
        root.navigate(1)
    }

    function finishNavigation() {
        currentImage.source = incomingImage.source
        currentImage.x = root.centeredImageX
        incomingImage.source = ""
        incomingImage.x = root.pendingStep < 0 ? -root.imageWidth : root.width
    }

    onActiveChanged: {
        if (root.active && root.resources) {
            root.resources.refresh()
            currentImage.source = root.resources.currentImageUrl
            currentImage.x = root.centeredImageX
        } else {
            slideAnimation.stop()
            currentImage.x = root.centeredImageX
            incomingImage.source = ""
        }
    }

    onWidthChanged: {
        if (!slideAnimation.running)
            currentImage.x = root.centeredImageX
    }

    Image {
        id: currentImage
        width: root.imageWidth
        height: root.height
        y: 0
        x: root.centeredImageX
        fillMode: Image.PreserveAspectFit
    }

    Image {
        id: incomingImage
        width: root.imageWidth
        height: root.height
        y: 0
        x: root.width
        fillMode: Image.PreserveAspectFit
    }

    Timer {
        interval: root.slideIntervalMs
        repeat: true
        running: root.canSwitch && root.visible
        onTriggered: root.selectNext()
    }

    ParallelAnimation {
        id: slideAnimation

        NumberAnimation {
            target: currentImage
            property: "x"
            to: root.pendingStep < 0 ? root.width : -root.imageWidth
            duration: 500
            easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            target: incomingImage
            property: "x"
            to: root.centeredImageX
            duration: 500
            easing.type: Easing.InOutCubic
        }
        onFinished: root.finishNavigation()
    }
}
