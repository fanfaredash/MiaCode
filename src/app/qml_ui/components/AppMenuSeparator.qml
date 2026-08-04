import QtQuick
import QtQuick.Controls
import MiaCode.UI

MenuSeparator {
    id: root

    topPadding: 7
    bottomPadding: 7
    leftPadding: 6
    rightPadding: 6

    contentItem: Rectangle {
        implicitWidth: 168
        implicitHeight: 1
        color: Theme.colors.border.normal
    }
}
