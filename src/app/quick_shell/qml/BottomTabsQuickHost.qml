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

    function syncNativeToastAnchor() {
        if (!controller)
            return
        const topLeft = root.mapToGlobal(0, 0)
        controller.syncBottomTabsToastAnchor(
            Math.round(topLeft.x),
            Math.round(topLeft.y),
            Math.round(root.width),
            Math.round(root.height),
            root.visible && root.width > 0 && root.height > 0 && controller.bottomTabsVisible
        )
    }

    readonly property int tabBarHeight: metric("bottomTabsTabBarHeight", 40)
    readonly property var visibleTabs: {
        // Labels come from the controller (which honours UiText::isChineseUi)
        // rather than QML's qsTr — qsTr requires a .ts translation file
        // that the project doesn't ship, so it'd just leak the English
        // literal. The controller mirrors MainWindow::bottomTabsFallbackLabel.
        const tabs = []
        if (controller && controller.timelineTabVisible)
            tabs.push({ "id": "timeline", "label": controller.timelineTabLabel })
        if (controller && controller.validationTabVisible)
            tabs.push({ "id": "validation", "label": controller.validationTabLabel })
        if (controller && controller.muriTabVisible)
            tabs.push({ "id": "muri", "label": controller.muriTabLabel })
        return tabs
    }

    color: tone("cardBg", "#ffffff")
    border.color: tone("border", "#d5e0ec")
    Component.onCompleted: syncNativeToastAnchor()
    onVisibleChanged: syncNativeToastAnchor()
    onXChanged: syncNativeToastAnchor()
    onYChanged: syncNativeToastAnchor()
    onWidthChanged: syncNativeToastAnchor()
    onHeightChanged: syncNativeToastAnchor()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Issue #2b Option B — restyle to match Qt's native QTabBar look:
        //  - Rectangular tabs (no pill rounding); subtle 2px top corner rounding.
        //  - Selected tab background MERGES with the content area below by
        //    sharing its colour and skipping the bottom border.
        //  - Unselected tabs have a visible bottom border (shown via the
        //    parent strip's bottom edge) and slightly recessed background.
        //  - Hover: subtle background shift.
        //  - Side / top borders only on selected to detach it from neighbours.
        // The visual is intentionally close to QTabWidget's North-tab-bar
        // styling so the legacy QSG path benefits from the same look once
        // the user enables it via the existing tab-bar code path.
        Rectangle {
            id: tabStrip
            Layout.fillWidth: true
            Layout.preferredHeight: root.tabBarHeight
            Layout.minimumHeight: root.tabBarHeight
            Layout.maximumHeight: root.tabBarHeight
            color: tone("cardAltBg", "#f3f5f8")

            // Bottom separator line under the strip. Drawn behind the tabs;
            // the selected tab paints over its segment to "punch through".
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: tone("border", "#d5e0ec")
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 0

                Repeater {
                    model: root.visibleTabs

                    delegate: Item {
                        id: tabItem
                        required property var modelData
                        // Bind directly to the controller property; using
                        // `parent.<x>` from inside child Rectangles can fail
                        // to re-evaluate reactively in some QML versions.
                        readonly property bool isCurrentTab: controller
                            && controller.bottomTabsCurrentTabId === modelData.id

                        Layout.preferredWidth: tabLabel.implicitWidth + 28
                        Layout.minimumWidth: tabLabel.implicitWidth + 28
                        Layout.fillHeight: true

                        // Tab body. Top-rounded rectangle: 2px radius at top
                        // corners only (achieved by pulling the rectangle 2px
                        // below the bottom edge so the bottom-radius vanishes
                        // beneath the strip's bottom edge / next-row content).
                        Rectangle {
                            anchors.left: tabItem.left
                            anchors.right: tabItem.right
                            anchors.top: tabItem.top
                            // Selected: extend 1px beyond the bottom so the
                            // strip's bottom-line border is overlapped and
                            // the tab visually merges with the content area.
                            anchors.bottom: tabItem.bottom
                            anchors.bottomMargin: tabItem.isCurrentTab ? -1 : 0
                            radius: 2
                            color: tabItem.isCurrentTab
                                ? tone("cardBg", "#ffffff")
                                : (tabMouseArea.containsMouse
                                    ? tone("menuHoverBg", "#eef5ff")
                                    : "transparent")
                            border.width: tabItem.isCurrentTab ? 1 : 0
                            border.color: tone("border", "#d5e0ec")
                        }

                        // Selected-tab "punch-through": redraw the bottom
                        // 1px in the content background colour so the
                        // strip's bottom separator looks broken under us.
                        Rectangle {
                            anchors.left: tabItem.left
                            anchors.leftMargin: 1
                            anchors.right: tabItem.right
                            anchors.rightMargin: 1
                            anchors.bottom: tabItem.bottom
                            height: 1
                            color: tone("cardBg", "#ffffff")
                            visible: tabItem.isCurrentTab
                        }

                        Text {
                            id: tabLabel

                            anchors.centerIn: tabItem
                            text: modelData.label
                            color: tabItem.isCurrentTab
                                ? tone("textPrimary", "#203040")
                                : tone("textSecondary", "#5a6878")
                            font.pixelSize: 13
                            // Native QTabBar uses regular weight; reserved
                            // semibold for emphasis is not how Qt's native
                            // tab labels look.
                            font.weight: Font.Normal
                        }

                        MouseArea {
                            id: tabMouseArea

                            anchors.fill: tabItem
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
                root.syncNativeToastAnchor()
            }
            onWidthChanged: {
                root.syncNativeBottomTabsSurface()
                root.syncNativeToastAnchor()
            }
            onHeightChanged: {
                root.syncNativeBottomTabsSurface()
                root.syncNativeToastAnchor()
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
