pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI
import MiaCode.Timeline

Rectangle {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var preferences
    required property var commands
    required property var shellController
    property real zoomMenuClosedAt: 0
    property real brightnessMenuClosedAt: 0
    signal analysisRowActivated(int difficultyId, var revision, int line, int column, int endColumn, double second)

    color: Theme.colors.background.surface
    clip: true
    readonly property int contentTopMargin: 4

    BottomTabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        documentSession: root.documentSession
        analysisSession: root.analysisSession
        shellController: root.shellController
    }

    TimelineQuickItem {
        id: timelineItem

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.shellController.bottomTabsCurrentTabId === "timeline"
        enabled: visible
        stateBridge: root.shellController.timelineStateBridge
        // Keep header labels and markers clear of the QML controls.
        headerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerRightLimit: Math.max(0, brightnessButton.x - 2)
        headerMarkerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerMarkerRightLimit: Math.max(0, brightnessButton.x - 2)
        onHeaderNavigateRequested: second => root.shellController.timelineHeaderNavigate(second)
        onTimelineWheelNavigateRequested: second => root.shellController.timelineWheelNavigate(second)
        onCenterNavigateRequested: second => root.shellController.timelineCenterNavigate(second)
        onTimelineDragStarted: root.shellController.timelineDragStarted()
        onTimelineDragFinished: second => root.shellController.timelineDragFinished(second)
        onTimelineUserInteractionStarted: root.shellController.timelineUserInteractionStarted()
        onTimelineSurfaceReady: root.shellController.noteTimelineSurfaceReady()
        onFollowPreviewToggled: enabled => root.shellController.timelineFollowPreviewToggled(enabled)
        onPreviewPlayPauseRequested: root.shellController.togglePreviewPlayback()
    }

    AppDropDownButton {
        id: zoomButton

        x: Math.round(4 * timelineItem.headerScale)
        y: timelineItem.y + Math.max(0, (timelineItem.timelineTop - height) / 2)
        height: Math.min(implicitHeight, Math.max(1, timelineItem.timelineTop))
        visible: timelineItem.visible
        text: qsTr("%1%").arg(Math.round(root.shellController.timelineStateBridge
            ? root.shellController.timelineStateBridge.zoomScale * 100
            : 50))
        sizeToLabels: zoomMenu.zoomLabels
        tooltip: qsTr("时间轴缩放")
        expanded: zoomMenu.visible
        Accessible.description: qsTr("打开时间轴缩放预设")
        onClicked: {
            if (zoomMenu.visible) {
                zoomMenu.close()
                return
            }
            if (Date.now() - root.zoomMenuClosedAt < 200)
                return
            zoomMenu.openAt(zoomButton)
        }
    }

    IconButton {
        id: brightnessButton

        width: Math.max(28, Math.round(28 * timelineItem.headerScale))
        height: Math.max(24, Math.round(24 * timelineItem.headerScale))
        x: Math.max(zoomButton.x + zoomButton.width + 8, parent.width - width - 8)
        y: timelineItem.y + Math.max(0, (timelineItem.timelineTop - height) / 2)
        visible: timelineItem.visible
        iconSource: Qt.resolvedUrl("icons/settings.svg")
        iconWidth: Math.max(16, Math.round(16 * timelineItem.headerScale))
        iconHeight: iconWidth
        tooltip: qsTr("时间轴亮度")
        active: brightnessMenu.visible
        Accessible.description: qsTr("打开波形和小节线亮度设置")
        onClicked: {
            if (brightnessMenu.visible) {
                brightnessMenu.close()
                return
            }
            if (Date.now() - root.brightnessMenuClosedAt < 200)
                return
            brightnessMenu.openAt(brightnessButton)
        }
    }

    TimelineZoomMenu {
        id: zoomMenu
        stateBridge: root.shellController.timelineStateBridge
        onClosed: root.zoomMenuClosedAt = Date.now()
    }

    TimelineBrightnessMenu {
        id: brightnessMenu
        stateBridge: root.shellController.timelineStateBridge
        onClosed: root.brightnessMenuClosedAt = Date.now()
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.shellController.bottomTabsCurrentTabId === "validation"

        Row {
            id: summaryRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 12

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.analysisSession.pending
                    ? qsTr("正在分析…")
                    : qsTr("%1 个错误，%2 个警告，已解析 %3 个物件")
                        .arg(root.documentSession.syntaxErrorCount)
                        .arg(root.documentSession.syntaxWarningCount)
                        .arg(root.documentSession.parsedNoteCount)
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
            }

            AppButton {
                text: qsTr("重新检查")
                onClicked: root.commands.validateDocument()
            }
        }

        ListView {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: summaryRow.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: 8
            clip: true
            model: root.analysisSession.validationRows

            delegate: ItemDelegate {
                id: issueDelegate
                required property var modelData
                width: ListView.view.width
                height: 34
                hoverEnabled: true
                leftPadding: 10
                rightPadding: 10
                onClicked: root.analysisSession.activateRow(modelData)

                contentItem: Row {
                    spacing: 10
                    Label {
                        width: 72
                        text: "%1 · L%2:C%3".arg(issueDelegate.modelData.severity === "error"
                            ? qsTr("错误") : qsTr("警告"))
                            .arg(issueDelegate.modelData.line).arg(issueDelegate.modelData.column)
                        color: issueDelegate.modelData.severity === "error"
                               ? Theme.colors.syntax.error
                               : Theme.colors.syntax.warning
                        font: Theme.codeFont
                    }
                    Label {
                        width: Math.max(0, issueDelegate.width - 120)
                        text: issueDelegate.modelData.detail
                        color: Theme.colors.text.primary
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                    }
                }

                background: HoverChrome {
                    hovered: issueDelegate.highlighted || issueDelegate.hovered
                    tone: "hover"
                }
            }

            ScrollBar.vertical: ScrollBar {}
        }

        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.validationRows.length === 0
            text: root.analysisSession.pending ? qsTr("正在分析…") : qsTr("未发现验证问题")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.shellController.bottomTabsCurrentTabId === "muri"

        ListView {
            id: muriList
            anchors.fill: parent
            anchors.margins: 8
            clip: true
            model: root.analysisSession.muriRows
            delegate: ItemDelegate {
                id: muriDelegate
                required property var modelData
                width: ListView.view.width
                height: 42
                hoverEnabled: true
                onClicked: root.analysisSession.activateRow(modelData)
                contentItem: Column {
                    spacing: 2
                    Label {
                        text: "[%1] %2 · L%3:C%4".arg(muriDelegate.modelData.alert === "warning"
                            ? qsTr("警告") : qsTr("警报"))
                            .arg(muriDelegate.modelData.title)
                            .arg(muriDelegate.modelData.line).arg(muriDelegate.modelData.column)
                        color: muriDelegate.modelData.severity === "warning"
                               ? Theme.colors.syntax.warning : Theme.colors.syntax.error
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                    }
                    Label {
                        width: muriDelegate.width - 20
                        text: muriDelegate.modelData.detail
                        elide: Text.ElideRight
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                }
                background: HoverChrome { hovered: muriDelegate.highlighted || muriDelegate.hovered; tone: "hover" }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.muriRows.length === 0
            text: root.analysisSession.pending ? qsTr("正在分析…") : qsTr("未发现 Muri 问题")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
        }
    }

    Connections {
        target: root.analysisSession
        function onRowActivated(difficultyId, revision, line, column, endColumn, second) {
            root.analysisRowActivated(difficultyId, revision, line, column, endColumn, second)
        }
    }
}
