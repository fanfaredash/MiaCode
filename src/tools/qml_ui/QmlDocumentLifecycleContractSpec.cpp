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
    const QString documentFileFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFileFlow.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString mainView = sourceFile(QStringLiteral("src/app/qml_ui/layout/MainView.qml"));
    const QString mainSplitView = sourceFile(
        QStringLiteral("src/app/qml_ui/layout/MainSplitView.qml"));
    const QString editorPane = sourceFile(
        QStringLiteral("src/app/qml_ui/editor/EditorPane.qml"));
    const QString viewState = sourceFile(QStringLiteral("src/app/qml_ui/ViewState.qml"));
    const QString autosaveFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentAutosave.cpp"));
    const QString designerFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentDesignerFlow.cpp"));
    const QString documentBridge = sourceFile(QStringLiteral("src/app/v2/DocumentBridge.h"));

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
                   QStringLiteral("a QML replacement resets active editors so chart text and bookmarks are rederived"), err)
        && require(mainView.contains(QStringLiteral("onActivated: splitView.requestCloseActiveEditor()"))
                       && !mainView.contains(QStringLiteral("onActivated: state.closeActiveEditor()"))
                       && mainSplitView.contains(QStringLiteral("function requestCloseActiveEditor()"))
                       && editorPane.contains(QStringLiteral("tabs.requestCloseTab(viewState.activeEditorKey)"))
                       && !viewState.contains(QStringLiteral("function closeActiveEditor()")),
                   QStringLiteral("keyboard tab-close routes through EditorTabBar's dirty guard instead of closing the ViewState directly"), err)
        && require(!documentUi.contains(QStringLiteral("refreshUnifiedDesignerStateForLoadedDocument"))
                       && !documentModel.contains(QStringLiteral("refreshUnifiedDesignerStateForLoadedDocument"))
                       && !designerFlow.contains(QStringLiteral("unified_designer_enabled"))
                       && !documentBridge.contains(QStringLiteral("refreshUnifiedDesignerStateForLoadedDocument"))
                       && documentFileFlow.contains(QStringLiteral("state_.unifiedDesignerEnabled_ = false")),
                   QStringLiteral("unified-designer preference load wiring stays detached pending v1 review"), err)
        && require(autosaveFlow.contains(QStringLiteral("snapshot.ensureDifficulty(state_.activeDifficultyId_)"))
                       && !autosaveFlow.contains(QStringLiteral("capturedDesignerFromUi"))
                       && !autosaveFlow.contains(QStringLiteral("state_.unifiedDesignerEnabled_ &&")),
                   QStringLiteral("autosave serializes the workspace snapshot without silently re-unifying designers"), err);
}

bool verifyQmlNewDocumentUsesRequestServices(QTextStream& err)
{
    const QString commandService = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlCommandService.cpp"));
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString fileFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFileFlow.cpp"));
    return require(containsAfter(commandService,
                                 QStringLiteral("void QmlCommandService::newDocument()"),
                                 QStringLiteral("document_->createDocumentFromPickedAudio()")),
                   QStringLiteral("the new-document command enters the QML audio-picker flow"), err)
        && require(containsAfter(documentModel,
                                 QStringLiteral("void QmlDocumentModel::createDocumentFromPickedAudio()"),
                                 QStringLiteral("requests->requestFile(request")),
                   QStringLiteral("the QML new-document flow uses UiRequestService for file picking"), err)
        && require(containsAfter(documentModel,
                                 QStringLiteral("void QmlDocumentModel::createEmptyDocumentAt"),
                                 QStringLiteral("fileService_->createEmptyDocument(targetPath)"))
                       && containsAfter(documentModel,
                                        QStringLiteral("fileService_->createEmptyDocument(targetPath)"),
                                        QStringLiteral("openFile(QUrl::fromLocalFile(targetPath))")),
                   QStringLiteral("the QML new-document flow writes through the file service then reopens the fresh file"), err)
        && require(!fileFlow.contains(QStringLiteral("DocumentSessionHost::onNewFile"))
                       && !fileFlow.contains(QStringLiteral("DocumentSessionHost::onOpenFile"))
                       && !fileFlow.contains(QStringLiteral("<QFileDialog>")),
                   QStringLiteral("the retired native new/open file dialog path is absent from DocumentFileFlow"), err);
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
    const QString documentEditorState = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentEditorState.cpp"));
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
        && require(documentBridge.contains(QStringLiteral("markCurrentFieldDirty();"))
                       && documentEditorState.contains(
                           QStringLiteral("void miacode::runtime::DocumentSessionHost::noteDocumentEditedForAutosave()"))
                       && documentEditorState.contains(QStringLiteral("autosaveIdleTimer_->start()"))
                       && documentEditorState.contains(
                           QStringLiteral("miacode::crash_recovery::updateSnapshot"))
                       && documentFileFlow.contains(QStringLiteral("if (snapshot.dirty)"))
                       && documentFileFlow.contains(QStringLiteral("noteDocumentEditedForAutosave();"))
                       && documentFileFlow.contains(QStringLiteral("resetAutosaveState(snapshot.sourceText)")),
                   QStringLiteral("a v2 edit arms the per-edit autosave and the crash snapshot "
                                  "through the runtime document host"), err)
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
                       && documentBridge.contains(
                           QStringLiteral("editorSyncController_->requestNavigation("))
                       && documentBridge.contains(QStringLiteral("appliedQmlWorkspaceRevision_"))
                       // Stage 4.9d-4b-2c routed the coordinator's read of the
                       // field through the new PlaybackDocumentPort instead of
                       // a direct session_.appliedQmlWorkspaceRevision_ read;
                       // the field itself did not move, only the access path.
                       // The invariant this line pins — follow carries the
                       // committed workspace revision — is unchanged.
                       && followSync.contains(
                           QStringLiteral("follow.revision = documents_.appliedWorkspaceRevision()"))
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

bool verifyPageNavigationUsesTheQmlLeaveGuard(QTextStream& err)
{
    const QString documentModel = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlDocumentModel.cpp"));
    const QString pageHost = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlEditorPageHost.cpp"));
    const QString pageHostHeader = sourceFile(
        QStringLiteral("src/app/qml_ui/QmlEditorPageHost.h"));
    const QString documentPages = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentPages.cpp"));
    const QString documentFileFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFileFlow.cpp"));
    const QString documentFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFlow.cpp"));
    const QString internalFlow = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentFlow.Internal.h"));
    const QString autosave = sourceFile(
        QStringLiteral("src/app/runtime/document/DocumentAutosave.cpp"));
    const QString shared = sourceFile(QStringLiteral("src/app/runtime/Shared.cpp"));
    const QString sharedHeader = sourceFile(QStringLiteral("src/app/runtime/Shared.h"));
    const QString timelineItem = sourceFile(
        QStringLiteral("src/timeline/quick/TimelineQuickItem.cpp"));
    const QString timelineItemHeader = sourceFile(
        QStringLiteral("src/timeline/quick/TimelineQuickItem.h"));
    const QString bottomPanel = sourceFile(
        QStringLiteral("src/app/qml_ui/timeline/BottomPanel.qml"));
    const QString videoExportController = sourceFile(
        QStringLiteral("src/tools/video_export/VideoExportController.cpp"));
    const QString videoExportHeader = sourceFile(
        QStringLiteral("src/tools/video_export/VideoExportController.h"));
    const QString videoExportInternal = sourceFile(
        QStringLiteral("src/tools/video_export/VideoExportControllerInternal.h"));

    bool ok = true;
    ok &= require(documentModel.contains(
                       QStringLiteral("void QmlDocumentModel::requestLeaveCurrentField"))
                       && documentModel.contains(QStringLiteral("uiRequests_->requestChoice"))
                       && documentModel.contains(
                           QStringLiteral("saveSectionOrAskForPath(difficultyId"))
                       && documentModel.contains(
                           QStringLiteral("workspace_->revertDifficultyChart(difficultyId)")),
                   QStringLiteral("page navigation uses the QML choice/save/discard flow for the current field"), err);
    ok &= require(pageHost.contains(QStringLiteral("requestPageSwitch"))
                       && pageHost.contains(
                           QStringLiteral("document_->requestLeaveCurrentField"))
                       && pageHost.contains(QStringLiteral("documentGeneration"))
                       && pageHost.contains(QStringLiteral("navigationPending_"))
                       && pageHostHeader.contains(QStringLiteral("overlayPageLeft()")),
                   QStringLiteral("the page host waits for one guarded continuation and rejects stale or repeated navigation"), err);
    ok &= require(!documentPages.contains(QStringLiteral("maybeSaveCurrentFieldChanges"))
                       && !documentFileFlow.contains(QStringLiteral("showUnsavedChangesDialog"))
                       && !documentFileFlow.contains(QStringLiteral("applyUnsavedChangesChoice"))
                       && !documentFlow.contains(QStringLiteral("showUnsavedChangesDialog"))
                       && !internalFlow.contains(QStringLiteral("QMessageBox"))
                       && !internalFlow.contains(QStringLiteral("QWidget")),
                   QStringLiteral("runtime page and document flows contain no synchronous native unsaved dialog"), err);
    ok &= require(!autosave.contains(QStringLiteral("QFileDialog"))
                       && !autosave.contains(QStringLiteral("onSaveFileAs"))
                       && !shared.contains(QStringLiteral("centerDialogOnAnchor"))
                       && !sharedHeader.contains(QStringLiteral("centerDialogOnAnchor")),
                   QStringLiteral("the document autosave and shared runtime layers contain no native dialog fallback"), err);
    ok &= require(!timelineItem.contains(QStringLiteral("QToolTip"))
                       && timelineItemHeader.contains(QStringLiteral("hoverTooltipText"))
                       && timelineItemHeader.contains(QStringLiteral("hoverTooltipPosition"))
                       && bottomPanel.contains(QStringLiteral("timelineMarkerTooltip"))
                       && bottomPanel.contains(QStringLiteral("Overlay.overlay")),
                   QStringLiteral("timeline marker tooltips use the QML Tooltip surface instead of QToolTip"), err);
    ok &= require(!videoExportController.contains(QStringLiteral("QProgressDialog"))
                       && !videoExportHeader.contains(QStringLiteral("QProgressDialog"))
                       && !videoExportInternal.contains(QStringLiteral("QProgressDialog")),
                   QStringLiteral("video export progress is callback/QML based and has no QProgressDialog boundary"), err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = verifyBackendReplacementPublishesOneQmlRefresh(err)
        && verifyQmlNewDocumentUsesRequestServices(err)
        && verifyV2AnalysisUsesWorkspaceSnapshot(err)
        && verifyWorkspaceOwnsProductionDocumentAndDirty(err)
        && verifyPageNavigationUsesTheQmlLeaveGuard(err);
    if (ok) {
        out << "qml_document_lifecycle_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
