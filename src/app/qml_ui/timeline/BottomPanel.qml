pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI
import MiaCode.Timeline

Rectangle {
    id: root

    required property var documentSession
    required property var preferences
    required property var commands
    required property var shellController
    signal syntaxIssueActivated(int difficultyId, var revision, int line, int column, int endColumn)

    color: Theme.colors.background.surface
    clip: true

    BottomTabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        documentSession: root.documentSession
        shellController: root.shellController
    }

    TimelineQuickItem {
        id: timelineItem

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        visible: root.shellController.bottomTabsCurrentTabId === "timeline"
        enabled: visible
        stateBridge: root.shellController.timelineStateBridge
        onHeaderNavigateRequested: second => root.shellController.timelineHeaderNavigate(second)
        onTimelineWheelNavigateRequested: second => root.shellController.timelineWheelNavigate(second)
        onCenterNavigateRequested: second => root.shellController.timelineCenterNavigate(second)
        onTimelineDragStarted: root.shellController.timelineDragStarted()
        onTimelineDragFinished: second => root.shellController.timelineDragFinished(second)
        onTimelineUserInteractionStarted: root.shellController.timelineUserInteractionStarted()
        onTimelineSurfaceReady: root.shellController.noteTimelineSurfaceReady()
        onFollowPreviewToggled: enabled => root.shellController.timelineFollowPreviewToggled(enabled)
        onFollowProgressToggled: enabled => root.shellController.timelineFollowProgressToggled(enabled)
        onPreviewPlayPauseRequested: root.shellController.togglePreviewPlayback()
    }

    // The timeline header remains a single QSG-rendered visual. These transparent
    // controls are only its v2 input/accessibility layer, so visual and hit geometry
    // share the header scale provided by the shell without duplicating its drawing.
    AbstractButton {
        id: zoomHitControl

        x: Math.round(4 * root.shellController.bottomTabsHeaderScale)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        width: Math.max(56, Math.round(72 * root.shellController.bottomTabsHeaderScale))
        height: Math.max(22, Math.round(22 * root.shellController.bottomTabsHeaderScale))
        visible: timelineItem.visible
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("时间轴缩放")
        Accessible.description: qsTr("打开时间轴缩放预设")
        onClicked: {
            const point = mapToGlobal(0, 0)
            root.shellController.openTimelineZoomMenu(
                Math.round(point.x), Math.round(point.y), Math.round(width))
        }
        onHoveredChanged: timelineItem.setZoomControlHoveredPart(hovered ? 1 : 0)
        onPressedChanged: timelineItem.setZoomControlPressedPart(pressed ? 1 : 0)
        contentItem: Item {}
        background: Item {
            Rectangle {
                anchors.fill: parent
                visible: zoomHitControl.activeFocus
                color: "transparent"
                border.width: 1
                border.color: Theme.colors.accent.primary
                radius: 3
            }
        }
    }

    AbstractButton {
        id: brightnessHitControl

        width: Math.max(28, Math.round(28 * root.shellController.bottomTabsHeaderScale))
        height: Math.max(22, Math.round(22 * root.shellController.bottomTabsHeaderScale))
        x: Math.max(zoomHitControl.x + zoomHitControl.width + 8, parent.width - width - 8)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        visible: timelineItem.visible
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("时间轴亮度")
        Accessible.description: qsTr("打开波形和小节线亮度设置")
        onClicked: {
            const point = mapToGlobal(width, 0)
            root.shellController.openTimelineBrightnessMenu(
                Math.round(point.x), Math.round(point.y))
        }
        onHoveredChanged: timelineItem.setSettingsControlHovered(hovered)
        onPressedChanged: timelineItem.setSettingsControlPressed(pressed)
        contentItem: Item {}
        background: Item {
            Rectangle {
                anchors.fill: parent
                visible: brightnessHitControl.activeFocus
                color: "transparent"
                border.width: 1
                border.color: Theme.colors.accent.primary
                radius: 3
            }
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
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
                text: root.documentSession.validationPending
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
            model: root.documentSession.syntaxIssues

            delegate: ItemDelegate {
                id: issueDelegate
                required property var modelData
                width: ListView.view.width
                height: 34
                hoverEnabled: true
                leftPadding: 10
                rightPadding: 10
                onClicked: root.syntaxIssueActivated(
                    modelData.difficultyId,
                    modelData.revision,
                    modelData.line,
                    modelData.column,
                    modelData.endColumn)

                contentItem: Row {
                    spacing: 10
                    Label {
                        width: 72
                        text: "L%1:C%2".arg(issueDelegate.modelData.line).arg(issueDelegate.modelData.column)
                        color: issueDelegate.modelData.severity === "error"
                               ? Theme.colors.syntax.error
                               : Theme.colors.syntax.warning
                        font: Theme.codeFont
                    }
                    Label {
                        width: Math.max(0, issueDelegate.width - 120)
                        text: issueDelegate.modelData.message
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
            visible: root.documentSession.syntaxIssueCount === 0
            text: root.documentSession.validationPending ? qsTr("正在分析…") : qsTr("未发现语法问题")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
        }
    }
}
