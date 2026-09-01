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
    const QString mainWindow = sourceFile(QStringLiteral("src/app/runtime/Session.h"));
    const QString documentUi = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentPages.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainView = sourceFile(QStringLiteral("src/app/qml_ui/layout/MainView.qml"));

    return require(mainWindow.contains(QStringLiteral("void documentReplaced();")),
                   QStringLiteral("Session exposes a backend document replacement notification"), err)
        && require(containsAfter(documentUi,
                                 QStringLiteral("void miacode::runtime::DocumentSessionHost::loadDocument"),
                                 QStringLiteral("emit session_.documentReplaced();")),
                   QStringLiteral("every loadDocument path publishes the backend replacement after loading"), err)
        && require(containsAfter(documentModel,
                                 QStringLiteral("&miacode::v2::ShellNotifications::documentReplaced"),
                                 QStringLiteral("clearMetadataSourceRejection();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&miacode::v2::ShellNotifications::documentReplaced"),
                                        QStringLiteral("Qt::QueuedConnection"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&miacode::v2::ShellNotifications::documentReplaced"),
                                        QStringLiteral("emitDocumentStateChanged();"))
                       && containsAfter(documentModel,
                                        QStringLiteral("&miacode::v2::ShellNotifications::documentReplaced"),
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
        QStringLiteral("src/app/runtime/document/DocumentFileFlow.cpp"));
    const int newDocument = fileFlow.indexOf(QStringLiteral("const SimaiDocument newDocument"));
    const int end = fileFlow.indexOf(QStringLiteral("namespace {"), newDocument);
    const QString createFlow = newDocument >= 0
        ? fileFlow.mid(newDocument, (end >= 0 ? end : fileFlow.size()) - newDocument)
        : QString();
    return require(containsAfter(createFlow,
                                 QStringLiteral("session_.setCurrentFilePath(targetPath)"),
                                 QStringLiteral("workspace().openSource(newDocument.toText(), targetPath)"))
                       && containsAfter(createFlow,
                                        QStringLiteral("workspace().openSource(newDocument.toText(), targetPath)"),
                                        QStringLiteral("loadDocument();")),
                   QStringLiteral("new-document replacement opens the workspace then refreshes hidden UI after the file identity is installed"), err);
}

bool verifyV2AnalysisUsesWorkspaceSnapshot(QTextStream& err)
{
    const QString applicationContext = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlApplicationContext.cpp"));
    const QString applicationServices = sourceFile(
        QStringLiteral("src/app/v2/ApplicationServices.h"));
    const QString analysisServiceHeader = sourceFile(
        QStringLiteral("src/app/v2/AnalysisService.h"));
    const QString analysisService = sourceFile(
        QStringLiteral("src/app/v2/AnalysisService.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString analysisModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlAnalysisModel.cpp"));

    // Stage 3.5 item 1 moved the single analysis service from the QML context to
    // miacode::v2::ApplicationServices. The contract is unchanged in substance —
    // one workspace, one analysis service, both reached by reference — but the
    // owner is now a non-Widget assembly rather than the QML context, so the
    // context must take them from `services` instead of declaring its own.
    return require(applicationServices.contains(QStringLiteral("AnalysisService analysis_;"))
                       && applicationServices.contains(QStringLiteral("ChartWorkspace workspace_;")),
                   QStringLiteral("the application assembly owns one workspace analysis service"), err)
        && require(applicationContext.contains(
                       QStringLiteral("services.workspace(), services.analysis()"))
                       && !applicationContext.contains(QStringLiteral("analysisService_(")),
                   QStringLiteral("the production QML context reaches the assembly's single "
                                  "workspace and analysis service instead of building its own"), err)
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
        QStringLiteral("src/app/qml_ui/QmlApplicationContext.cpp"));
    const QString applicationServices = sourceFile(
        QStringLiteral("src/app/v2/ApplicationServices.h"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainWindow = sourceFile(
        QStringLiteral("src/app/runtime/Session.h"));
    const QString memberStorage = sourceFile(
        QStringLiteral("src/app/runtime/SessionMembers.inc"));
    const QString documentBridge = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentBridge.cpp"));
    const QString fileFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentAutosave.cpp"));
    const QString timelineFlow = sourceFile(
        QStringLiteral("src/app/runtime/playback/TimelineFlow.cpp"));
    const QString followSync = sourceFile(
        QStringLiteral("src/app/runtime/playback/FollowSync.cpp"));
    const QString frameBootstrap = sourceFile(
        QStringLiteral("src/app/runtime/SessionBootstrap.cpp"));
    const QString documentFileFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFileFlow.cpp"));

    // The owner moved to the non-Widget assembly in stage 3.5 item 1; the count
    // is what matters and it is still one apiece, reached by reference.
    return require(
               applicationServices.contains(QStringLiteral("ChartWorkspace workspace_;"))
                   && applicationServices.contains(
                       QStringLiteral("ChartWorkspaceFileService files_;"))
                   && applicationContext.contains(
                       QStringLiteral("services.workspace(), services.files()")),
               QStringLiteral("one workspace and one file boundary, owned by the application "
                              "assembly and reached by the QML context"), err)
        && require(documentModel.contains(
                       QStringLiteral("workspace_->replaceActiveDifficultyChart(value)"))
                       && documentModel.contains(
                           QStringLiteral("workspace_->updateDocumentField("))
                       && documentModel.contains(
                           QStringLiteral("workspace_->updateDifficultyField("))
                       && documentModel.contains(QStringLiteral("fileService_->open(path)"))
                       && documentModel.contains(QStringLiteral("fileService_->save(saveSectionDifficultyId())")),
                   QStringLiteral("production QML body, metadata, difficulty, and file operations submit workspace transactions"), err)
        && require(documentBridge.contains(QStringLiteral("noteDocumentEditedForAutosave()"))
                       && documentBridge.contains(QStringLiteral("sourceChanged && dirty"))
                       && documentBridge.contains(QStringLiteral("resetAutosaveState(sourceText)")),
                   QStringLiteral("a v2 edit arms the per-edit autosave and the crash snapshot, "
                                  "which left with the hidden chart editor that used to drive them"), err)
        && require(documentModel.contains(
                       QStringLiteral("void QmlDocumentModel::saveSectionOrAskForPath"))
                       && documentModel.contains(QStringLiteral("request.saveMode = true"))
                       && documentModel.contains(
                           QStringLiteral("saveSectionOrAskForPath(\n                    difficultyId,"))
                       && documentModel.contains(QStringLiteral("saveSectionOrAskForPath(0,")),
                   QStringLiteral("saving from the leave flow asks for a path when the document has none, "
                                  "instead of refusing an empty path in silence"), err)
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
        && require(!memberStorage.contains(QStringLiteral("SimaiDocument document_"))
                       && !documentBridge.contains(QStringLiteral("state_.document_ ="))
                       && !mainWindow.contains(QStringLiteral("applyChartTextThroughWorkspace(")),
                   QStringLiteral("Session keeps no SimaiDocument and adds no workspace write-through helper"), err)
        && require(!documentModel.contains(QStringLiteral("applyCommittedDocument("))
                       && frameBootstrap.contains(QStringLiteral("&miacode::v2::ChartWorkspace::changed"))
                       && documentFileFlow.contains(QStringLiteral("void miacode::runtime::DocumentSessionHost::syncRuntimeFromWorkspace()"))
                       && documentFileFlow.contains(QStringLiteral("appliedQmlWorkspaceRevision_ = snapshot.revision")),
                   QStringLiteral("QML does not echo commits into the window; the window follows ChartWorkspace::changed"), err)
        && require(documentModel.contains(
                       QStringLiteral("publishWorkspaceCommit(WorkspaceCommitKind::Incremental);"))
                       && timelineFlow.contains(
                           QStringLiteral("editorSyncController_->requestNavigation("))
                       && timelineFlow.contains(QStringLiteral("appliedQmlWorkspaceRevision_"))
                       && followSync.contains(
                           QStringLiteral("follow.revision = session_.appliedQmlWorkspaceRevision_"))
                       && !followSync.contains(QStringLiteral("documentValidationSnapshot()")),
                   QStringLiteral("initial, navigation, and follow identities stay on the committed workspace revision"), err)
        && require(timelineFlow.contains(
                       QStringLiteral("updateDocumentField(miacode::v2::ChartWorkspaceDocumentField::First"))
                       && timelineFlow.contains(QStringLiteral("upsertExtraField(QStringLiteral(\"wholebpm\")"))
                       && timelineFlow.contains(QStringLiteral("upsertExtraField(QStringLiteral(\"clock_count\")")),
                   QStringLiteral("latency BPM, offset, and clock_count write the workspace"), err)
        && require(fileFlow.contains(QStringLiteral("session_.qmlDocumentSaveHandler_(path)")),
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
