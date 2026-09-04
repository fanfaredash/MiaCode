import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 延迟校准 keeps a synthesized test chart active only while this page is visible.
Rectangle {
    id: root

    required property var latency
    required property var pages

    color: Theme.surfaceColor(Theme.colors.background.panel)
    clip: true

    onVisibleChanged: {
        if (visible)
            root.latency.enter()
        else
            root.latency.leave()
    }

    component SectionHeading: RowLayout {
        id: heading
        required property string title

        Layout.fillWidth: true
        spacing: 10

        Text {
            text: heading.title
            color: Theme.colors.text.active
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.colors.border.soft
        }
    }

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: UiText.text("延迟校准")
        sidebarTitle: true
        showMore: false
    }

    Flickable {
        id: pageFlick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: form.y + form.implicitHeight + 12
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ColumnLayout {
            id: form
            x: Math.max(16, (pageFlick.width - width) / 2)
            y: 12
            width: Math.max(0, Math.min(640, pageFlick.width - 32))
            spacing: 24

            ColumnLayout {
                objectName: "latencyBpmCard"
                Layout.fillWidth: true
                spacing: 10

                SectionHeading {
                    title: UiText.text("BPM")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppTextField {
                        objectName: "latencyBpmField"
                        Layout.preferredWidth: 150
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
                        Layout.preferredWidth: 88
                        text: UiText.text("自动检测")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectBpm()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.latency.bpmDetectResult
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7

                    Text {
                        text: UiText.text("计数拍")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                    AppTextField {
                        objectName: "latencyClockCountField"
                        Layout.preferredWidth: 64
                        text: String(root.latency.clockCount)
                        onEditingFinished: {
                            const parsed = parseInt(text)
                            if (!isNaN(parsed) && parsed > 0)
                                root.latency.clockCount = parsed
                            text = String(root.latency.clockCount)
                        }
                    }
                    Text {
                        text: UiText.text("解码器")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                    AppComboBox {
                        objectName: "latencyDecoderCombo"
                        Layout.preferredWidth: 138
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

            ColumnLayout {
                objectName: "latencyOffsetCard"
                Layout.fillWidth: true
                spacing: 10

                SectionHeading {
                    title: UiText.text("偏移")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppTextField {
                        objectName: "latencyOffsetField"
                        Layout.preferredWidth: 150
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
                        Layout.preferredWidth: 88
                        text: UiText.text("自动检测")
                        enabled: root.latency.trackAvailable
                        onClicked: root.latency.detectOffset()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.latency.offsetDetectResult
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                        elide: Text.ElideRight
                    }
                }
            }

            ColumnLayout {
                objectName: "latencyAuditionCard"
                Layout.fillWidth: true
                spacing: 10

                SectionHeading {
                    title: UiText.text("试听")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppButton {
                        objectName: "latencyAuditionButton"
                        Layout.preferredWidth: 96
                        emphasized: !root.latency.auditionRunning
                        text: root.latency.auditionRunning ? UiText.text("暂停") : UiText.text("开始试听")
                        onClicked: root.latency.toggleAudition()
                    }
                    Text {
                        objectName: "latencyPositionLabel"
                        text: root.latency.positionText
                        color: Theme.colors.text.active
                        font.family: Theme.codeFont.family
                        font.pixelSize: Theme.uiFontSize + 1
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: UiText.text("细分")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
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
                    spacing: 8

                    Text {
                        text: UiText.text("音效音量")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
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
                        Layout.preferredWidth: 36
                        horizontalAlignment: Text.AlignRight
                        text: root.latency.sfxVolumePercent + "%"
                        color: Theme.colors.text.active
                        font.family: Theme.codeFont.family
                    }
                }
            }
        }

        ScrollBar.vertical: AppScrollBar {}
    }
}
