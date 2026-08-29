import QtQuick
import QtQuick.Dialogs

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

    // A notice with an extra action offers it as Open; plain notices are Ok
    // only. Either way the service is told exactly once which one was chosen.
    MessageDialog {
        id: noticeDialog
        objectName: "uiRequestNoticeDialog"
        property string actionLabel: ""
        property bool confirmation: false
        // A question offers Yes/No; a message with an extra action offers
        // Open/Close; a plain message is Ok only.
        buttons: confirmation
                 ? (MessageDialog.Yes | MessageDialog.No)
                 : (actionLabel.length > 0
                    ? (MessageDialog.Open | MessageDialog.Close)
                    : MessageDialog.Ok)

        function resolve(actionChosen) {
            const requestId = root.activeNoticeId
            root.activeNoticeId = ""
            if (requestId.length > 0 && root.requests)
                root.requests.submitNoticeResult(requestId, actionChosen)
        }

        onButtonClicked: function(button, role) {
            noticeDialog.resolve(noticeDialog.confirmation
                                 ? button === MessageDialog.Yes
                                 : button === MessageDialog.Open)
        }
        onRejected: noticeDialog.resolve(false)
    }
}
