import QtQuick

// Background hit area for custom window chrome. Interactive controls painted
// above it retain their own pointer handling; exposed chrome moves or zooms the
// native window with the platform window-system operations.
Item {
    id: root

    required property var hostWindow

    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active)
                root.hostWindow.startSystemMove()
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: {
            if (root.hostWindow.visibility === Window.Maximized)
                root.hostWindow.showNormal()
            else
                root.hostWindow.showMaximized()
        }
    }
}
