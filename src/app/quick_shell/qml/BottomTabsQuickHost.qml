import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var controller: null
    property var paletteMap: ({})
    property var metricsMap: ({})

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    function usesNativeBottomTabsSurface() {
        return controller
            && (controller.bottomTabsCurrentTabId === "validation"
                || controller.bottomTabsCurrentTabId === "muri")
    }

    function syncNativeBottomTabsSurface() {
        if (!controller || !usesNativeBottomTabsSurface())
            return
        controller.syncBottomTabsSurfaceSize(contentHost.width, contentHost.height)
    }

    readonly property int tabBarHeight: metric("bottomTabsTabBarHeight", 40)
    readonly property var visibleTabs: {
        const tabs = []
        if (controller && controller.timelineTabVisible)
            tabs.push({ "id": "timeline", "label": qsTr("Timeline") })
        if (controller && controller.validationTabVisible)
            tabs.push({ "id": "validation", "label": qsTr("Syntax Check") })
        if (controller && controller.muriTabVisible)
            tabs.push({ "id": "muri", "label": qsTr("Muri Check") })
        return tabs
    }

    color: tone("cardBg", "#ffffff")
    border.color: tone("border", "#d5e0ec")

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.tabBarHeight
            Layout.minimumHeight: root.tabBarHeight
            Layout.maximumHeight: root.tabBarHeight
            color: tone("cardAltBg", "#f7f9fc")
            border.color: tone("border", "#d5e0ec")

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Repeater {
                    model: root.visibleTabs

                    delegate: Rectangle {
                        required property var modelData

                        Layout.preferredWidth: tabLabel.implicitWidth + 26
                        Layout.minimumWidth: tabLabel.implicitWidth + 26
                        Layout.fillHeight: true
                        radius: 8
                        color: controller && controller.bottomTabsCurrentTabId === modelData.id
                            ? tone("accent", "#2e77d0")
                            : (tabMouseArea.containsMouse
                                ? tone("menuHoverBg", "#eef5ff")
                                : "transparent")
                        border.color: controller && controller.bottomTabsCurrentTabId === modelData.id
                            ? tone("accent", "#2e77d0")
                            : "transparent"

                        Text {
                            id: tabLabel

                            anchors.centerIn: parent
                            text: modelData.label
                            color: controller && controller.bottomTabsCurrentTabId === modelData.id
                                ? tone("accentText", "#ffffff")
                                : tone("textPrimary", "#203040")
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }

                        MouseArea {
                            id: tabMouseArea

                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (controller)
                                    controller.setBottomTabsCurrentTabId(modelData.id)
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        Item {
            id: contentHost

            Layout.fillWidth: true
            Layout.fillHeight: true
            Component.onCompleted: {
                root.syncNativeBottomTabsSurface()
            }
            onWidthChanged: {
                root.syncNativeBottomTabsSurface()
            }
            onHeightChanged: {
                root.syncNativeBottomTabsSurface()
            }

            TimelineTabSurface {
                anchors.fill: parent
                visible: controller && controller.bottomTabsCurrentTabId === "timeline"
                controller: root.controller
                paletteMap: root.paletteMap
                metricsMap: root.metricsMap
            }

            WindowContainer {
                anchors.fill: parent
                visible: root.usesNativeBottomTabsSurface()
                window: controller ? controller.bottomTabsWindow : null
                onVisibleChanged: {
                    if (visible)
                        root.syncNativeBottomTabsSurface()
                }
            }
        }
    }
}
