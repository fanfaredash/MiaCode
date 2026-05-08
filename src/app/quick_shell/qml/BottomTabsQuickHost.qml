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

    function isChineseUi() {
        return paletteMap && paletteMap["isChineseUi"] === true
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

                // Timeline-tab follow toggles. Live in the tab strip
                // (rather than inside TimelineTabSurface's cramped
                // header band) so there's room for both checkboxes
                // and so the visible state is driven by ONE QML
                // control per toggle — no QSG-layer / DComp dual
                // rendering path that previously dropped the
                // progress-follow checkbox in QSG-only mode. Only
                // surfaced when the timeline tab is the current
                // tab; the validation / muri tabs leave the right
                // side empty as before.
                CheckBox {
                    id: cursorFollowCheck

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: tabStrip.height - 6
                    Layout.rightMargin: 4
                    visible: controller && controller.bottomTabsCurrentTabId === "timeline"
                    hoverEnabled: true
                    spacing: 4
                    text: root.isChineseUi() ? "光标跟随" : "Cursor Follow"
                    checked: controller && controller.timelineStateBridge
                        ? controller.timelineStateBridge.followPreviewEnabled
                        : false

                    indicator: Rectangle {
                        implicitWidth: 14
                        implicitHeight: 14
                        x: 0
                        y: (cursorFollowCheck.height - height) / 2
                        radius: 3
                        color: cursorFollowCheck.checked
                            ? root.tone("accent", "#60a5fa")
                            : root.tone("cardBg", "#1f2937")
                        border.width: 1
                        border.color: cursorFollowCheck.checked
                            ? root.tone("accent", "#60a5fa")
                            : (cursorFollowCheck.hovered
                                ? root.tone("accent", "#60a5fa")
                                : root.tone("border", "#475569"))

                        Canvas {
                            anchors.fill: parent
                            visible: cursorFollowCheck.checked

                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = root.tone("accentText", "#ffffff")
                                ctx.lineWidth = 1.8
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                ctx.beginPath()
                                ctx.moveTo(width * 0.24, height * 0.55)
                                ctx.lineTo(width * 0.44, height * 0.74)
                                ctx.lineTo(width * 0.78, height * 0.28)
                                ctx.stroke()
                            }
                        }
                    }

                    contentItem: Text {
                        text: cursorFollowCheck.text
                        color: root.tone("textPrimary", "#203040")
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: cursorFollowCheck.indicator.width + cursorFollowCheck.spacing
                    }

                    onClicked: {
                        if (controller && controller.timelineStateBridge) {
                            controller.timelineStateBridge.followPreviewEnabled = checked
                        }
                        if (controller) {
                            controller.timelineFollowPreviewToggled(checked)
                        }
                    }
                }

                CheckBox {
                    id: progressFollowCheck

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: tabStrip.height - 6
                    Layout.rightMargin: 8
                    visible: controller && controller.bottomTabsCurrentTabId === "timeline"
                    hoverEnabled: true
                    spacing: 4
                    text: root.isChineseUi() ? "进度跟随" : "Progress Follow"
                    checked: controller && controller.timelineStateBridge
                        ? controller.timelineStateBridge.followProgressEnabled
                        : false

                    indicator: Rectangle {
                        implicitWidth: 14
                        implicitHeight: 14
                        x: 0
                        y: (progressFollowCheck.height - height) / 2
                        radius: 3
                        color: progressFollowCheck.checked
                            ? root.tone("accent", "#60a5fa")
                            : root.tone("cardBg", "#1f2937")
                        border.width: 1
                        border.color: progressFollowCheck.checked
                            ? root.tone("accent", "#60a5fa")
                            : (progressFollowCheck.hovered
                                ? root.tone("accent", "#60a5fa")
                                : root.tone("border", "#475569"))

                        Canvas {
                            anchors.fill: parent
                            visible: progressFollowCheck.checked

                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.strokeStyle = root.tone("accentText", "#ffffff")
                                ctx.lineWidth = 1.8
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"
                                ctx.beginPath()
                                ctx.moveTo(width * 0.24, height * 0.55)
                                ctx.lineTo(width * 0.44, height * 0.74)
                                ctx.lineTo(width * 0.78, height * 0.28)
                                ctx.stroke()
                            }
                        }
                    }

                    contentItem: Text {
                        text: progressFollowCheck.text
                        color: root.tone("textPrimary", "#203040")
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: progressFollowCheck.indicator.width + progressFollowCheck.spacing
                    }

                    onClicked: {
                        if (controller && controller.timelineStateBridge) {
                            controller.timelineStateBridge.followProgressEnabled = checked
                        }
                        if (controller) {
                            controller.timelineFollowProgressToggled(checked)
                        }
                    }
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
