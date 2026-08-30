import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import MiaCode.UI

// Renders the file picks and notices that a Widgets-free application service
// asks for. Drop one of these next to any page that owns a UiRequestService and
// bind `requests` to it; the page itself never needs dialog code.
Item {
    id: root

    // A miacode::v2::UiRequestService instance. Null while the owning page has
    // no session yet, which is why Connections guards on it rather than the
    // property being required.
    property var requests: null

    // The request currently shown by fileDialog / folderDialog. A picker can
    // only be open once at a time, so one id per dialog is enough.
    property string activeFileRequestId: ""
    property string activeFolderRequestId: ""
    property string activeNoticeId: ""
    property string activeChoiceId: ""

    visible: false
    width: 0
    height: 0

    function openRequest(requestId, request) {
        if (request.selectFolder) {
            root.activeFolderRequestId = requestId
            folderDialog.title = request.title
            if (request.startPath)
                folderDialog.currentFolder = root.toFolderUrl(request.startPath)
            folderDialog.open()
            return
        }
        root.activeFileRequestId = requestId
        fileDialog.title = request.title
        fileDialog.nameFilters = request.nameFilters && request.nameFilters.length > 0
                                 ? request.nameFilters
                                 : [qsTr("所有文件 (*)")]
        fileDialog.fileMode = request.saveMode ? FileDialog.SaveFile : FileDialog.OpenFile
        if (request.startPath) {
            fileDialog.currentFolder = root.toFolderUrl(root.parentPath(request.startPath))
            fileDialog.selectedFile = root.toFileUrl(request.startPath)
        }
        fileDialog.open()
    }

    function parentPath(path) {
        const separator = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return separator > 0 ? path.substring(0, separator) : path
    }

    function toFileUrl(path) {
        return path.startsWith("file:") ? path : "file://" + path
    }

    function toFolderUrl(path) {
        return root.toFileUrl(path)
    }

    Connections {
        target: root.requests
        function onFileRequested(requestId, request) { root.openRequest(requestId, request) }
        function onChoiceRequested(requestId, request) {
            root.activeChoiceId = requestId
            choiceDialog.title = request.title
            choiceDialog.message = request.text
            choiceDialog.choices = request.choices || []
            choiceDialog.dismissChoiceId = request.dismissChoiceId || ""
            choiceDialog.open()
        }
        function onNoticeRequested(requestId, notice) {
            root.activeNoticeId = requestId
            noticeDialog.title = notice.title
            noticeDialog.text = notice.text
            noticeDialog.informativeText = notice.details
            noticeDialog.actionLabel = notice.actionLabel || ""
            noticeDialog.confirmation = !!notice.confirmation
            noticeDialog.open()
        }
    }

    FileDialog {
        id: fileDialog
        objectName: "uiRequestFileDialog"
        onAccepted: {
            const requestId = root.activeFileRequestId
            root.activeFileRequestId = ""
            root.requests.submitFileResult(requestId, selectedFile)
        }
        onRejected: {
            const requestId = root.activeFileRequestId
            root.activeFileRequestId = ""
            root.requests.cancelFileRequest(requestId)
        }
    }

    FolderDialog {
        id: folderDialog
        objectName: "uiRequestFolderDialog"
        onAccepted: {
            const requestId = root.activeFolderRequestId
            root.activeFolderRequestId = ""
            root.requests.submitFileResult(requestId, selectedFolder)
        }
        onRejected: {
            const requestId = root.activeFolderRequestId
            root.activeFolderRequestId = ""
            root.requests.cancelFileRequest(requestId)
        }
    }

    // Notices are an app-styled Dialog, not QtQuick.Dialogs.MessageDialog:
    // that one renders as the platform's own alert (an NSAlert on macOS), which
    // cannot take the shell's typography or buttons and read as a foreign
    // window in the middle of the app.
    Dialog {
        id: noticeDialog
        objectName: "uiRequestNoticeDialog"

        property string actionLabel: ""
        property bool confirmation: false
        property string informativeText: ""
        property string text: ""

        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 48 : 460)
        closePolicy: Popup.CloseOnEscape

        function resolve(actionChosen) {
            const requestId = root.activeNoticeId
            root.activeNoticeId = ""
            if (requestId.length > 0 && root.requests)
                root.requests.submitNoticeResult(requestId, actionChosen)
        }

        // Dismissing by Escape or the scrim answers the same as declining, so a
        // closed window is never read as consent.
        onRejected: noticeDialog.resolve(false)

        footer: DialogFooter {
            acceptText: noticeDialog.confirmation
                        ? qsTr("是")
                        : (noticeDialog.actionLabel.length > 0 ? noticeDialog.actionLabel : "")
            cancelText: noticeDialog.confirmation
                        ? qsTr("否")
                        : (noticeDialog.actionLabel.length > 0 ? qsTr("关闭") : qsTr("确定"))
            onAccepted: {
                noticeDialog.resolve(true)
                noticeDialog.close()
            }
            onRejected: {
                noticeDialog.resolve(false)
                noticeDialog.close()
            }
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                objectName: "uiRequestNoticeText"
                Layout.fillWidth: true
                text: noticeDialog.text
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                Layout.maximumHeight: 220
                visible: noticeDialog.informativeText.length > 0
                text: noticeDialog.informativeText
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
            }
        }
    }

    // Questions with more than two answers — 保存 / 放弃 / 取消.
    ChoiceDialog {
        id: choiceDialog
        objectName: "uiRequestChoiceDialog"

        onChosen: function(choiceId) {
            const requestId = root.activeChoiceId
            root.activeChoiceId = ""
            if (requestId.length > 0 && root.requests)
                root.requests.submitChoiceResult(requestId, choiceId)
        }
    }
}
