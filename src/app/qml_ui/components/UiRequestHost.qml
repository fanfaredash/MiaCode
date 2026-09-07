import QtQuick
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
                                 : [UiText.text("所有文件 (*)")]
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
            noticeDialog.message = notice.text
            noticeDialog.details = notice.details
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

    ChoiceDialog {
        id: noticeDialog
        objectName: "uiRequestNoticeDialog"

        property string actionLabel: ""
        property bool confirmation: false
        dismissChoiceId: "reject"
        choices: {
            const actions = [{ id: "reject", label: confirmation ? UiText.text("否")
                                : (actionLabel.length > 0 ? UiText.text("关闭") : UiText.text("确定")) }]
            if (confirmation || actionLabel.length > 0)
                actions.push({ id: "accept", label: confirmation ? UiText.text("是") : actionLabel,
                               role: "accept" })
            return actions
        }

        onChosen: function(choiceId) {
            const requestId = root.activeNoticeId
            root.activeNoticeId = ""
            Qt.callLater(function() {
                if (requestId.length > 0 && root.requests)
                    root.requests.submitNoticeResult(requestId, choiceId === "accept")
            })
        }
    }

    // Questions with more than two answers — 保存 / 放弃 / 取消.
    ChoiceDialog {
        id: choiceDialog
        objectName: "uiRequestChoiceDialog"

        onChosen: function(choiceId) {
            const requestId = root.activeChoiceId
            root.activeChoiceId = ""
            // ChoiceDialog emits chosen before its resolve() call closes the
            // popup. The continuation can synchronously ask the next question;
            // submitting now would open it, then let the old popup's close()
            // immediately hide it. Let that close finish before advancing the
            // request queue.
            Qt.callLater(function() {
                if (requestId.length > 0 && root.requests)
                    root.requests.submitChoiceResult(requestId, choiceId)
            })
        }
    }
}
