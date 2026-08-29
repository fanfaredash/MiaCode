import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 延迟校准. Three cards — BPM, offset, audition — over a sandbox controller that
// swaps in a synthesized test chart while the page is open and restores the real
// one on the way out. The page itself owns no playback state; enter/leave drive
// that, so leaving by any route tears the sandbox down.
Rectangle {
    id: root

    required property var latency
    required property var pages

    color: Theme.colors.background.surface
    clip: true

    onVisibleChanged: {
        if (visible)
            root.latency.enter()
        else
            root.latency.leave()
    }

    component Card: Rectangle {
        id: card
        default property alias content: cardColumn.data
        required property string title
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 44
        radius: 8
        color: Theme.colors.background.elevated
        border.width: 1
        border.color: Theme.colors.border.normal
        Text {
            id: cardTitle
            x: 16
            y: 14
            text: card.title
            color: Theme.colors.text.active
            font.family: Theme.uiFont
            font.bold: true
        }
        ColumnLayout {
            id: cardColumn
            anchors.top: cardTitle.bottom
            anchors.topMargin: 10
            x: 16
            width: card.width - 32
            spacing: 8
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 12

            Card {
                objectName: "latencyBpmCard"
                title: qsTr("BPM")
                RowLayout {
                    Layout.fillWidth: true
                    AppTextField {
                        objectName: "latencyBpmField"
                        Layout.preferredWidth: 120
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
                        text: qsTr("自动检测")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectBpm()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.latency.bpmDetectResult
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        elide: Text.ElideRight
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("计数拍")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                    }
                    AppTextField {
                        objectName: "latencyClockCountField"
                        Layout.preferredWidth: 80
                        text: String(root.latency.clockCount)
                        onEditingFinished: {
                            const parsed = parseInt(text)
                            if (!isNaN(parsed) && parsed > 0)
                                root.latency.clockCount = parsed
                            text = String(root.latency.clockCount)
                        }
                    }
                    Text {
                        text: qsTr("解码器")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                    }
                    AppComboBox {
                        objectName: "latencyDecoderCombo"
                        Layout.preferredWidth: 140
                        textRole: "label"
                        model: root.latency.audioDecoderOptions
                        currentIndex: root.latency.audioDecoder === "bass" ? 1 : 0
                        onActivated: function(index) {
                            root.latency.audioDecoder = root.latency.audioDecoderOptions[index].value
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            Card {
                objectName: "latencyOffsetCard"
                title: qsTr("偏移")
                RowLayout {
                    Layout.fillWidth: true
                    AppTextField {
                        objectName: "latencyOffsetField"
                        Layout.preferredWidth: 120
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
                        text: qsTr("自动检测")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectOffset()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.latency.offsetDetectResult
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        elide: Text.ElideRight
                    }
                }
            }

            Card {
                objectName: "latencyAuditionCard"
                title: qsTr("试听")
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "latencyAuditionButton"
                        emphasized: !root.latency.auditionRunning
                        text: root.latency.auditionRunning ? qsTr("暂停") : qsTr("开始试听")
                        onClicked: root.latency.toggleAudition()
                    }
                    Text {
                        objectName: "latencyPositionLabel"
                        text: root.latency.positionText
                        color: Theme.colors.text.active
                        font.family: Theme.codeFont.family
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("细分")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
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
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("音效音量")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                    }
                    AppSlider {
                        objectName: "latencySfxVolumeSlider"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: root.latency.sfxVolumePercent
                        onMoved: root.latency.sfxVolumePercent = Math.round(value)
                    }
                    Text {
                        text: root.latency.sfxVolumePercent + "%"
                        color: Theme.colors.text.active
                        font.family: Theme.uiFont
                    }
                }
            }
        }
    }
}
