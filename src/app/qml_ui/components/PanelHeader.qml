import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    property string title
    property bool showMore: false
    property bool sidebarTitle: false
    default property alias trailing: trailingRow.data

    readonly property real titleInkTop: {
        const sample = root.title.length > 0 ? root.title : "汉"
        const rect = titleMetrics.tightBoundingRect(sample)
        return Math.max(0, titleMetrics.ascent + rect.y)
    }

    implicitHeight: root.sidebarTitle
                    ? Math.max(Math.ceil(titleLabel.y + titleLabel.implicitHeight + Theme.chromeInsetY),
                               trailingRow.implicitHeight)
                    : 34
    color: "transparent"

    FontMetrics {
        id: titleMetrics
        font: titleLabel.font
    }

    Text {
        id: titleLabel

        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.top: root.sidebarTitle ? parent.top : undefined
        anchors.topMargin: root.sidebarTitle
                           ? Math.max(0, Theme.activityIconTop - root.titleInkTop)
                           : 0
        anchors.verticalCenter: root.sidebarTitle ? undefined : parent.verticalCenter
        text: root.title
        color: root.sidebarTitle ? Theme.colors.text.heading : Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: root.sidebarTitle ? Theme.headingFontSize : Theme.uiFontSize
        font.weight: root.sidebarTitle ? Font.DemiBold : Font.Normal
        verticalAlignment: Text.AlignTop
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showMore && trailingRow.children.length === 0
        text: "..."
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }
}
