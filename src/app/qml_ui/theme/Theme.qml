pragma Singleton

import QtQuick

QtObject {
    property var preferences: null
    property var appBackground: null
    readonly property bool darkTheme: preferences ? preferences.darkTheme : true

    property var colors: ({
        background: {
            surface: "#191A1B",
            editor: "#121314",
            elevated: "#202122"
        },
        border: {
            normal: "#2A2B2C",
            control: "#333536"
        },
        text: {
            // Navigation: selected ≈ 1.0, idle ≈ 0.75 of active.
            active: "#E8E8E8",
            primary: "#BFBFBF",
            secondary: "#AEAEAE",
            disabled: "#6E6E6E",
            editor: "#BBBEBF",
            lineNumber: "#858889",
            heading: "#FFFFFF",
            onAccent: "#FFFFFF"
        },
        accent: {
            primary: "#3994BC",
            badge: "#307E9F",
            soft: "#A5D6FF",
            focus: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0xB3 / 255)
        },
        state: {
            // Row chrome: gray lifts only (no accent tint on hover/selection).
            hover: Qt.rgba(1, 1, 1, 0x2A / 512),
            pressed: Qt.rgba(1, 1, 1, 0x40 / 512),
            selected: Qt.rgba(1, 1, 1, 0x36 / 512),

            // Editor text selection (not row chrome).
            menuSelection: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0x26 / 255),
            textSelection: Qt.rgba(0x27 / 255, 0x67 / 255, 0x82 / 255, 0xDD / 255),
            lineHighlight: "#2A2B2C"
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
    // 行距, in the pixels of bottom margin each text block carries.
    readonly property int codeBlockSpacing: preferences ? preferences.editorBlockSpacing : 0
    readonly property int uiFontSize: preferences ? preferences.fontSize : 13
    readonly property int headingFontSize: uiFontSize + 1
    readonly property int secondaryFontSize: uiFontSize - 1
    readonly property int captionFontSize: uiFontSize - 3

    function overlayAlpha(token) {
        const model = appBackground
        if (!model || !model.imageReadable || token === "card")
            return 1.0
        const value = model[token + "Alpha" + (darkTheme ? "Dark" : "Light")]
        return Math.max(0, Math.min(255, Number(value))) / 255.0
    }

    function surfaceColor(token, baseColor) {
        if (!appBackground || !appBackground.imageReadable || token === "card")
            return baseColor
        const c = Qt.color(baseColor)
        return Qt.rgba(c.r, c.g, c.b, overlayAlpha(token))
    }

    // Geometry aligned with v1 UiTheme dialog* sheets (colors stay local).
    readonly property int controlRadius: 6
    readonly property int itemRadius: 6
    readonly property int controlMinHeight: 30
    readonly property int controlBorderWidth: 1
    readonly property int menuPadding: 7
    // Default inset so adjacent HoverChrome pills do not touch.
    readonly property int chromeInsetX: 3
    readonly property int chromeInsetY: 2
    // Content inset for a chromed row. HoverChrome insets itself from the
    // control but never the content, so ChromeRow spends this on padding to
    // keep text off the highlight edge.
    readonly property int rowPaddingX: 10
    // SplitView handle: 1px layout (same as non-interactive dividers),
    // wider invisible hit, thicker stroke only while hovered/pressed.
    readonly property int splitDividerThickness: 1
    readonly property int splitHandleActiveThickness: 3
    readonly property int splitHandleHitExtent: 9
    readonly property int activityButtonSize: 48
    readonly property int activityIconSize: 24
    readonly property int activityIconTop: Math.round((activityButtonSize - activityIconSize) * 0.5)

    // Per-difficulty swatch, matching v1 difficultyColor() in MainWindowShared.
    readonly property var difficultyColors: [
        "#69A6FF", "#78C85A", "#DCC548", "#E35C50", "#7A4FD1", "#D548B6", "#E29A46"
    ]
    readonly property color difficultyColorFallback: "#8A8F98"
    readonly property int difficultySwatchSize: 10
    readonly property int difficultySwatchRadius: 3
    function difficultyColor(difficultyId) {
        return difficultyId >= 1 && difficultyId <= difficultyColors.length
            ? difficultyColors[difficultyId - 1]
            : difficultyColorFallback
    }
}
