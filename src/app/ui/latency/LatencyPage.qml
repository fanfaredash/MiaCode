import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var latency
    required property var pages

    color: Theme.surfaceColor(Theme.colors.background.panel)
    clip: true

    // The form rows share one label column, the way the export and cover
    // pages do, so every control's left edge lines up down the page.
    readonly property int labelWidth: 96

    component FormLabel: Text {
        Layout.preferredWidth: root.labelWidth
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        wrapMode: Text.WordWrap
    }

    component DetectResult: Text {
        Layout.fillWidth: true
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        elide: Text.ElideRight
    }

    Flickable {
        id: pageFlick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: form.y + form.implicitHeight + 12
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: form
            x: 16
            y: 12
            width: Math.max(0, Math.min(560, pageFlick.width - 32))
            spacing: 12

            SettingsSection {
                objectName: "latencyBpmCard"
                Layout.fillWidth: true
                title: qsTrId("qml.bpm")
                first: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    FormLabel { text: qsTrId("qml.bpm") }
                    AppTextField {
                        objectName: "latencyBpmField"
                        Layout.preferredWidth: 140
                        text: root.latency.bpm.toFixed(3)
                        onEditingFinished: {
                            const parsed = parseFloat(text)
                            if (!isNaN(parsed) && parsed > 0)
                                root.latency.bpm = parsed
                            text = root.latency.bpm.toFixed(3)
                        }
                    }
                    AppButton {
                        objectName: "latencyDetectBpmButton"
                        text: qsTrId("latency.auto_detect")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectBpm()
                    }
                    DetectResult { text: root.latency.bpmDetectResult }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    FormLabel { text: qsTrId("qml.count_in_beats") }
                    AppTextField {
                        objectName: "latencyClockCountField"
                        Layout.preferredWidth: 140
                        text: String(root.latency.clockCount)
                        onEditingFinished: {
                            const parsed = parseInt(text)
                            if (!isNaN(parsed) && parsed > 0)
                                root.latency.clockCount = parsed
                            text = String(root.latency.clockCount)
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                LabeledCombo {
                    objectName: "latencyDecoderCombo"
                    label: qsTrId("qml.decoder")
                    labelWidth: root.labelWidth
                    spacing: 8
                    options: root.latency.audioDecoderOptions
                    currentValue: root.latency.audioDecoder
                    onPicked: function(value) { root.latency.audioDecoder = value }
                }
            }

            SettingsSection {
                objectName: "latencyOffsetCard"
                Layout.fillWidth: true
                title: qsTrId("latency.offset")

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    FormLabel { text: qsTrId("latency.offset") }
                    AppTextField {
                        objectName: "latencyOffsetField"
                        Layout.preferredWidth: 140
                        text: root.latency.offsetSeconds.toFixed(3)
                        onEditingFinished: {
                            const parsed = parseFloat(text)
                            if (!isNaN(parsed))
                                root.latency.offsetSeconds = parsed
                            text = root.latency.offsetSeconds.toFixed(3)
                        }
                    }
                    AppButton {
                        objectName: "latencyDetectOffsetButton"
                        text: qsTrId("latency.auto_detect")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectOffset()
                    }
                    DetectResult { text: root.latency.offsetDetectResult }
                }
            }

            // The playhead readout already sits in the preview panel's transport,
            // which drives this same audition, so the section does not repeat it.
            SettingsSection {
                objectName: "latencyAuditionCard"
                Layout.fillWidth: true
                title: qsTrId("dialog.render_settings.music.audition")

                AppButton {
                    objectName: "latencyAuditionButton"
                    emphasized: !root.latency.auditionRunning
                    text: root.latency.auditionRunning ? qsTrId("preview.pause") : qsTrId("qml.start_audition")
                    onClicked: root.latency.toggleAudition()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    FormLabel {
                        text: qsTrId("qml.subdivision")
                        Layout.rightMargin: 4
                    }
                    AppTab {
                        panelTab: true
                        text: "1/4"
                        active: root.latency.subdivision === 4
                        onClicked: root.latency.subdivision = 4
                    }
                    AppTab {
                        panelTab: true
                        text: "1/8"
                        active: root.latency.subdivision === 8
                        onClicked: root.latency.subdivision = 8
                    }
                    Item { Layout.fillWidth: true }
                }

                LabeledSlider {
                    objectName: "latencySfxVolumeSlider"
                    label: qsTrId("qml.sound_effect_volume")
                    labelWidth: root.labelWidth
                    spacing: 8
                    from: 0
                    to: 100
                    stepSize: 1
                    value: root.latency.sfxVolumePercent
                    onMoved: function(value) { root.latency.sfxVolumePercent = Math.round(value) }
                }
            }
        }

        ScrollBar.vertical: AppScrollBar {}
    }
}
