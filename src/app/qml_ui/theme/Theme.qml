pragma Singleton

import QtQuick

QtObject {
    property var preferences: null
    property var appBackground: null
    readonly property bool darkTheme: preferences ? preferences.darkTheme : true

    property var colors: ({
        background: {
            surface: "#121314",
            panel: "#191A1B",
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
            // Ordinary notes: same full brightness as text.active, not a muted gray.
            editor: "#E8E8E8",
            lineNumber: "#858889",
            heading: "#FFFFFF",
            onAccent: "#FFFFFF"
        },
        accent: {
            primary: "#297AA0",
            badge: "#307E9F",
            soft: "#A5D6FF",
            focus: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0xB3 / 255)
        },
        scroll: {
            handle: "#5E6062",
            handleHover: "#7A7C7E"
        },
        state: {
            // Solid UI state fills, independent of the surface underneath.
            hover: "#1E2021",
            pressed: "#282A2B",
            selected: "#232526",

            // Editor text selection (not row chrome).
            menuSelection: Qt.rgba(0x39 / 255, 0x94 / 255, 0xBC / 255, 0x26 / 255),
            textSelection: Qt.rgba(0x27 / 255, 0x67 / 255, 0x82 / 255, 0xDD / 255),
            followHighlight: "#52B0D8",
            lineHighlight: "#2A2B2C",
            focusLine: "#232425",
            selectionHighlight: "#3E4042"
        },
        popupState: {
            hover: "#2C2D2E",
            pressed: "#3A3B3C",
            selected: "#333536"
        },
        syntax: {
            keyword: "#F5AE9C",
            comment: "#71B77A",
            duration: "#A0B6FF",
            modifier: "#D2A8FF",
            error: "#C62828",
            warning: "#B07B00"
        },
        // 时间轴(QSG)外壳颜色。时间轴是原生绘制，由 TimelineThemeBridge
        // 把这里的分组读入 C++ 快照；浅色第二套尚未实现，全部保持深色值。
        // 与外壳同值的条目（window/base/border/label/textSecondary）须与
        // 上方 background / border / text 各角色保持一致。
        timeline: {
            window: "#191A1B",
            header: "#191A1B",
            sidebar: "#191A1B",
            base: "#121314",
            border: "#2A2B2C",
            axis: "#6E6E6E",
            gridMajor: "#6E6E6E",
            gridSubdivision: Qt.rgba(42 / 255, 43 / 255, 44 / 255, 140 / 255),
            gridMinor: Qt.rgba(42 / 255, 43 / 255, 44 / 255, 70 / 255),
            laneEven: Qt.rgba(1, 1, 1, 11 / 255),
            laneOdd: Qt.rgba(1, 1, 1, 5 / 255),
            label: "#AEAEAE",
            textSecondary: "#AEAEAE",
            waveStroke: Qt.rgba(57 / 255, 148 / 255, 188 / 255, 144 / 255)
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

    // Shared UI geometry.
    readonly property int controlRadius: 6
    readonly property int itemRadius: controlRadius
    readonly property int controlMinHeight: 30
    readonly property int compactControlHeight: 24
    readonly property int compactFontSize: uiFontSize - 2
    readonly property int panelPadding: 8
    readonly property int controlBorderWidth: 1
    readonly property int menuPadding: 7
    // Default inset so adjacent HoverChrome pills do not touch.
    readonly property int chromeInsetX: 3
    readonly property int chromeInsetY: 2
    readonly property int chromePadding: 4
    readonly property int chromeMinSize: 24
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
