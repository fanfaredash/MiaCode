#pragma once

#include "runtime/Session.h"

#include "app/v2/DocumentBridge.h"
#include "app/v2/EditorPageRouter.h"
#include "app/v2/PlaybackDocumentPort.h"

class QTextCursor;

namespace miacode::runtime {

class DocumentSessionHost final : public miacode::v2::DocumentBridge,
                                      public miacode::v2::EditorPageRouter,
                                      public miacode::v2::PlaybackDocumentPort {
public:
    using CommitKind = miacode::v2::DocumentBridge::CommitKind;

    DocumentSessionHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state);

    bool maybeSaveBeforeContinue();
    // The same decision, asked without blocking. The v2 shell has no place to
    // run a nested event loop from — a window's close handler least of all — so
    // the answer arrives as a continuation and the prompt itself is a QML
    // dialog. `onDecided(true)` means "the document may be left behind".
    //
    // A clean document decides immediately, in this call; only a prompt defers.
    void requestLeaveDocument(std::function<void(bool)> onDecided) override;
    // Everything that happens once the choice is in, shared by both forms so
    // they cannot drift.
    bool applyUnsavedChangesChoice(const QString& choiceId, const QString& logContext);
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument() override;
    QString documentField(Session::DocumentField field) const;
    QString difficultyField(int difficultyId, Session::DifficultyField field) const;
    bool updateDocumentField(Session::DocumentField field, const QString& value);
    bool updateDifficultyField(int difficultyId, Session::DifficultyField field, const QString& value);
    bool updateActiveChartText(const QString& value);
    Session::DocumentSourceReplaceResult replaceDocumentSourceText(const QString& value);
    bool addDocumentDifficulty(int difficultyId);
    void enableUnifiedDocumentDesigner(const QString& canonicalName);
    void disableUnifiedDocumentDesigner();
    bool applyCommittedQmlDocument(
        const QString& sourceText, const QString& filePath, int activeDifficultyId,
        bool dirty, quint64 revision, Session::QmlDocumentCommitKind kind,
        bool usedSystemEncoding);
    // Opens the modal "manage per-difficulty designers" dialog: seven rows for
    // &des_1..7 plus the "all difficulties share one designer" toggle. Commits
    // on OK (chart-less names become standalone &des_N — no phantom
    // difficulty). See implementation in DocumentFlow.cpp.
    void openPerDifficultyDesignerDialog();
    // Apply the saved-or-inferred unified-designer preference to the
    // runtime flag when a chart is opened. Reads
    // <chartDir>/.miacode/preferences.json; falls back to
    // SimaiDocument::inferUnifiedDesignerDefault() when the key is absent.
    void refreshUnifiedDesignerStateForLoadedDocument();
    void onNewFile();
    void onOpenFile();
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
    void refreshRestoreBackupMenu(QMenu* restoreBackupMenu);
    void restoreBackupFilePath(const QString& path, bool mentionAbnormalExit = false);
    // Continuation of restoreBackupFilePath once the confirm is answered.
    void applyBackupFile(const QString& normalizedPath, const QString& title);
    bool restoreLastSessionFile();
    void scheduleStartupRestoreLastSessionFile();
    void cancelPendingStartupRestore();
    void applyPreparedStartupRestoreDocument(const Session::PreparedStartupRestoreDocument& prepared);
    void applyOpenedDocumentState(
        const QString& normalizedPath,
        Session::TextEncoding encodingUsed,
        const SimaiDocument& document,
        bool showStatusMessage,
        double knownTrackDurationSeconds = -1.0
    );
    void resetAutosaveState(const QString& referenceText);
    // Drop the in-memory crash-recovery snapshot AND delete the
    // on-disk recovery file for the current chart. Called from the
    // close-event path so a clean exit doesn't leave a stale recovery
    // file that would prompt "recover unsaved changes?" on next open.
    void cleanupCrashRecoveryForCleanExit();
    QString resolveAutosaveDirectoryPath() const;
    // The 恢复备份 list as values, so a QML menu can show it. Same entries and
    // same labels as the Widgets menu built, including its collision rule.
    QVariantList backupDocumentEntries() override;
    QString currentDocumentTextForAutosave() const;
    void pruneAutosaveFiles(const QString& autosaveDirectoryPath) const;
    void runAutosaveCheck(bool allowHistory = true);
    bool onSaveFile();
    bool onSaveFileAs();
    bool saveToPath(const QString& path);
    void updatePauseButtonAppearance();
    void updateDirtyState() override;
    // The workspace revision QML last committed — see PlaybackDocumentPort.h
    // for why this is a query rather than a relocation of the field.
    quint64 appliedWorkspaceRevision() const override;
    bool currentFieldHasUndoChanges() const;
    void anchorCurrentFieldCleanState();
    void refreshCurrentFieldDirtyState();
    void markCurrentFieldDirty();
    // The per-edit autosave safety net: restart the 2s debounce that writes
    // latest.bak, and push the document into the crash handler's snapshot
    // mailbox so an abnormal exit in the next moments still leaves something
    // recoverable.
    //
    // Its driver used to be the hidden chart editor's textChanged, and it went
    // out with that editor — leaving v2 with only the 2-minute routine snapshot
    // and, worse, with nothing at all in the crash mailbox. The v2 commit path
    // calls it now, which is where an edit actually lands.
    void noteDocumentEditedForAutosave();
    void clearDeletedDifficultyUndoState();
    bool undoDeletedDifficultyField();
    void clearChartSelectionTransformUndoEntries();
    void syncChartSelectionTransformUndoState();
    // `originalAnchor`/`originalPosition` are PRE-EDIT offsets and must be read
    // as ints before the document is touched: a live QTextCursor is adjusted by
    // the very edit being recorded, so reading .position() off one afterwards
    // hands undo an offset shifted by the inserted/removed length.
    void recordChartCursorUndoEntry(
        int originalAnchor,
        int originalPosition,
        const QTextCursor& transformedCursor,
        double previewSecond);
    void recordChartSelectionUndoRestoreAfterNextEdit(int originalAnchor, int originalPosition);
    bool undoChartEditorWithSelectionRestore();
    bool redoChartEditorWithSelectionRestore();
    QString resolveInitialOpenDirectory() const;
    void setLastOpenDirectory(const QString& pathOrDir);
    using DroppedChartCandidate = miacode::v2::ChartDropCandidate;
    miacode::v2::DocumentImportAdapter chartDropImportAdapter();
    void finishChartsFromAudioDrop(
        const QList<DroppedChartCandidate>& candidates,
        QElapsedTimer dropTimer,
        std::function<void(const miacode::v2::ChartDropCreateResult&)> onFinished);
    void onNormalizeWholeChart();
    void updateEditorHeader();
    void updateDifficultyScopedActionStates();
    void updateEditorHeaderLayoutMode();
    void syncEditorHeaderMinimumWidth();
    void updateEditorStatus();
    void updateEditorEmptyState();
    void updateMetadataPageMode();
    // `alreadyConfirmed` is what the v2 shell passes: DifficultyList.qml puts
    // the question up itself, and asking again here was a second dialog on top
    // of an answered one.
    bool deleteDifficultyField(int difficultyId, bool alreadyConfirmed = false);
    void rebuildFieldSidebar();
    // Sidebar bookmark-group fold state. Only explicit toggles are recorded;
    // an untouched difficulty defaults to expanded when active, collapsed
    // otherwise (see the bookmark redesign spec).
    bool isBookmarkGroupExpanded(int difficultyId) const;
    void setBookmarkGroupExpanded(int difficultyId, bool expanded);
    // Expands the difficulty's bookmark group, rebuilds the sidebar, selects
    // and centers the bookmark row; beginRename additionally starts the
    // inline name editor. Safe no-op when the bookmark does not exist.
    void revealBookmarkInSidebar(int difficultyId, int line, bool beginRename);
    // The bookmark list item for (difficultyId, line), or nullptr.
    QListWidgetItem* findBookmarkSidebarItem(int difficultyId, int line) const;
    void populateMetadataPage();
    void populateDifficultyPage(int difficultyId);
    bool switchToMetadataField();
    bool switchToWelcomePage();
    bool switchToDifficultyField(int difficultyId);
    bool switchToLatencyField();
    bool switchToExportField();
    // Floats the busy spinner over the "Export" sidebar row / hides it. Shown
    // while the (slow) export-page build runs after switchToExportField().
    // Positioning is separate so sidebar rebuilds can re-anchor an active
    // spinner after rows move.
    void activateInitialField();
    void loadDocument();
    void syncRuntimeFromWorkspace();
    void clearTimelineAndPreview();
    void rebuildAutosaveMetadata(const QString& autosaveDirectoryPath) const;

    QString sourceText() const override;
    QString filePath() const override;
    int activeDifficultyId() const override;
    bool applyCommittedDocument(const QString& sourceText, const QString& filePath,
                                int activeDifficultyId, bool dirty, quint64 revision,
                                CommitKind kind, bool usedSystemEncoding) override;
    QVariantList recentDocumentEntries() override;
    void noteRecentDocument(const QString& path) override;
    void restoreBackupDocument(const QString& path) override;
    miacode::chart_transform::ChartNormalizationOptions normalizationOptions() const override;
    void setNormalizationOptions(
        const miacode::chart_transform::ChartNormalizationOptions& options) override;
    // Overrides both DocumentBridge::requestEditorNavigation and
    // PlaybackDocumentPort::requestEditorNavigation, which share this exact
    // signature.
    bool requestEditorNavigation(int line, int column, int endLine, int endColumn,
                                 bool selectToken, bool focusEditor, bool centerView) override;
    void setDocumentSaveHandler(std::function<bool(const QString&)> handler) override;
    void setChartTextHandler(std::function<bool(const QString&)> handler) override;
    void setLeaveDocumentHandler(
        std::function<void(std::function<void(bool)>)> handler) override;
    void importDroppedAudio(const QStringList& audioPaths, quint64 requestId,
                            quint64 generation,
                            miacode::v2::ChartDropImportService::Completion completion) override;
    void releaseChartDropImport() override;

    bool hasActiveDifficulty() const override;
    bool enterDifficultyPage(int difficultyId) override;
    bool enterMetadataPage() override;
    bool enterLatencyPage() override;
    bool enterExportPage() override;
    void packChartAsZip() override;
    void openPreferences() override;
    void requestShellClose(std::function<void(bool)> onDecided) override;

private:
    void schedulePendingAbnormalExitBackupRestore();
    void runPendingAbnormalExitBackupRestore();
    // Broadcast the chosen designer name into the top &des, every
    // per-difficulty designer (charted and standalone), and the metadata-page
    // designer line edit. Used by the load-time reconcile.
    void applyUnifiedDesignerName(const QString& canonicalName);
    // Modal picker shown when the user enables the option but no single
    // canonical name exists yet. Lists every distinct non-empty designer
    // found in the document plus a "Clear all" option, and writes the
    // user's selection (or an empty string for "Clear all") to *out.
    // Returns false if the user cancels — callers should revert the
    // checkbox to OFF in that case.
    bool promptCanonicalDesignerName(const QStringList& candidates, QString* out);
    // The heavy body of switchToExportField(), run one event-loop tick later so
    // the busy spinner can paint before the build blocks the UI thread.
    void performSwitchToExportField();
    void setChartBottomTabsMode(bool enabled);

    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
};

}  // namespace miacode::runtime
