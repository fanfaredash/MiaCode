import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root

    visible: true
    width: 1680
    height: 980
    minimumWidth: 1280
    minimumHeight: 800
    title: controller.windowTitle
    color: "#e9eef4"

    property color chromeBg: "#f7fafc"
    property color panelBg: "#ffffff"
    property color panelAltBg: "#f1f5f9"
    property color edgeColor: "#d5dee8"
    property color accent: "#117a8b"
    property color accentSoft: "#dff1f4"
    property color warning: "#d97706"
    property color danger: "#c2410c"
    property color ink: "#132033"
    property color inkSoft: "#5b6778"

    onClosing: function(close) {
        if (!controller.confirmClose())
            close.accepted = false
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("New"); onTriggered: controller.newFile() }
            Action { text: qsTr("Open..."); onTriggered: controller.openFile() }
            Action { text: qsTr("Save"); onTriggered: controller.saveFile() }
            Action { text: qsTr("Save As..."); onTriggered: controller.saveFileAs() }
            MenuSeparator {}
            Action { text: qsTr("Preferences..."); onTriggered: controller.openPreferences() }
        }
        Menu {
            title: qsTr("&Edit")
            Action { text: qsTr("Syntax Check"); onTriggered: controller.runSyntaxCheck() }
            Action { text: qsTr("Format Chart"); onTriggered: controller.normalizeWholeChart() }
            MenuSeparator {}
            Action { text: qsTr("Mirror Left/Right"); onTriggered: controller.mirrorLeftRight() }
            Action { text: qsTr("Mirror Up/Down"); onTriggered: controller.mirrorUpDown() }
            Action { text: qsTr("Rotate 180"); onTriggered: controller.rotate180() }
            Action { text: qsTr("Rotate -45"); onTriggered: controller.rotate45CounterClockwise() }
            Action { text: qsTr("Rotate +45"); onTriggered: controller.rotate45Clockwise() }
            MenuSeparator {}
            Action { text: qsTr("Toggle Break"); onTriggered: controller.toggleBreakSelection() }
            Action { text: qsTr("Toggle EX"); onTriggered: controller.toggleExSelection() }
            Action { text: qsTr("Toggle Firework"); onTriggered: controller.toggleFireworkSelection() }
            Action { text: qsTr("Random Rotate"); onTriggered: controller.randomRotateSelection() }
        }
        Menu {
            title: qsTr("&Preview")
            Action { text: qsTr("Play / Pause"); onTriggered: controller.togglePreviewPlayback() }
            Action { text: qsTr("Stop"); onTriggered: controller.stopPreview() }
            MenuSeparator {}
            Action { text: qsTr("Swap Side Panels"); onTriggered: controller.toggleWorkspacePanelsSwapped() }
            Action {
                text: controller.previewFullscreen ? qsTr("Exit Fullscreen Preview") : qsTr("Fullscreen Preview")
                onTriggered: controller.previewFullscreen = !controller.previewFullscreen
            }
            MenuSeparator {}
            Action { text: qsTr("Audio Settings..."); onTriggered: controller.openPreviewAudioSettings() }
            Action { text: qsTr("Preview Settings..."); onTriggered: controller.openPreviewVideoSettings() }
        }
        Menu {
            title: qsTr("&Tools")
            Action { text: qsTr("BPM && Offset Detection"); onTriggered: controller.openLatencyDetector() }
            Action { text: qsTr("Export Chart"); onTriggered: controller.exportPreviewVideo() }
            Action { text: qsTr("Batch Export"); onTriggered: controller.batchExportPreviewVideo() }
            MenuSeparator {}
            Action { text: qsTr("Official Chart Mirror"); onTriggered: controller.openOfficialChartMirror() }
            Action { text: qsTr("simaiwiki"); onTriggered: controller.openSimaiWiki() }
        }
        Menu {
            title: qsTr("&Help")
            Action { text: qsTr("About"); onTriggered: controller.openAbout() }
        }
    }

    header: ToolBar {
        background: Rectangle {
            color: root.chromeBg
            border.color: root.edgeColor
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            ColumnLayout {
                spacing: 2

                Label {
                    text: controller.currentFileLabel
                    color: root.ink
                    font.pixelSize: 17
                    font.bold: true
                    Layout.maximumWidth: 780
                    elide: Text.ElideMiddle
                }

                Label {
                    text: controller.hasActiveDifficulty
                        ? controller.activeDifficultyName + "  |  " + controller.cursorLabel
                        : (controller.currentPage === "metadata" ? qsTr("Metadata") : qsTr("Welcome"))
                    color: root.inkSoft
                    font.pixelSize: 12
                    Layout.maximumWidth: 780
                    elide: Text.ElideRight
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Save")
                onClicked: controller.saveFile()
            }

            Button {
                text: controller.workspacePanelsSwapped ? qsTr("Preview Left") : qsTr("Preview Right")
                visible: controller.currentPage === "chart"
                onClicked: controller.toggleWorkspacePanelsSwapped()
            }

            Button {
                text: controller.previewFullscreen ? qsTr("Exit Fullscreen") : qsTr("Fullscreen")
                visible: controller.currentPage === "chart"
                onClicked: controller.previewFullscreen = !controller.previewFullscreen
            }
        }
    }

    footer: Rectangle {
        color: root.chromeBg
        border.color: root.edgeColor
        implicitHeight: 36

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            Label {
                text: controller.statusText.length > 0 ? controller.statusText : qsTr("Quick Shell Beta")
                color: root.inkSoft
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                visible: controller.currentPage === "chart"
                text: controller.previewSpeedLabel
                color: root.ink
                font.bold: true
            }
        }
    }

    Menu {
        id: toolboxMenu

        MenuItem { text: qsTr("BPM && Offset Detection"); onTriggered: controller.openLatencyDetector() }
        MenuItem { text: qsTr("Export Chart"); onTriggered: controller.exportPreviewVideo() }
        MenuItem { text: qsTr("Batch Export"); onTriggered: controller.batchExportPreviewVideo() }
        MenuSeparator {}
        MenuItem { text: qsTr("Format Chart"); onTriggered: controller.normalizeWholeChart() }
        MenuItem { text: qsTr("Official Chart Mirror"); onTriggered: controller.openOfficialChartMirror() }
    }

    Menu {
        id: addDifficultyMenu

        Instantiator {
            model: controller.availableDifficultyOptions

            delegate: MenuItem {
                required property var modelData

                text: modelData.label
                onTriggered: controller.addDifficulty(modelData.id)
            }

            onObjectAdded: function(index, object) { addDifficultyMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) {
                addDifficultyMenu.removeItem(object)
            }
        }
    }

    Menu {
        id: previewSpeedMenu

        MenuItem { text: "0.25x"; onTriggered: controller.setPreviewRate(0.25) }
        MenuItem { text: "0.5x"; onTriggered: controller.setPreviewRate(0.5) }
        MenuItem { text: "0.75x"; onTriggered: controller.setPreviewRate(0.75) }
        MenuItem { text: "1x"; onTriggered: controller.setPreviewRate(1.0) }
        MenuItem { text: "1.25x"; onTriggered: controller.setPreviewRate(1.25) }
        MenuItem { text: "2x"; onTriggered: controller.setPreviewRate(2.0) }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 12
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 220
            color: root.panelBg
            radius: 16
            border.color: root.edgeColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Label {
                    text: qsTr("Outline")
                    color: root.ink
                    font.pixelSize: 18
                    font.bold: true
                }

                ListView {
                    id: outlineView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    clip: true
                    model: controller.outlineModel

                    delegate: Rectangle {
                        radius: 12
                        color: selected ? root.accentSoft : root.panelAltBg
                        border.color: selected ? root.accent : root.edgeColor
                        implicitHeight: 52
                        width: outlineView.width

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 10

                            Label {
                                text: label
                                color: selected ? root.accent : root.ink
                                font.pixelSize: 14
                                font.bold: selected
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            ToolButton {
                                visible: canDelete
                                text: qsTr("Delete")
                                onClicked: controller.deleteDifficulty(difficultyId)
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (kind === "add")
                                    addDifficultyMenu.popup()
                                else if (kind === "toolbox")
                                    toolboxMenu.popup()
                                else
                                    controller.activateOutlineRow(index)
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 900
            color: "transparent"

            StackLayout {
                anchors.fill: parent
                currentIndex: controller.currentPage === "metadata" ? 1 : (controller.currentPage === "chart" ? 2 : 0)

                Rectangle {
                    color: root.panelBg
                    radius: 20
                    border.color: root.edgeColor

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 12

                        Label {
                            text: qsTr("Quick Shell Beta")
                            font.pixelSize: 34
                            font.bold: true
                            color: root.ink
                        }

                        Label {
                            text: qsTr("Open a chart folder or start a new maidata.txt project.")
                            color: root.inkSoft
                            horizontalAlignment: Text.AlignHCenter
                        }

                        RowLayout {
                            spacing: 10

                            Button { text: qsTr("Open"); onClicked: controller.openFile() }
                            Button { text: qsTr("New"); onClicked: controller.newFile() }
                        }
                    }
                }

                Rectangle {
                    color: root.panelBg
                    radius: 20
                    border.color: root.edgeColor

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 18
                        clip: true

                        ColumnLayout {
                            width: parent.width
                            spacing: 14

                            Label {
                                text: qsTr("Metadata")
                                font.pixelSize: 24
                                font.bold: true
                                color: root.ink
                            }

                            TextField {
                                text: controller.titleDraft
                                placeholderText: qsTr("Title")
                                onTextEdited: controller.titleDraft = text
                            }

                            TextField {
                                text: controller.artistDraft
                                placeholderText: qsTr("Artist")
                                onTextEdited: controller.artistDraft = text
                            }

                            TextField {
                                text: controller.firstDraft
                                placeholderText: qsTr("&first")
                                onTextEdited: controller.firstDraft = text
                            }

                            TextField {
                                text: controller.designerDraft
                                placeholderText: qsTr("Designer")
                                onTextEdited: controller.designerDraft = text
                            }

                            TextArea {
                                id: metadataExtraArea
                                Layout.fillWidth: true
                                Layout.minimumHeight: 420
                                wrapMode: TextEdit.NoWrap
                                text: controller.metadataExtraDraft
                                onTextChanged: {
                                    if (activeFocus && text !== controller.metadataExtraDraft)
                                        controller.metadataExtraDraft = text
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        color: root.panelBg
                        radius: 18
                        border.color: root.edgeColor
                        implicitHeight: 84

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 18
                            anchors.rightMargin: 18
                            spacing: 12

                            ColumnLayout {
                                spacing: 4

                                Label {
                                    text: controller.activeDifficultyName
                                    color: root.ink
                                    font.pixelSize: 22
                                    font.bold: true
                                }

                                RowLayout {
                                    spacing: 8

                                    Label {
                                        text: controller.cursorLabel
                                        color: root.inkSoft
                                    }

                                    Label {
                                        text: qsTr("Errors %1").arg(controller.validationErrorCount)
                                        color: root.danger
                                        visible: controller.validationErrorCount > 0
                                    }

                                    Label {
                                        text: qsTr("Warnings %1").arg(controller.validationWarningCount)
                                        color: root.warning
                                        visible: controller.validationWarningCount > 0
                                    }

                                    Label {
                                        text: qsTr("Muri %1").arg(controller.muriIssueCount)
                                        color: root.accent
                                        visible: controller.muriIssueCount > 0
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }

                            TextField {
                                text: controller.difficultyLevelDraft
                                placeholderText: qsTr("Level")
                                Layout.preferredWidth: 120
                                onTextEdited: controller.difficultyLevelDraft = text
                            }

                            TextField {
                                text: controller.difficultyDesignerDraft
                                placeholderText: qsTr("Difficulty Designer")
                                Layout.preferredWidth: 220
                                onTextEdited: controller.difficultyDesignerDraft = text
                            }
                        }
                    }

                    SplitView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Horizontal

                        Item {
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: 480

                            Loader {
                                anchors.fill: parent
                                sourceComponent: controller.workspacePanelsSwapped ? previewPaneComponent : editorPaneComponent
                            }
                        }

                        Item {
                            SplitView.preferredWidth: 460
                            SplitView.minimumWidth: 360

                            Loader {
                                anchors.fill: parent
                                sourceComponent: controller.workspacePanelsSwapped ? editorPaneComponent : previewPaneComponent
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 340
                        visible: controller.showBottomTabs
                        color: root.panelBg
                        radius: 18
                        border.color: root.edgeColor

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 12

                            TabBar {
                                Layout.fillWidth: true
                                currentIndex: controller.bottomTabIndex
                                onCurrentIndexChanged: controller.bottomTabIndex = currentIndex

                                TabButton { text: qsTr("Timeline") }
                                TabButton { text: qsTr("Validation") }
                                TabButton { text: qsTr("Muri") }
                            }

                            StackLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: controller.bottomTabIndex

                                Rectangle {
                                    color: root.panelAltBg
                                    radius: 14
                                    border.color: root.edgeColor

                                    WindowContainer {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        window: controller.timelineSurface.window
                                    }
                                }

                                ListView {
                                    clip: true
                                    spacing: 10
                                    model: controller.validationModel

                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        radius: 14
                                        color: issueEnabled ? root.panelBg : root.panelAltBg
                                        border.color: ignored ? root.edgeColor : (warning ? root.warning : root.edgeColor)
                                        implicitHeight: validationContentColumn.implicitHeight + 22

                                        ColumnLayout {
                                            id: validationContentColumn
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            spacing: 8

                                            Text {
                                                Layout.fillWidth: true
                                                text: richText
                                                textFormat: Text.RichText
                                                wrapMode: Text.Wrap
                                                color: root.ink
                                            }

                                            RowLayout {
                                                spacing: 8

                                                Button {
                                                    text: qsTr("Jump")
                                                    enabled: issueEnabled
                                                    onClicked: controller.activateIssue(false, index)
                                                }

                                                Button {
                                                    text: qsTr("Copy")
                                                    onClicked: controller.copyIssue(false, index)
                                                }

                                                Button {
                                                    text: ignored ? qsTr("Unignore") : qsTr("Ignore")
                                                    enabled: issueTypeKey.length > 0
                                                    onClicked: controller.toggleIssueIgnored(false, index)
                                                }

                                                Item { Layout.fillWidth: true }

                                                Label {
                                                    text: line > 0 ? ("L" + line + " C" + col) : ""
                                                    color: root.inkSoft
                                                }
                                            }
                                        }
                                    }
                                }

                                ListView {
                                    clip: true
                                    spacing: 10
                                    model: controller.muriModel

                                    delegate: Rectangle {
                                        width: ListView.view.width
                                        radius: 14
                                        color: issueEnabled ? root.panelBg : root.panelAltBg
                                        border.color: ignored ? root.edgeColor : root.accent
                                        implicitHeight: muriContentColumn.implicitHeight + 22

                                        ColumnLayout {
                                            id: muriContentColumn
                                            anchors.fill: parent
                                            anchors.margins: 12
                                            spacing: 8

                                            Text {
                                                Layout.fillWidth: true
                                                text: richText
                                                textFormat: Text.RichText
                                                wrapMode: Text.Wrap
                                                color: root.ink
                                            }

                                            RowLayout {
                                                spacing: 8

                                                Button {
                                                    text: qsTr("Jump")
                                                    enabled: issueEnabled
                                                    onClicked: controller.activateIssue(true, index)
                                                }

                                                Button {
                                                    text: qsTr("Copy")
                                                    onClicked: controller.copyIssue(true, index)
                                                }

                                                Button {
                                                    text: ignored ? qsTr("Unignore") : qsTr("Ignore")
                                                    enabled: issueTypeKey.length > 0
                                                    onClicked: controller.toggleIssueIgnored(true, index)
                                                }

                                                Item { Layout.fillWidth: true }

                                                Label {
                                                    text: line > 0 ? ("L" + line + " C" + col) : ""
                                                    color: root.inkSoft
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: editorPaneComponent

        Rectangle {
            color: root.panelBg
            radius: 18
            border.color: root.edgeColor

            WindowContainer {
                anchors.fill: parent
                anchors.margins: 6
                window: controller.chartEditorSurface.window
            }
        }
    }

    Component {
        id: previewPaneComponent

        Rectangle {
            color: root.panelBg
            radius: 18
            border.color: root.edgeColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#111827"
                    radius: 14

                    WindowContainer {
                        anchors.fill: parent
                        anchors.margins: 4
                        window: controller.previewFullscreen ? null : controller.previewWindow
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: controller.previewPlaying ? qsTr("Pause") : qsTr("Play")
                        onClicked: controller.togglePreviewPlayback()
                    }

                    Button {
                        text: qsTr("Stop")
                        onClicked: controller.stopPreview()
                    }

                    Slider {
                        id: previewSlider
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(controller.previewDurationSeconds, 0.001)
                        value: controller.previewPositionSeconds
                        onMoved: controller.seekPreview(value)
                    }

                    Button {
                        text: controller.previewSpeedLabel
                        onClicked: previewSpeedMenu.popup()
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: controller.previewStats

                        Rectangle {
                            radius: 999
                            color: root.panelAltBg
                            border.color: root.edgeColor
                            height: 28
                            implicitWidth: statLabel.implicitWidth + 18

                            Label {
                                id: statLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: root.inkSoft
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }

    Window {
        id: fullscreenPreviewWindow

        visibility: controller.previewFullscreen ? Window.FullScreen : Window.Hidden
        color: "#020617"
        title: root.title

        onClosing: function(close) {
            close.accepted = false
            controller.previewFullscreen = false
        }

        Rectangle {
            anchors.fill: parent
            color: "#020617"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#020617"
                    radius: 18
                    border.color: "#1f2937"

                    WindowContainer {
                        anchors.fill: parent
                        anchors.margins: 4
                        window: controller.previewFullscreen ? controller.previewWindow : null
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        text: controller.previewPlaying ? qsTr("Pause") : qsTr("Play")
                        onClicked: controller.togglePreviewPlayback()
                    }

                    Button {
                        text: qsTr("Stop")
                        onClicked: controller.stopPreview()
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(controller.previewDurationSeconds, 0.001)
                        value: controller.previewPositionSeconds
                        onMoved: controller.seekPreview(value)
                    }

                    Button {
                        text: controller.previewSpeedLabel
                        onClicked: previewSpeedMenu.popup()
                    }

                    Button {
                        text: qsTr("Exit Fullscreen")
                        onClicked: controller.previewFullscreen = false
                    }
                }
            }
        }
    }
}
