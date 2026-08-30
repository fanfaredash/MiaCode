#include <QFile>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined as the absolute repository root"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString sourceFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT "/") + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool containsAfter(const QString& source, const QString& first, const QString& second)
{
    const int firstPosition = source.indexOf(first);
    return firstPosition >= 0 && source.indexOf(second, firstPosition + first.size()) >= 0;
}

bool verifyBackendReplacementPublishesOneQmlRefresh(QTextStream& err)
{
    const QString mainWindow = sourceFile(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    const QString documentUi = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainView = sourceFile(QStringLiteral("src/app/qml_ui/layout/MainView.qml"));

    return require(mainWindow.contains(QStringLiteral("void documentReplaced();")),
                   QStringLiteral("MainWindow exposes a backend document replacement notification"), err)
        && require(containsAfter(documentUi,
                                 QStringLiteral("void MainWindow::DocumentSection::loadDocument"),
                                 QStringLiteral("emit owner_.documentReplaced();")),
                   QStringLiteral("every loadDocument path publishes the backend replacement after loading"), err)
        && require(containsAfter(documentModel,
                                 QStringLiteral("&MainWindow::documentReplaced"),
                                 QStringLiteral("clearMetadataSourceRejection();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("Qt::QueuedConnection"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("emitDocumentStateChanged();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&MainWindow::documentReplaced"),
                                        QStringLiteral("emit documentReplaced();")),
                   QStringLiteral("backend replacements wait for the completed transaction, clear rejected source state, and publish the complete QML document snapshot"), err)
        && require(containsAfter(mainView,
                                 QStringLiteral("function onDocumentReplaced()"),
                                 QStringLiteral("state.resetEditorTabs(root.documentSession.currentDifficultyId)")),
                   QStringLiteral("a QML replacement resets active editors so chart text and bookmarks are rederived"), err);
}

bool verifyNewDocumentPublishesFinalFileIdentity(QTextStream& err)
{
    const QString fileFlow = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentFileFlow.cpp"));
    const int newDocument = fileFlow.indexOf(QStringLiteral("const SimaiDocument newDocument"));
    const int end = fileFlow.indexOf(QStringLiteral("namespace {"), newDocument);
    const QString createFlow = newDocument >= 0
        ? fileFlow.mid(newDocument, (end >= 0 ? end : fileFlow.size()) - newDocument)
        : QString();
    return require(containsAfter(createFlow,
                                 QStringLiteral("owner_.setCurrentFilePath(targetPath)"),
                                 QStringLiteral("loadDocument(newDocument)")),
                   QStringLiteral("new-document replacement publishes after the final file identity is installed"), err);
}

bool verifyV2AnalysisUsesWorkspaceSnapshot(QTextStream& err)
{
    const QString applicationContext = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlApplicationContext.h"));
    const QString analysisServiceHeader = sourceFile(
        QStringLiteral("src/app/v2/AnalysisService.h"));
    const QString analysisService = sourceFile(
        QStringLiteral("src/app/v2/AnalysisService.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString analysisModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlAnalysisModel.cpp"));

    return require(applicationContext.contains(
                       QStringLiteral("AnalysisService analysisService_;")),
                   QStringLiteral("the production QML context owns one workspace analysis service"), err)
        && require(analysisServiceHeader.contains(QStringLiteral("bool pending = false;"))
                       && analysisServiceHeader.contains(QStringLiteral("SimaiNativeValidationReport validation;"))
                       && analysisServiceHeader.contains(QStringLiteral("QVector<TimelineNoteMarker> noteMarkers;"))
                       && analysisServiceHeader.contains(QStringLiteral("MuriAnalysisReport muri;"))
                       && analysisServiceHeader.contains(QStringLiteral("QVector<MuriStaticReference> muriStaticReferences;")),
                   QStringLiteral("pending, validation, markers, Muri, and static references share one snapshot value"), err)
        && require(analysisService.contains(QStringLiteral("&ChartWorkspace::changed"))
                       && containsAfter(analysisService,
                                        QStringLiteral("snapshot_ = std::move(pending);"),
                                        QStringLiteral("emit snapshotChanged"))
                       && analysisService.contains(QStringLiteral("identityIsCurrent(")),
                   QStringLiteral("workspace changes publish pending before async work and gate completion by identity"), err)
        && require(!analysisService.contains(QStringLiteral("QtWidgets"))
                       && !analysisServiceHeader.contains(QStringLiteral("QtWidgets"))
                       && !analysisService.contains(QStringLiteral("#include \"mainwindow/"))
                       && !analysisServiceHeader.contains(QStringLiteral("#include \"mainwindow/")),
                   QStringLiteral("AnalysisService stays Widgets-free and independent of MainWindow"), err)
        && require(documentModel.contains(QStringLiteral("analysisService_->snapshot()"))
                       && documentModel.contains(QStringLiteral("analysisService_->requestAnalysis()"))
                       && !documentModel.contains(QStringLiteral("backend_->documentValidationSnapshot()"))
                       && !documentModel.contains(QStringLiteral("backend_->validateActiveDocument()")),
                   QStringLiteral("document diagnostics and explicit validation consume the workspace service"), err)
        && require(analysisModel.contains(QStringLiteral("analysisService_->snapshot()"))
                       && analysisModel.contains(QStringLiteral("workspace_->snapshot()"))
                       && !analysisModel.contains(QStringLiteral("backend_->qmlAnalysisSnapshot()")),
                   QStringLiteral("QML analysis gates the service snapshot against the current workspace pair"), err);
}

bool verifyWorkspaceOwnsProductionDocumentAndDirty(QTextStream& err)
{
    const QString applicationContext = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlApplicationContext.h"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainWindow = sourceFile(
        QStringLiteral("src/app/mainwindow/MainWindow.h"));
    const QString documentBridge = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentBridge.cpp"));
    const QString fileFlow = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentAutosaveFlow.cpp"));
    const QString timelineFlow = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp"));
    const QString followSync = sourceFile(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.TimelinePreviewFollowSync.cpp"));

    return require(
               applicationContext.contains(QStringLiteral("ChartWorkspace workspace_;"))
                   && applicationContext.contains(
                       QStringLiteral("ChartWorkspaceFileService fileService_;")),
               QStringLiteral("QmlApplicationContext owns one workspace and its file boundary"), err)
        && require(documentModel.contains(
                       QStringLiteral("workspace_->replaceActiveDifficultyChart(value)"))
                       && documentModel.contains(
                           QStringLiteral("workspace_->updateDocumentField("))
                       && documentModel.contains(
                           QStringLiteral("workspace_->updateDifficultyField("))
                       && documentModel.contains(QStringLiteral("fileService_->open(path)"))
                       && documentModel.contains(QStringLiteral("fileService_->save(saveSectionDifficultyId())")),
                   QStringLiteral("production QML body, metadata, difficulty, and file operations submit workspace transactions"), err)
        && require(!documentModel.contains(QStringLiteral("backend_->isWindowModified()"))
                       && !documentModel.contains(
                           QStringLiteral("backend_->updateActiveChartText(value)"))
                       && !documentModel.contains(
                           QStringLiteral("backend_->updateDocumentField("))
                       && !documentModel.contains(
                           QStringLiteral("backend_->updateDifficultyField("))
                       && !documentModel.contains(
                           QStringLiteral("backend_->saveDocument()")),
                   QStringLiteral("QmlDocumentModel never asks MainWindow to own a mutation or determine dirty"), err)
        && require(mainWindow.contains(QStringLiteral("applyCommittedQmlDocument("))
                       && documentBridge.contains(
                           QStringLiteral("revision <= owner_.appliedQmlWorkspaceRevision_"))
                       && containsAfter(documentBridge,
                                        QStringLiteral("state_.document_ = committedDocument;"),
                                        QStringLiteral("state_.documentDirty_ = dirty;")),
                   QStringLiteral("MainWindow accepts only monotonic committed adapter values and mirrors workspace dirty"), err)
        // What the gate DOES with the pair is a behaviour regression against the
        // real object (editor_sync_controller_spec); it used to be an
        // `appliedQmlWorkspaceRevision_ > 0` scan here, which went red when the
        // guard moved into EditorSyncController — a scan cannot tell a moved
        // guard from a deleted one. What stays here is the half a source
        // contract can actually see: both publishers read the revision
        // MainWindow last accepted from the workspace. The follow path once
        // sent the validation snapshot's revision instead — a different counter
        // that matched only by coincidence, and silently disabled 代码跟随 for
        // the rest of the session once a difficulty switch separated the two.
        && require(documentModel.contains(
                       QStringLiteral("publishWorkspaceCommit(WorkspaceCommitKind::Incremental);"))
                       && timelineFlow.contains(
                           QStringLiteral("editorSyncController_->requestNavigation("))
                       && timelineFlow.contains(QStringLiteral("appliedQmlWorkspaceRevision_"))
                       && followSync.contains(
                           QStringLiteral("follow.revision = owner_.appliedQmlWorkspaceRevision_"))
                       && !followSync.contains(QStringLiteral("documentValidationSnapshot()")),
                   QStringLiteral("initial, navigation, and follow identities stay on the committed workspace revision"), err)
        && require(fileFlow.contains(QStringLiteral("owner_.qmlDocumentSaveHandler_(path)")),
                   QStringLiteral("legacy close-save routing delegates durable writes to the workspace file service"), err);
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = verifyBackendReplacementPublishesOneQmlRefresh(err)
        && verifyNewDocumentPublishesFinalFileIdentity(err)
        && verifyV2AnalysisUsesWorkspaceSnapshot(err)
        && verifyWorkspaceOwnsProductionDocumentAndDirty(err);
    if (ok) {
        out << "qml_document_lifecycle_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
