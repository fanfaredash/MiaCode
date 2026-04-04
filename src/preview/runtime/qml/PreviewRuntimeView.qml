import QtQuick
import MiaCode.Preview

Rectangle {
    color: "#1F2833"
    implicitWidth: 480
    implicitHeight: 480

    PreviewQuickSceneRoot {
        id: previewQuickSceneRoot
        objectName: "previewQuickSceneRoot"
        anchors.fill: parent
        z: 0
    }

    PreviewQuickItem {
        id: previewLegacyBridgeItem
        objectName: "previewQuickItem"
        anchors.fill: parent
        z: 1
    }

    PreviewQuickHudLayer {
        id: previewQuickHudLayer
        objectName: "previewQuickHudLayer"
        anchors.fill: parent
        z: 2
    }
}
