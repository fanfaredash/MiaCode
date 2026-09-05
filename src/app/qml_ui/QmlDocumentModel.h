#pragma once

#include <QObject>

#include <functional>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "QmlDocumentProjection.h"
#include "app/v2/AnalysisService.h"
#include "app/v2/ChartMediaService.h"
#include "app/v2/ChartWorkspace.h"
#include "app/v2/ChartWorkspaceFileService.h"
#include "app/v2/DocumentBridge.h"
#include "app/v2/PreviewSurface.h"
#include "app/v2/UiRequestService.h"

#include "app/v2/ShellNotifications.h"


// QML-facing owner for the active MiaCode chart document. This class is the
// single boundary between the visual UI and the existing chart model;
// QML never reaches into widgets or MainWindow internals.
class QmlDocumentModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString chartText READ chartText WRITE setChartText NOTIFY chartTextChanged)
    Q_PROPERTY(QString metadataTitle READ metadataTitle WRITE setMetadataTitle NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataArtist READ metadataArtist WRITE setMetadataArtist NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataFirst READ metadataFirst WRITE setMetadataFirst NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataDesigner READ metadataDesigner WRITE setMetadataDesigner NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataVideoPath READ metadataVideoPath WRITE setMetadataVideoPath NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataClockCount READ metadataClockCount WRITE setMetadataClockCount NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataExtraText READ metadataExtraText WRITE setMetadataExtraText NOTIFY metadataChanged)
    Q_PROPERTY(QVariantList metadataExtraIssues READ metadataExtraIssues NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataExtraError READ metadataExtraError NOTIFY metadataChanged)
    Q_PROPERTY(bool metadataNeedsAttention READ metadataNeedsAttention NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataAttentionText READ metadataAttentionText NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataSourceText READ metadataSourceText WRITE setMetadataSourceText NOTIFY metadataSourceChanged)
    Q_PROPERTY(QString metadataSourceError READ metadataSourceError NOTIFY metadataSourceChanged)
    Q_PROPERTY(QVariantList metadataSourceIssues READ metadataSourceIssues NOTIFY metadataSourceChanged)
    Q_PROPERTY(bool metadataSourceValid READ metadataSourceValid NOTIFY metadataSourceChanged)
    Q_PROPERTY(bool unifiedDesignerEnabled READ unifiedDesignerEnabled NOTIFY unifiedDesignerEnabledChanged)
    // Every &des_N slot the designer-management dialog edits, charted or not:
    // { id, name, designer, hasChart }.
    Q_PROPERTY(QVariantList designerSlots READ designerSlots NOTIFY metadataChanged)
    Q_PROPERTY(QString documentTitle READ documentTitle NOTIFY documentTitleChanged)
    Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY currentFilePathChanged)
    Q_PROPERTY(QString currentFileName READ currentFileName NOTIFY currentFilePathChanged)
    Q_PROPERTY(QString currentDifficultyName READ currentDifficultyName NOTIFY currentDifficultyChanged)
    Q_PROPERTY(QString currentDifficultyLabel READ currentDifficultyLabel NOTIFY currentDifficultyChanged)
    Q_PROPERTY(int currentDifficultyId READ currentDifficultyId NOTIFY currentDifficultyChanged)
    Q_PROPERTY(QVariantList difficulties READ difficulties NOTIFY difficultiesChanged)
    Q_PROPERTY(QVariantList availableDifficulties READ availableDifficulties NOTIFY difficultiesChanged)
    Q_PROPERTY(QString currentDifficultyLevel READ currentDifficultyLevel WRITE setCurrentDifficultyLevel NOTIFY currentDifficultyFieldsChanged)
    Q_PROPERTY(QString currentDifficultyDesigner READ currentDifficultyDesigner WRITE setCurrentDifficultyDesigner NOTIFY currentDifficultyFieldsChanged)
    Q_PROPERTY(bool currentDifficultyLevelMissing READ currentDifficultyLevelMissing NOTIFY currentDifficultyFieldsChanged)
    Q_PROPERTY(QVariantList syntaxIssues READ syntaxIssues NOTIFY syntaxIssuesChanged)
    Q_PROPERTY(int syntaxIssueCount READ syntaxIssueCount NOTIFY syntaxIssuesChanged)
    Q_PROPERTY(int syntaxErrorCount READ syntaxErrorCount NOTIFY syntaxIssuesChanged)
    Q_PROPERTY(int syntaxWarningCount READ syntaxWarningCount NOTIFY syntaxIssuesChanged)
    Q_PROPERTY(int parsedNoteCount READ parsedNoteCount NOTIFY syntaxIssuesChanged)
    Q_PROPERTY(qulonglong documentRevision READ documentRevision NOTIFY documentStateChanged)
    Q_PROPERTY(qulonglong validationRevision READ validationRevision NOTIFY documentStateChanged)
    Q_PROPERTY(bool validationPending READ validationPending NOTIFY documentStateChanged)
    Q_PROPERTY(bool validationAvailable READ validationAvailable NOTIFY documentStateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    // Which section a save writes. A tab is what a person works in, so 保存
    // means "save what I am doing" — the active difficulty, leaving the other
    // difficulties on disk as they are. The whole-source view's section is the
    // whole file, which is why the shell has to say when that view is in front.
    Q_PROPERTY(bool wholeSourceEditorActive READ wholeSourceEditorActive
                   WRITE setWholeSourceEditorActive NOTIFY wholeSourceEditorActiveChanged)
    Q_PROPERTY(QStringList dirtyEditorKeys READ dirtyEditorKeys NOTIFY dirtyEditorKeysChanged)
    Q_PROPERTY(qulonglong bookmarkGeneration READ bookmarkGeneration NOTIFY bookmarksChanged)

public:
    explicit QmlDocumentModel(
        miacode::v2::ShellNotifications& notifications, miacode::v2::ChartWorkspace& workspace,
        miacode::v2::ChartWorkspaceFileService& fileService,
        miacode::v2::AnalysisService& analysisService,
        miacode::v2::UiRequestService& uiRequests,
        miacode::v2::DocumentBridge*& bridgeSlot,
        miacode::v2::PreviewSurface*& previewSlot,
        QObject* parent = nullptr);
    ~QmlDocumentModel() override;

    QString chartText() const;
    void setChartText(const QString& value);
    QString metadataTitle() const;
    QString metadataArtist() const;
    QString metadataFirst() const;
    QString metadataDesigner() const;
    QString metadataVideoPath() const;
    QString metadataClockCount() const;
    QString metadataExtraText() const;
    QVariantList metadataExtraIssues() const;
    QString metadataExtraError() const;
    bool metadataNeedsAttention() const;
    QString metadataAttentionText() const;
    QString wholeBpm() const;
    QString metadataSourceText() const;
    QString metadataSourceError() const;
    QVariantList metadataSourceIssues() const;
    bool metadataSourceValid() const;
    bool unifiedDesignerEnabled() const;
    QVariantList designerSlots() const;
    void setMetadataTitle(const QString& value);
    void setMetadataArtist(const QString& value);
    void setMetadataFirst(const QString& value);
    void setMetadataDesigner(const QString& value);
    void setMetadataVideoPath(const QString& value);
    void setMetadataClockCount(const QString& value);
    void setMetadataExtraText(const QString& value);
    Q_INVOKABLE QVariantMap applyMetadataExtraText(const QString& value);
    Q_INVOKABLE void importChartBackgroundImage();
    Q_INVOKABLE void importChartBackgroundVideo();
    Q_INVOKABLE void removeChartPv();
    void setMetadataSourceText(const QString& value);

    QString documentTitle() const;
    QString currentFilePath() const;
    QString currentFileName() const;
    QString currentDifficultyName() const;
    QString currentDifficultyLabel() const;
    int currentDifficultyId() const;
    QVariantList difficulties() const;
    QVariantList availableDifficulties() const;
    QString currentDifficultyLevel() const;
    QString currentDifficultyDesigner() const;
    bool currentDifficultyLevelMissing() const;
    void setCurrentDifficultyLevel(const QString& value);
    void setCurrentDifficultyDesigner(const QString& value);
    QVariantList syntaxIssues() const;
    int syntaxIssueCount() const;
    int syntaxErrorCount() const;
    int syntaxWarningCount() const;
    int parsedNoteCount() const;
    qulonglong documentRevision() const;
    qulonglong documentGeneration() const { return documentGeneration_; }
    qulonglong validationRevision() const;
    bool validationPending() const;
    bool validationAvailable() const;
    bool dirty() const;
    // Put one difficulty's chart back to the last save point, leaving the rest
    // of the document alone. This is what "放弃" means when the thing being
    // closed is one tab rather than the file.
    Q_INVOKABLE bool revertDifficultyChart(int difficultyId);
    // Write one difficulty to the file, leaving the others at their on-disk
    // text. Used where the thing being saved is named rather than implied.
    Q_INVOKABLE bool saveDifficultySection(int difficultyId);
    // The same save, allowed to ask for a path. The answer arrives on
    // sectionSaveFinished because a file pick cannot be waited for.
    Q_INVOKABLE void requestSaveDifficultySection(int difficultyId);
    // The unsaved-changes flow, asked one section at a time.
    //
    // 保存 writes one difficulty, so a single question about "the document"
    // could only ever save one of them and would drop the rest on the way out.
    // Each changed difficulty is therefore asked about in turn, with the editor
    // switched to it first so the question is about something visible; whatever
    // is left over (metadata, added or removed difficulties) is asked about
    // last, as the file.
    //
    // onDecided(false) means the user cancelled and nothing should continue.
    void requestLeaveDocument(std::function<void(bool)> onDecided);
    // The page router uses the same QML-owned choice flow, but only for the
    // section currently in front. A page switch must never block on a native
    // dialog or save a different dirty difficulty by accident.
    void requestLeaveCurrentField(std::function<void(bool)> onDecided);

    bool wholeSourceEditorActive() const;
    void setWholeSourceEditorActive(bool active);
    int saveSectionDifficultyId() const;
    QStringList dirtyEditorKeys() const;
    qulonglong bookmarkGeneration() const;

    Q_INVOKABLE bool openFile(const QUrl& fileUrl);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& fileUrl);
    Q_INVOKABLE bool discardChanges();
    // Leaves the shell with no document: no difficulties, so no tabs and no
    // source. The guard that asks about unsaved work lives at the command
    // boundary, not here.
    Q_INVOKABLE void closeDocument();
    // 打开最近 / 恢复备份, newest first, each entry { path, label }. Reads of
    // document history, which is why they sit here; acting on one is a command
    // and sits behind the unsaved-changes guard.
    Q_INVOKABLE QVariantList recentDocuments();
    Q_INVOKABLE QVariantList backupDocuments();
    // Restore an autosave snapshot. Confirms first, through the shell.
    Q_INVOKABLE void restoreBackup(const QString& path);
    // 新建: pick an audio file; the chart is created beside it, in that same
    // folder rather than in a new one. A chart's track is found by name — only
    // track.mp3/.wav/.flac/.ogg count — so the picked file is copied to that
    // name unless it already has it.
    // The unsaved-changes guard belongs to the caller in QmlCommandService.
    Q_INVOKABLE void createDocumentFromPickedAudio();
    Q_INVOKABLE void selectDifficulty(int id);
    Q_INVOKABLE bool addDifficulty(int id);
    Q_INVOKABLE bool removeDifficulty(int id);
    Q_INVOKABLE void validateChart();
    Q_INVOKABLE int chartPosition(int line, int column) const;
    // Paired with the editor/document_replaced projection line: what the
    // visible editor actually ended up showing. The two together identify
    // whether a stale editor is a projection problem or a QML one.
    Q_INVOKABLE void logEditorDocumentState(const QString& reason, int difficultyId,
                                            qulonglong revision, int shownChars,
                                            bool metadataMode);
    Q_INVOKABLE QVariantList bookmarksForDifficulty(int difficultyId) const;
    Q_INVOKABLE void navigateToBookmark(int difficultyId, int line);
    // The designer-management dialog's whole result, applied as one
    // transaction: `slotValues` is a list of { id, designer } for every row the
    // dialog showed, `unified` is the "all difficulties share one name"
    // checkbox, and `canonicalName` the name it settled on (empty clears).
    Q_INVOKABLE bool applyDesignerSlots(const QVariantList& slotValues, bool unified,
                                        const QString& canonicalName);

    // Normalizes the selected range (or the whole text when nothing is
    // selected) and returns the result as a value: { ok, changed, text,
    // selectionStart, selectionEnd, error }. It does NOT commit — the editor
    // applies it as one of its own transactions so undo covers it.
    Q_INVOKABLE QVariantMap normalizeChartSelection(
        const QString& text, int anchor, int position, const QVariantMap& options) const;

    // Applies one of the 谱面变换 commands to the selected range, returned as an
    // editor transaction the same way normalizeChartSelection is. `opId` is the
    // ShortcutRegistry id ("transform.mirror_lr", …), so the shortcut table,
    // the menu and this dispatch all name the operation identically.
    //
    // These used to be MainWindow methods that read the hidden Widgets editor's
    // cursor. Nothing ever carried the QML selection into that widget, so in v2
    // every one of them found an empty selection and did nothing.
    Q_INVOKABLE QVariantMap transformChartSelection(
        const QString& text, int anchor, int position, const QString& opId) const;
    Q_INVOKABLE QVariantMap selectionBeatSummary(
        const QString& text, int anchor, int position) const;
    Q_INVOKABLE QStringList chartTransformIds() const;
    // The same table as rows a menu can render: { id, label, section }. Both
    // the menubar's 调整 menu and the editor's context menu build from this, so
    // neither carries its own copy of the operation list or its labels.
    Q_INVOKABLE QVariantList chartTransformMenu() const;
    Q_INVOKABLE QString chartTransformMoreLabel() const;

    // Stored normalize options, as the same map normalizeChartSelection takes.
    Q_INVOKABLE QVariantMap normalizeOptions() const;
    Q_INVOKABLE void setNormalizeOptions(const QVariantMap& options);
    // Sectioning is one choice, not a toggle plus a count: 4 / 2 / 0 measures,
    // where 0 means no sectioning. splitEveryFourMeasures is derived from it.
    Q_INVOKABLE QVariantList normalizeGridOptions() const;
    Q_INVOKABLE QVariantList normalizeSectionOptions() const;
    Q_INVOKABLE QVariantList normalizeSyntaxOptions() const;

signals:
    void chartTextChanged();
    void metadataChanged();
    void metadataSourceChanged();
    void unifiedDesignerEnabledChanged();
    void documentTitleChanged();
    void currentFilePathChanged();
    void currentDifficultyChanged();
    void difficultiesChanged();
    void currentDifficultyFieldsChanged();
    void syntaxIssuesChanged();
    void dirtyChanged();
    void sectionSaveFinished(int difficultyId, bool saved);
    void wholeSourceEditorActiveChanged();
    void dirtyEditorKeysChanged();
    void documentStateChanged();
    void documentReplaced();
    void bookmarksChanged();
    void operationFailed(const QString& title, const QString& message);

private:
    enum class WorkspaceCommitKind {
        Incremental,
        DifficultySelection,
        Structure,
        SourceReplacement,
        Open,
        SavePoint,
    };

    void publishWorkspaceCommit(
        WorkspaceCommitKind kind, bool documentReplaced = false,
        bool usedSystemEncoding = false);
    bool saveToPath(const QString& path);
    void adoptBackendDocumentReplacement();
    QString documentField(miacode::v2::ChartWorkspaceDocumentField field) const;
    QString difficultyField(
        int difficultyId, miacode::v2::ChartWorkspaceDifficultyField field) const;
    void emitDocumentStateChanged();
    void refreshDocumentState();
    void clearMetadataSourceRejection();
    void clearMetadataExtraRejection();
    bool runWorkspaceMutation(const std::function<bool()>& mutate);
    // The workspace half of applyDesignerSlots() for assemblies with no shell
    // bridge (specs, headless hosts): no project preference is written there.
    bool applyDesignerSlotsWithoutBridge(const QVector<QPair<int, QString>>& slotValues,
                                         bool unified, const QString& canonicalName);
    // Re-checks the shared-designer mode against the document after a whole
    // -source replacement (source editing, discard, backup restore).
    void reconcileUnifiedDesignerAfterSourceReplacement();
    QVariantList sourceIssuesToVariantList() const;
    QVariantList extraIssuesToVariantList() const;
    QStringList metadataAttentionItems() const;
    void requestChartMediaImport(miacode::v2::ChartMediaService::Kind kind);
    void applyChartMediaImport(const QString& sourcePath,
                               miacode::v2::ChartMediaService::Kind kind);
    miacode::v2::ShellNotifications* notifications_ = nullptr;
    // Bound to the assembly's slot, not a snapshot.
    miacode::v2::DocumentBridge** bridgeSlot_ = nullptr;
    miacode::v2::DocumentBridge* bridge() const
    {
        return bridgeSlot_ != nullptr ? *bridgeSlot_ : nullptr;
    }
    miacode::v2::ChartWorkspace* workspace_ = nullptr;
    miacode::v2::ChartWorkspaceFileService* fileService_ = nullptr;
    miacode::v2::AnalysisService* analysisService_ = nullptr;
    // From the application assembly, not from the hidden window: the window is
    // not the owner of this boundary and must not be asked for it.
    miacode::v2::UiRequestService* uiRequests_ = nullptr;
    miacode::v2::ChartMediaService mediaService_;
    miacode::v2::PreviewSurface** previewSlot_ = nullptr;
    miacode::v2::PreviewSurface* preview() const
    {
        return previewSlot_ != nullptr ? *previewSlot_ : nullptr;
    }
    QString metadataSourceError_;
    QString metadataSourceAttemptText_;
    QVector<miacode::qml_ui::DocumentValidationProjectionIssue> metadataSourceIssues_;
    QString metadataExtraError_;
    QString metadataExtraAttemptText_;
    QVector<miacode::qml_ui::DocumentValidationProjectionIssue> metadataExtraIssues_;
    bool metadataExtraAttemptActive_ = false;
    miacode::qml_ui::DocumentValidationProjection validationSnapshot_;
    miacode::qml_ui::DocumentPresentationState presentationState_;
    quint64 documentRevision_ = 0;
    qulonglong documentGeneration_ = 0;
    qulonglong bookmarkGeneration_ = 0;
    // Mirror of ChartWorkspace's session mode, refreshed in
    // refreshDocumentState() purely so the property can report a change.
    bool unifiedDesignerEnabled_ = false;
    bool wholeSourceEditorActive_ = false;
    bool suppressWorkspaceChanged_ = false;
    // Saving needs a path. A document that has never been written has none, so
    // the save asks for one first — through the shell's file request, which
    // makes it a continuation like the rest of this flow. Without this, 保存 on
    // a never-saved chart wrote nothing and said nothing: the file service
    // refused an empty path and the prompt just went away.
    void createChartBesideAudio(const QString& audioPath);
    void ensureTrackCopyThenCreate(const QString& audioPath, const QString& targetPath);
    void createEmptyDocumentAt(const QString& targetPath);
    void saveSectionOrAskForPath(int difficultyId, std::function<void(bool)> onSaved);
    void askNextDirtySection(std::function<void(bool)> onDecided);
    void askAboutRemainingDocument(std::function<void(bool)> onDecided);
};
