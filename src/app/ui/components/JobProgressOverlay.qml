import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// The shared dialog blocks interaction while the service runs asynchronously.
// Cancellation raises the flag consumed by the job at its checkpoints.
Item {
    id: root

    // A miacode::JobProgressService instance.
    property var progress: null
    property var comicResources: null

    readonly property bool jobActive: !!root.progress && root.progress.active
    readonly property bool chartExportActive: !!root.progress
        && root.progress.active
        && root.progress.taskTypeName === "chartExport"
    readonly property bool genericActive: !!root.progress
        && root.progress.active
        && root.progress.taskTypeName !== "chartExport"
    readonly property var chartExportLabelLines: {
        const label = root.progress ? String(root.progress.label || "") : ""
        const lines = label.split(/\r?\n/)
        return [lines.length > 0 ? lines[0] : "",
                lines.length > 1 ? lines.slice(1).join(" ") : ""]
    }
    readonly property real comicFrameAspectRatio: 10 / 9
    readonly property real comicFrameWidth: 340
    readonly property real comicFrameHeight: root.comicFrameWidth
        / root.comicFrameAspectRatio
    readonly property int progressBodySpacing: 10
    readonly property color comicButtonGlyphColor:
        Theme.activeTheme.dark
            ? Theme.comicButtonLightGlyph : Theme.comicButtonDarkGlyph
    readonly property real comicButtonGlyphScale: 3.375
    readonly property int comicButtonGlyphPixelSize:
        Math.round(Theme.uiFontSize * root.comicButtonGlyphScale)
    readonly property real comicButtonActiveGlyphScale: 1.5
    readonly property real comicButtonHitScale: 1.1
    readonly property int comicButtonHitSize:
        Math.round(root.comicButtonGlyphPixelSize
                   * root.comicButtonActiveGlyphScale
                   * root.comicButtonHitScale)
    readonly property var comicButtonGlyphStateColors: ({
        normal: root.comicButtonGlyphColor,
        hovered: root.comicButtonGlyphColor,
        pressed: root.comicButtonGlyphColor,
        focused: root.comicButtonGlyphColor,
        disabled: Theme.colors.text.disabled
    })
    readonly property var comicButtonStateColors: ({
        normal: "transparent",
        hover: Theme.comicButtonBackground,
        pressed: Theme.comicButtonBackground,
        selected: "transparent"
    })

    AppDialog {
        objectName: "jobProgressCard"
        visible: root.jobActive
        preferredWidth: root.chartExportActive ? 468 : 420
        preferredHeight: root.chartExportActive
            ? 499 : Theme.dialogCompactHeight
        closePolicy: Popup.NoAutoClose
        title: root.progress ? root.progress.title : ""
        onAboutToShow: {
            if (root.chartExportActive && root.comicResources
                    && root.comicResources.resourceCount > 1) {
                root.comicResources.selectRandomResource()
            }
        }

        body: ColumnLayout {
            spacing: root.progressBodySpacing
            Text {
                objectName: "jobProgressLabel"
                Layout.fillWidth: true
                visible: !root.chartExportActive
                text: root.progress ? root.progress.label : ""
                color: Theme.colors.text.secondary
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "jobProgressChartExportLabelLine1"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive ? Theme.uiFontSize + 4 : 0
                visible: root.chartExportActive
                text: root.chartExportLabelLines[0]
                color: Theme.colors.text.secondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                objectName: "jobProgressChartExportLabelLine2"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive ? Theme.uiFontSize + 4 : 0
                visible: root.chartExportActive
                text: root.chartExportLabelLines[1]
                color: Theme.colors.text.secondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            ProgressBar {
                objectName: "jobProgressBar"
                Layout.fillWidth: true
                from: 0
                to: 100
                indeterminate: !!root.progress && root.progress.indeterminate
                value: root.progress ? root.progress.percent : 0
            }

            RowLayout {
                objectName: "jobProgressComicControls"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: root.chartExportActive ? root.comicFrameHeight : 0
                Layout.bottomMargin: root.chartExportActive
                    ? root.progressBodySpacing * 2 : 0
                spacing: Theme.panelPadding

                IconButton {
                    id: previousButton
                    glyph: "‹"
                    glyphPixelSize: root.comicButtonGlyphPixelSize
                    glyphScale: 1
                    activeGlyphScale: root.comicButtonActiveGlyphScale
                    glyphScaleAnimationDuration: 100
                    glyphStateColors: root.comicButtonGlyphStateColors
                    stateColors: root.comicButtonStateColors
                    tooltip: qsTrId("qml.previous_comic")
                    Accessible.name: qsTrId("qml.previous_comic")
                    Accessible.description: qsTrId("qml.show_previous_comic")
                    visible: comic.canSwitch
                    enabled: comic.canSwitch
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: root.comicButtonHitSize
                    Layout.preferredHeight: root.comicButtonHitSize
                    onClicked: comic.requestSlide(-1)
                }

                JobProgressComic {
                    id: comic
                    objectName: "jobProgressComic"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.comicFrameWidth
                    Layout.preferredHeight: root.chartExportActive ? root.comicFrameHeight : 0
                    resources: root.comicResources
                    active: root.jobActive
                    chartExportActive: root.chartExportActive
                }

                IconButton {
                    id: nextButton
                    glyph: "›"
                    glyphPixelSize: root.comicButtonGlyphPixelSize
                    glyphScale: 1
                    activeGlyphScale: root.comicButtonActiveGlyphScale
                    glyphScaleAnimationDuration: 100
                    glyphStateColors: root.comicButtonGlyphStateColors
                    stateColors: root.comicButtonStateColors
                    tooltip: qsTrId("qml.next_comic")
                    Accessible.name: qsTrId("qml.next_comic")
                    Accessible.description: qsTrId("qml.show_next_comic")
                    visible: comic.canSwitch
                    enabled: comic.canSwitch
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: root.comicButtonHitSize
                    Layout.preferredHeight: root.comicButtonHitSize
                    onClicked: comic.requestSlide(1)
                }
            }

        }

        footer: DialogFooter {
            visible: !!root.progress && root.progress.cancellable
            choices: [{ id: "cancel", label: root.progress && root.progress.cancelRequested
                         ? qsTrId("qml.cancelling") : qsTrId("action.cancel"),
                        enabled: !!root.progress && !root.progress.cancelRequested }]
            onChosen: root.progress.requestCancel()
        }
    }
}
