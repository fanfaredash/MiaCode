import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppStickyPopup {
    id: root

    required property var preferences
    required property var previewSession
    openAbove: true
    minimumWidth: 180

    readonly property var mediaHost: root.previewSession ? root.previewSession.mediaHost : null

    contentItem: Column {
        id: body
        width: Math.max(180, freeAspectItem.implicitWidth, hidePvItem.implicitWidth)

        AppMenuItem {
            id: freeAspectItem
            width: body.width
            text: qsTrId("preview.canvas.free_aspect")
            checkable: true
            checked: root.preferences && root.preferences.previewCanvasFreeAspect
            onTriggered: {
                if (!root.preferences)
                    return
                root.preferences.previewCanvasFreeAspect = !root.preferences.previewCanvasFreeAspect
                checked = Qt.binding(() => root.preferences && root.preferences.previewCanvasFreeAspect)
            }
        }

        AppMenuItem {
            id: hidePvItem
            width: body.width
            text: qsTrId("preview.canvas.hide_pv")
            checkable: true
            enabled: !!(root.mediaHost && root.mediaHost.chartHasVideoBackground)
            checked: root.preferences && root.preferences.previewHidePv
            onTriggered: {
                if (!root.preferences || !hidePvItem.enabled)
                    return
                root.preferences.previewHidePv = !root.preferences.previewHidePv
                if (root.mediaHost)
                    root.mediaHost.hidePv = root.preferences.previewHidePv
                checked = Qt.binding(() => root.preferences && root.preferences.previewHidePv)
            }
        }
    }
}
