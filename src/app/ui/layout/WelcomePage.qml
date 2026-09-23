pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var documentSession

    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)

    property var recentDocuments: []

    function refreshRecentDocuments() {
        recentDocuments = documentSession ? documentSession.recentDocuments() : []
    }

    color: Theme.surfaceColor(Theme.colors.background.panel)

    onVisibleChanged: {
        if (visible)
            refreshRecentDocuments()
    }
    Component.onCompleted: refreshRecentDocuments()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: welcomeColumn.implicitHeight + 96
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        clip: true

        ColumnLayout {
            id: welcomeColumn

            width: Math.min(620, Math.max(320, parent.width - 64))
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(48, (parent.height - implicitHeight) / 2)
            spacing: 22

            Text {
                Layout.fillWidth: true
                text: "MiaCode"
                color: Theme.colors.text.heading
                font.family: Theme.uiFont
                font.pixelSize: 36
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                AppButton {
                    text: qsTrId("action.new")
                    emphasized: true
                    onClicked: root.newRequested()
                }

                AppButton {
                    text: qsTrId("action.open")
                    onClicked: root.openRequested()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 10
                height: 1
                color: Theme.colors.border.normal
            }

            Text {
                Layout.fillWidth: true
                text: qsTrId("cover.open_recent")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.weight: Font.DemiBold
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    Layout.fillWidth: true
                    visible: root.recentDocuments.length === 0
                    text: qsTrId("qml.no_recent_documents")
                    color: Theme.colors.text.disabled
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.secondaryFontSize
                }

                Repeater {
                    model: root.recentDocuments.slice(0, 8)

                    delegate: Item {
                        id: recentRow

                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: 48

                        HoverChrome {
                            anchors.fill: parent
                            baseColor: "transparent"
                            stateColors: Theme.colors.listState
                            hovered: recentMouse.containsMouse
                            pressed: recentMouse.pressed
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 52
                            spacing: 2

                            Text {
                                width: parent.width
                                text: recentRow.modelData.label
                                color: Theme.colors.text.primary
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize
                                elide: Text.ElideMiddle
                            }

                            Text {
                                width: parent.width
                                text: recentRow.modelData.path
                                color: Theme.colors.text.disabled
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.secondaryFontSize
                                elide: Text.ElideMiddle
                            }
                        }

                        MouseArea {
                            id: recentMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openRecentRequested(recentRow.modelData.path)
                        }

                        IconButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            z: 1
                            compact: true
                            iconSource: Qt.resolvedUrl("icons/remove.svg")
                            tooltip: qsTrId("metadata.delete")
                            onClicked: {
                                root.documentSession.removeRecentDocument(recentRow.modelData.path)
                                root.refreshRecentDocuments()
                            }
                        }
                    }
                }
            }
        }
    }
}
