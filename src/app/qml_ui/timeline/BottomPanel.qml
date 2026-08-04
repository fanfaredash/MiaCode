pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI
import MiaCode.Timeline

Rectangle {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var preferences
    required property var commands
    required property var shellController
    signal syntaxIssueActivated(int line, int column, int endColumn)

    color: Theme.colors.background.workbench
    clip: true

    BottomTabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        workbenchState: root.workbenchState
        documentSession: root.documentSession
    }

    TimelineQuickItem {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        visible: root.workbenchState.activeBottomTab === 0
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

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        visible: root.workbenchState.activeBottomTab === 1

        Row {
            id: summaryRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 12

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 个错误，%2 个警告，已解析 %3 个物件")
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
            text: qsTr("未发现语法问题")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
        }
    }
}
