pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    required property var hostWindow
    readonly property int margin: 5
    readonly property int cornerSpan: 16

    anchors.fill: parent
    z: 9999
    enabled: root.hostWindow.visibility !== Window.Maximized && root.hostWindow.visibility !== Window.FullScreen

    // Corners
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.cornerSpan
        height: root.cornerSpan
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.TopEdge | Qt.LeftEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.cornerSpan
        height: root.cornerSpan
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.TopEdge | Qt.RightEdge)
    }

    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.cornerSpan
        height: root.cornerSpan
        cursorShape: Qt.SizeBDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.BottomEdge | Qt.LeftEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.cornerSpan
        height: root.cornerSpan
        cursorShape: Qt.SizeFDiagCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.BottomEdge | Qt.RightEdge)
    }

    // Edges
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: root.cornerSpan
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.cornerSpan
        width: root.margin
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.LeftEdge)
    }

    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: root.cornerSpan
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.cornerSpan
        width: root.margin
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.RightEdge)
    }

    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: root.cornerSpan
        anchors.right: parent.right
        anchors.rightMargin: root.cornerSpan
        height: root.margin
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.TopEdge)
    }

    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.leftMargin: root.cornerSpan
        anchors.right: parent.right
        anchors.rightMargin: root.cornerSpan
        height: root.margin
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.hostWindow.startSystemResize(Qt.BottomEdge)
    }
}
