pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI
import MiaCode.Timeline

Item {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var preferences
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
    readonly property real minimumHeight: tabs.implicitHeight + timelineItem.minimumViewportHeight
    readonly property real minimumWidth: tabs.minimumWidth
    readonly property int timelineHeaderLeftLimit:
        zoomButton.x + zoomButton.width + Theme.panelPadding
    readonly property int timelineHeaderRightLimit:
        brightnessButton.x - Theme.panelPadding
    readonly property real analysisTextLeftInset:
        Theme.panelPadding - Theme.chromeInsetX + Theme.compactTabContentPadding
    readonly property real analysisListLeftInset:
        Math.max(0, analysisTextLeftInset - Theme.rowPaddingX)
    readonly property real analysisVerticalSpacing: Theme.chromeInsetY
    readonly property int muriWarningCount: countMuriRows("warning")
    readonly property int muriIssueCount: countMuriRows("muri")

    function countMuriRows(alert) {
        const rows = analysisSession.muriRows
        let count = 0
        for (let index = 0; index < rows.length; ++index) {
            if (rows[index].alert === alert)
                ++count
        }
        return count
    }

    component AnalysisIssueRow: ChromeRow {
        id: issueRow

        required property var modelData
        required property string leadingText
        required property string bodyText

        readonly property color leadingColor: modelData.severity === "error"
            ? Theme.colors.syntax.error
            : Theme.colors.syntax.warning

        width: ListView.view.width
        height: implicitHeight
        onClicked: root.analysisSession.activateRow(modelData)

        contentItem: Item {
            implicitHeight: issueContent.implicitHeight

            Column {
                id: issueContent

                width: parent.width
                y: (parent.height - implicitHeight) / 2
                spacing: 2

                Label {
                    width: parent.width
                    text: issueRow.leadingText
                    color: issueRow.leadingColor
                    wrapMode: Text.Wrap
                    font.family: Theme.codeFont.family
                    font.pixelSize: Theme.compactFontSize
                }

                Label {
                    width: parent.width
                    text: issueRow.bodyText
                    color: Theme.colors.text.primary
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                }
            }
        }
    }

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
        headerLeftLimit: root.timelineHeaderLeftLimit
        headerRightLimit: root.timelineHeaderRightLimit
        headerMarkerLeftLimit: root.timelineHeaderLeftLimit
        headerMarkerRightLimit: root.timelineHeaderRightLimit
        onHeaderNavigateRequested: second => root.timelineSession.headerNavigate(second)
        onTimelineWheelNavigateRequested: second => root.timelineSession.wheelNavigate(second)
        onCenterNavigateRequested: second => root.timelineSession.centerNavigate(second)
        onTimelineDragStarted: root.timelineSession.dragStarted()
        onTimelineDragFinished: second => root.timelineSession.dragFinished(second)
        onTimelineUserInteractionStarted: root.timelineSession.userInteractionStarted()
        onTimelineSurfaceReady: root.timelineSession.surfaceReady()
        onFollowPreviewToggled: enabled => root.timelineSession.followPreviewToggled(enabled)
    }

    Tooltip {
        id: timelineMarkerTooltip
        parent: Overlay.overlay
        visible: timelineItem.hoverTooltipText.length > 0
        text: timelineItem.hoverTooltipText
        x: {
            const position = timelineItem.mapToItem(null, timelineItem.hoverTooltipPosition)
            return position.x + 12
        }
        y: {
            const position = timelineItem.mapToItem(null, timelineItem.hoverTooltipPosition)
            return position.y + 16
        }
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
        iconSource: Qt.resolvedUrl("icons/sliders-horizontal.svg")
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
            anchors.leftMargin: root.analysisTextLeftInset
            anchors.rightMargin: Theme.panelPadding
            anchors.topMargin: root.analysisVerticalSpacing

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.analysisSession.pending
                    ? UiText.text("正在分析…")
                    : UiText.text("%1 个错误，%2 个警告")
                        .arg(root.documentSession.syntaxErrorCount)
                        .arg(root.documentSession.syntaxWarningCount)
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
            }
        }

        ListView {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: summaryRow.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: root.analysisVerticalSpacing
            anchors.leftMargin: root.analysisListLeftInset
            anchors.rightMargin: Theme.panelPadding
            clip: true
            model: root.analysisSession.validationRows

            delegate: AnalysisIssueRow {
                id: issueDelegate
                leadingText: modelData.code === "missing_difficulty_level"
                    ? UiText.text("validation.difficulty_header")
                    : "%1 · L%2:C%3".arg(modelData.severity === "error"
                        ? UiText.text("错误") : UiText.text("警告"))
                        .arg(modelData.line).arg(modelData.column)
                bodyText: modelData.detail
            }

            ScrollBar.vertical: AppScrollBar {}
        }

        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.validationRows.length === 0
            text: root.analysisSession.pending
                ? UiText.text("正在分析…")
                : UiText.text("validation.no_syntax_errors_detected")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.compactFontSize
        }
    }

    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.topMargin: root.contentTopMargin
        anchors.bottom: parent.bottom
        visible: root.timelineSession.currentTabId === "muri"

        Row {
            id: muriSummaryRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: root.analysisTextLeftInset
            anchors.rightMargin: Theme.panelPadding
            anchors.topMargin: root.analysisVerticalSpacing

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.analysisSession.pending
                    ? UiText.text("正在分析…")
                    : UiText.text("validation.muri.summary")
                        .arg(root.muriIssueCount)
                        .arg(root.muriWarningCount)
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
            }
        }

        ListView {
            id: muriList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: muriSummaryRow.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: root.analysisVerticalSpacing
            anchors.leftMargin: root.analysisListLeftInset
            anchors.rightMargin: Theme.panelPadding
            clip: true
            model: root.analysisSession.muriRows
            delegate: AnalysisIssueRow {
                id: muriDelegate
                leadingText: "%1 · %2 · L%3:C%4".arg(modelData.alert === "warning"
                    ? UiText.text("validation.muri.alert.warning")
                    : UiText.text("validation.muri.alert.muri"))
                    .arg(modelData.title)
                    .arg(modelData.line)
                    .arg(modelData.column)
                bodyText: modelData.detail
            }
            ScrollBar.vertical: AppScrollBar {}
        }
        Label {
            anchors.centerIn: parent
            visible: root.analysisSession.muriRows.length === 0
            text: root.analysisSession.pending
                ? UiText.text("正在分析…")
                : UiText.text("validation.no_muri_issues_detected")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.compactFontSize
        }
    }

    // The playback-rate HUD sits over the timeline, not over the preview stage:
    // while editing, the timeline is where the eyes already are, and the canvas
    // stays unobscured. Declared last so it paints above every bottom tab —
    // QML stacking is declaration order — and anchored to the panel body rather
    // than to timelineItem so it still shows while 语法 or 无理 is in front.
    // Fullscreen preview keeps its own copy; the timeline is not on screen there.
    PreviewRateToast {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        previewSession: root.previewSession
    }

    Connections {
        target: root.analysisSession
        function onRowActivated(difficultyId, revision, line, column, endColumn, second) {
            root.analysisRowActivated(difficultyId, revision, line, column, endColumn, second)
        }
    }
}
