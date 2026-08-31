pragma Singleton

import QtQuick

// The QML-facing half of the canonical UiText catalog. Keeping this as a QML
// singleton (instead of registering a process-global C++ singleton) makes the
// source-mirrored QML specs load the exact same import surface. Main.qml binds
// `provider` before the window becomes visible; the source fallback keeps
// isolated component specs deterministic.
QtObject {
    property var provider: null

    function text(source) {
        return provider ? provider.localizedText(source) : source
    }
}
