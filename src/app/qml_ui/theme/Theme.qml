pragma Singleton

import QtQuick

QtObject {
    // 应用宿主注入视图偏好；独立 QML 工具在缺省配置下仍可创建主题。
    property var preferences: null

    property var colors: ({
        background: {
            workbench: "#191A1B",
            editor: "#121314",
            elevated: "#202122"
        },
        border: {
            normal: "#2A2B2C",
            control: "#333536"
        },
        text: {
            primary: "#BFBFBF",
            editor: "#BBBEBF",
            secondary: "#8C8C8C",
            lineNumber: "#858889",
            onAccent: "#FFFFFF"
        },
        accent: {
            primary: "#3994BC",
            badge: "#307E9F",
            soft: "#A5D6FF",
            focus: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0xB3 / 255)
        },
        state: {
            hover: Qt.rgba(1, 1, 1, 0x14 / 255),
            pressed: Qt.rgba(1, 1, 1, 0x33 / 255),
            menuSelection: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0x26 / 255),
            textSelection: Qt.rgba(0x27 / 255, 0x67 / 255, 0x82 / 255, 0xDD / 255),
            lineHighlight: "#242526"
        },
        syntax: {
            keyword: "#F29A83",
            comment: "#71B77A",
            duration: "#88A4FF",
            modifier: "#D2A8FF",
            error: "#C62828",
            warning: "#B07B00"
        }
    })

    readonly property string uiFont: preferences ? preferences.uiFontFamily : ""
    readonly property font codeFont: preferences ? preferences.codeFont : Qt.font({})
    readonly property int uiFontSize: preferences ? preferences.fontSize : 13
    readonly property int secondaryFontSize: uiFontSize - 1
}

