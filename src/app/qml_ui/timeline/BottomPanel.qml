pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI
import MiaCode.Timeline

Item {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var preferences
    required property var commands
    required property var timelineSession
    required property var previewSession
    signal analysisRowActivated(int difficultyId, var revision, int line, int column, int endColumn, double second)

    Rectangle {
        width: parent.width
        height: timelineItem.visible ? tabs.height : parent.height
        color: Theme.surfaceColor(Theme.colors.background.panel)
    }
    clip: true
    readonly property int contentTopMargin: 0

    // 时间轴外壳颜色的唯一来源是 Theme.qml 的 colors.timeline 分组。
    // 桥接对象必须先于 TimelineQuickItem 创建，颜色才会在首次绘制前进入
    // C++ 快照；后续若给该分组换值，这里会随绑定自动重新推送。
    TimelineThemeBridge {
        id: timelineTheme
        windowColor: Theme.colors.timeline.window
        headerColor: Theme.surfaceColor(Theme.colors.background.panel)
        sidebarColor: Theme.surfaceColor(Theme.colors.background.panel)
        baseColor: Theme.surfaceColor(Theme.colors.background.surface)
        onBaseColorChanged: Qt.callLater(() => timelineItem.refreshTheme())
        borderColor: Theme.colors.timeline.border
        axisColor: Theme.colors.timeline.axis
        gridMajorColor: Theme.colors.timeline.gridMajor
        gridSubdivisionColor: Theme.colors.timeline.gridSubdivision
        gridMinorColor: Theme.colors.timeline.gridMinor
        laneEvenColor: Theme.colors.timeline.laneEven
        laneOddColor: Theme.colors.timeline.laneOdd
        labelColor: Theme.colors.timeline.label
        textSecondaryColor: Theme.colors.timeline.textSecondary
        waveStrokeColor: Theme.colors.timeline.waveStroke
    }

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
        anchors.bottom: parent.bottom
        visible: root.timelineSession.currentTabId === "timeline"
        enabled: visible
        stateBridge: root.timelineSession.stateBridge
        headerLeftLimit: zoomButton.x + zoomButton.width + Theme.panelPadding
        headerRightLimit: brightnessButton.x - Theme.panelPadding
        headerMarkerLeftLimit: headerLeftLimit
        headerMarkerRightLimit: headerRightLimit
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
        compact: true

        x: Theme.panelPadding - Theme.chromeInsetX
        y: timelineItem.y + (timelineItem.timelineTop - height) / 2
        height: implicitHeight
        visible: timelineItem.visible
        text: UiText.text("%1%").arg(Math.round(root.timelineSession.stateBridge
            ? root.timelineSession.stateBridge.zoomScale * 100
            : 50))
        sizeToLabels: zoomMenu.zoomLabels
        tooltip: UiText.text("时间轴缩放")
        expanded: zoomMenu.active
        Accessible.description: UiText.text("打开时间轴缩放预设")
        onClicked: {
            if (zoomMenu.active) {
                zoomMenu.close()
                return
            }
            zoomMenu.openAt(zoomButton)
        }
    }

    IconButton {
        id: brightnessButton
        compact: true

        width: implicitWidth
        height: implicitHeight
        x: root.width - Theme.panelPadding - width + horizontalInset
        y: timelineItem.y + (timelineItem.timelineTop - height) / 2
        visible: timelineItem.visible
        iconSource: Qt.resolvedUrl("icons/settings.svg")
        tooltip: UiText.text("时间轴亮度")
        active: brightnessMenu.active
        Accessible.description: UiText.text("打开波形和小节线亮度设置")
        onClicked: {
            if (brightnessMenu.active) {
                brightnessMenu.close()
                return
            }
            brightnessMenu.openAt(brightnessButton)
        }
    }

    TimelineZoomMenu {
        id: zoomMenu
        stateBridge: root.timelineSession.stateBridge
    }

    TimelineBrightnessMenu {
        id: brightnessMenu
        stateBridge: root.timelineSession.stateBridge
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
                    ? UiText.text("正在分析…")
                    : UiText.text("%1 个错误，%2 个警告")
                        .arg(root.documentSession.syntaxErrorCount)
                        .arg(root.documentSession.syntaxWarningCount)
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
            }

            AppButton {
                text: UiText.text("重新检查")
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
                onClicked: root.analysisSession.activateRow(modelData)

                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        id: issueLocation
                        Layout.preferredWidth: Math.max(implicitWidth, 100)
                        text: "%1 · L%2:C%3".arg(issueDelegate.modelData.severity === "error"
                            ? UiText.text("错误") : UiText.text("警告"))
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

            ScrollBar.vertical: AppScrollBar {}
        }

        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.validationRows.length === 0
            text: root.analysisSession.pending ? UiText.text("正在分析…") : UiText.text("未发现验证问题")
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
                onClicked: root.analysisSession.activateRow(modelData)
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: "[%1] %2 · L%3:C%4".arg(muriDelegate.modelData.alert === "warning"
                            ? UiText.text("警告") : UiText.text("警报"))
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
            ScrollBar.vertical: AppScrollBar {}
        }
        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.muriRows.length === 0
            text: root.analysisSession.pending ? UiText.text("正在分析…") : UiText.text("未发现 Muri 问题")
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
