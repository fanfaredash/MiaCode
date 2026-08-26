#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include "QmlEditorNavigationBridge.h"
#include "QmlDocumentProjection.h"
#include "app/v2/ChartWorkspace.h"
#include "app/v2/ChartWorkspaceFileService.h"

class MainWindow;

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
    Q_PROPERTY(QString metadataExtraText READ metadataExtraText WRITE setMetadataExtraText NOTIFY metadataChanged)
    Q_PROPERTY(QString metadataSourceText READ metadataSourceText WRITE setMetadataSourceText NOTIFY metadataSourceChanged)
    Q_PROPERTY(QString metadataSourceError READ metadataSourceError NOTIFY metadataSourceChanged)
    Q_PROPERTY(QVariantList metadataSourceIssues READ metadataSourceIssues NOTIFY metadataSourceChanged)
    Q_PROPERTY(bool metadataSourceValid READ metadataSourceValid NOTIFY metadataSourceChanged)
    Q_PROPERTY(bool unifiedDesignerEnabled READ unifiedDesignerEnabled NOTIFY unifiedDesignerEnabledChanged)
    Q_PROPERTY(QStringList designerCandidates READ designerCandidates NOTIFY metadataChanged)
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
    Q_PROPERTY(QStringList dirtyEditorKeys READ dirtyEditorKeys NOTIFY dirtyEditorKeysChanged)
    Q_PROPERTY(qulonglong bookmarkGeneration READ bookmarkGeneration NOTIFY bookmarksChanged)

public:
    explicit QmlDocumentModel(
        MainWindow& backend, miacode::v2::ChartWorkspace& workspace,
        miacode::v2::ChartWorkspaceFileService& fileService,
        QObject* parent = nullptr);
    ~QmlDocumentModel() override;

    QString chartText() const;
    void setChartText(const QString& value);
    QString metadataTitle() const;
    QString metadataArtist() const;
    QString metadataFirst() const;
    QString metadataDesigner() const;
    QString metadataVideoPath() const;
    QString metadataExtraText() const;
    QString metadataSourceText() const;
    QString metadataSourceError() const;
    QVariantList metadataSourceIssues() const;
    bool metadataSourceValid() const;
    bool unifiedDesignerEnabled() const;
    QStringList designerCandidates() const;
    void setMetadataTitle(const QString& value);
    void setMetadataArtist(const QString& value);
    void setMetadataFirst(const QString& value);
    void setMetadataDesigner(const QString& value);
    void setMetadataVideoPath(const QString& value);
    void setMetadataExtraText(const QString& value);
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
    void setCurrentDifficultyLevel(const QString& value);
    void setCurrentDifficultyDesigner(const QString& value);
    QVariantList syntaxIssues() const;
    int syntaxIssueCount() const;
    int syntaxErrorCount() const;
    int syntaxWarningCount() const;
    int parsedNoteCount() const;
    qulonglong documentRevision() const;
    qulonglong validationRevision() const;
    bool validationPending() const;
    bool validationAvailable() const;
    bool dirty() const;
    QStringList dirtyEditorKeys() const;
    qulonglong bookmarkGeneration() const;

    Q_INVOKABLE bool openFile(const QUrl& fileUrl);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& fileUrl);
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE void selectDifficulty(int id);
    Q_INVOKABLE bool addDifficulty(int id);
    Q_INVOKABLE bool removeDifficulty(int id);
    Q_INVOKABLE void validateChart();
    Q_INVOKABLE int chartPosition(int line, int column) const;
    Q_INVOKABLE bool publishEditorCaret(int difficultyId, qulonglong revision, int line, int column);
    Q_INVOKABLE void setQmlEditorInteraction(int difficultyId, qulonglong revision, int anchor, int position, bool focused, bool imeComposing);
    Q_INVOKABLE void setQmlEditorNavigationReadiness(int difficultyId, qulonglong revision,
                                                     bool sourceVisible, bool metadataMode);
    Q_INVOKABLE void setQmlTouchPadAuthoringCtrlHold(bool active);
    Q_INVOKABLE bool setTouchPadAuthoringPreviewAnchor(int difficultyId, qulonglong revision,
                                                       const QString& text, int tokenStart);
    // Ctrl/Command click in the visible QML editor: seek the preview to the
    // clicked token instead of only moving the timeline cursor.
    Q_INVOKABLE bool seekPreviewToEditorLocation(int difficultyId, qulonglong revision,
                                                 int line, int column);
    // Paired with the editor/document_replaced projection line: what the
    // visible editor actually ended up showing. The two together identify
    // whether a stale editor is a projection problem or a QML one.
    Q_INVOKABLE void logEditorDocumentState(const QString& reason, int difficultyId,
                                            qulonglong revision, int shownChars,
                                            bool metadataMode);
    Q_INVOKABLE QVariantList bookmarksForDifficulty(int difficultyId) const;
    Q_INVOKABLE void navigateToBookmark(int difficultyId, int line);
    Q_INVOKABLE void enableUnifiedDesigner(const QString& canonicalName);
    Q_INVOKABLE void disableUnifiedDesigner();

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
    void dirtyEditorKeysChanged();
    void documentStateChanged();
    void documentReplaced();
    void qmlEditorNavigationRequested(int difficultyId, qulonglong revision, int line,
                                      int column, int endLine, int endColumn, bool selectToken,
                                      bool focusEditor, bool centerView);
    // Paused / 代码跟随-off preview follow: a read-only decoration, never a
    // caret move. The editor paints the playhead's span and follow caret.
    void qmlEditorFollowDecorationChanged(bool active, int difficultyId, qulonglong revision,
                                          int startLine, int startColumn, int endLine,
                                          int endColumn, int cursorLine, int cursorColumn,
                                          bool ensureVisible);
    void qmlTouchPadAuthoringRequested(const QString& pad, bool useBacktickSeparator,
                                       int difficultyId, qulonglong revision,
                                       int anchor, int position);
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
    QVariantList sourceIssuesToVariantList() const;
    MainWindow* backend_ = nullptr;
    miacode::v2::ChartWorkspace* workspace_ = nullptr;
    miacode::v2::ChartWorkspaceFileService* fileService_ = nullptr;
    QString metadataSourceError_;
    QString metadataSourceAttemptText_;
    QVector<miacode::qml_ui::DocumentValidationProjectionIssue> metadataSourceIssues_;
    miacode::qml_ui::DocumentValidationProjection validationSnapshot_;
    miacode::qml_ui::DocumentPresentationState presentationState_;
    quint64 documentRevision_ = 0;
    int qmlCaretDifficultyId_ = -1;
    quint64 qmlCaretRevision_ = 0;
    int qmlCaretAnchor_ = 0;
    int qmlCaretPosition_ = 0;
    bool qmlEditorFocused_ = false;
    bool qmlImeComposing_ = false;
    miacode::qml_ui::QmlEditorNavigationReadiness qmlEditorNavigationReadiness_;
    qulonglong bookmarkGeneration_ = 0;
    bool unifiedDesignerEnabled_ = false;
};
