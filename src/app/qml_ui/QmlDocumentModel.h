#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

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
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QStringList dirtyEditorKeys READ dirtyEditorKeys NOTIFY dirtyEditorKeysChanged)

public:
    explicit QmlDocumentModel(MainWindow& backend, QObject* parent = nullptr);

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
    bool dirty() const;
    QStringList dirtyEditorKeys() const;

    Q_INVOKABLE bool openFile(const QUrl& fileUrl);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& fileUrl);
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE void selectDifficulty(int id);
    Q_INVOKABLE bool addDifficulty(int id);
    Q_INVOKABLE bool removeDifficulty(int id);
    Q_INVOKABLE void validateChart();
    Q_INVOKABLE int chartPosition(int line, int column) const;
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
    void documentReplaced();
    void operationFailed(const QString& title, const QString& message);

private:
    void markDocumentChanged();
    void emitDocumentStateChanged();
    MainWindow* backend_ = nullptr;
    QString metadataSourceError_;
};
