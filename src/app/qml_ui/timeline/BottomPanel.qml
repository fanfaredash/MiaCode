pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI
import MiaCode.Timeline

Rectangle {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var preferences
    required property var commands
    required property var timelineSession
    required property var previewSession
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
        timelineSession: root.timelineSession
    }

    TimelineQuickItem {
        id: timelineItem

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.timelineSession.currentTabId === "timeline"
        enabled: visible
        stateBridge: root.timelineSession.stateBridge
        // Keep header labels and markers clear of the QML controls.
        headerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerRightLimit: Math.max(0, brightnessButton.x - 2)
        headerMarkerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerMarkerRightLimit: Math.max(0, brightnessButton.x - 2)
        onHeaderNavigateRequested: second => root.timelineSession.headerNavigate(second)
        onTimelineWheelNavigateRequested: second => root.timelineSession.wheelNavigate(second)
        onCenterNavigateRequested: second => root.timelineSession.centerNavigate(second)
        onTimelineDragStarted: root.timelineSession.dragStarted()
        onTimelineDragFinished: second => root.timelineSession.dragFinished(second)
        onTimelineUserInteractionStarted: root.timelineSession.userInteractionStarted()
        onTimelineSurfaceReady: root.timelineSession.surfaceReady()
        onFollowPreviewToggled: enabled => root.timelineSession.followPreviewToggled(enabled)
        onPreviewPlayPauseRequested: root.previewSession.togglePlayback()
    }

    AppDropDownButton {
        id: zoomButton

        x: Math.round(4 * timelineItem.headerScale)
        y: timelineItem.y + Math.max(0, (timelineItem.timelineTop - height) / 2)
        height: Math.min(implicitHeight, Math.max(1, timelineItem.timelineTop))
        visible: timelineItem.visible
        text: qsTr("%1%").arg(Math.round(root.timelineSession.stateBridge
            ? root.timelineSession.stateBridge.zoomScale * 100
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
        stateBridge: root.timelineSession.stateBridge
        onClosed: root.zoomMenuClosedAt = Date.now()
    }

    TimelineBrightnessMenu {
        id: brightnessMenu
        stateBridge: root.timelineSession.stateBridge
        onClosed: root.brightnessMenuClosedAt = Date.now()
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.timelineSession.currentTabId === "validation"

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
                    : qsTr("%1 个错误，%2 个警告")
                        .arg(root.documentSession.syntaxErrorCount)
                        .arg(root.documentSession.syntaxWarningCount)
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

            // The severity column is measured, not guessed: a hard-coded width
            // let "警告 · L44:C1" overflow its box and the detail column then
            // painted straight over the tail. The floor only aligns the columns
            // when the label happens to be narrower.
            delegate: ChromeRow {
                id: issueDelegate
                required property var modelData
                width: ListView.view.width
                height: 34
                tone: "hover"
                onClicked: root.analysisSession.activateRow(modelData)

                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        id: issueLocation
                        Layout.preferredWidth: Math.max(implicitWidth, 100)
                        text: "%1 · L%2:C%3".arg(issueDelegate.modelData.severity === "error"
                            ? qsTr("错误") : qsTr("警告"))
                            .arg(issueDelegate.modelData.line).arg(issueDelegate.modelData.column)
                        color: issueDelegate.modelData.severity === "error"
                               ? Theme.colors.syntax.error
                               : Theme.colors.syntax.warning
                        font: Theme.codeFont
                        verticalAlignment: Text.AlignVCenter
                    }
                    Label {
                        Layout.fillWidth: true
                        text: issueDelegate.modelData.detail
                        color: Theme.colors.text.primary
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                        verticalAlignment: Text.AlignVCenter
                    }
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
        visible: root.timelineSession.currentTabId === "muri"

        ListView {
            id: muriList
            anchors.fill: parent
            anchors.margins: 8
            clip: true
            model: root.analysisSession.muriRows
            delegate: ChromeRow {
                id: muriDelegate
                required property var modelData
                width: ListView.view.width
                height: 42
                tone: "hover"
                onClicked: root.analysisSession.activateRow(modelData)
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: "[%1] %2 · L%3:C%4".arg(muriDelegate.modelData.alert === "warning"
                            ? qsTr("警告") : qsTr("警报"))
                            .arg(muriDelegate.modelData.title)
                            .arg(muriDelegate.modelData.line).arg(muriDelegate.modelData.column)
                        color: muriDelegate.modelData.severity === "warning"
                               ? Theme.colors.syntax.warning : Theme.colors.syntax.error
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                    }
                    Label {
                        Layout.fillWidth: true
                        text: muriDelegate.modelData.detail
                        elide: Text.ElideRight
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                }
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
