import QtQuick

QtObject {
    id: root

    property var paletteMap: ({})
    property var metricsMap: ({})
    property bool fullscreenMode: false

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    function transportSurfaceColor() {
        return fullscreenMode ? "#C8141B22" : tone("cardBg", "#ffffff")
    }

    function transportBorderColor() {
        return fullscreenMode ? "#3AFFFFFF" : tone("border", "#d5e0ec")
    }

    function transportPrimaryTextColor() {
        return fullscreenMode ? "#F2F7FF" : tone("textPrimary", "#203040")
    }

    function transportTrackColor() {
        return fullscreenMode ? "#34404D" : tone("inputDisabledBg", "#e3e8ef")
    }

    function transportHandleFillColor() {
        return fullscreenMode ? "#F7FBFF" : "white"
    }

    function transportHandleBorderColor() {
        return fullscreenMode ? "#5D748E" : tone("borderSoft", "#ccd6e2")
    }

    function transportButtonFillColor(down) {
        if (!fullscreenMode)
            return down ? tone("menuHoverBg", "#eef5ff") : "transparent"
        return down ? "#2A3542" : "#1A222B"
    }

    function transportButtonBorderColor() {
        return fullscreenMode ? "#40FFFFFF" : tone("border", "#d5e0ec")
    }
}
