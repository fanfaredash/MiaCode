#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QListWidgetItem;
class QTimer;
class QWindow;

class MainWindow;
class OutlineListModel;
class IssueListModel;
class LegacyChartEditorSurface;
class LegacyTimelineSurface;

class QuickShellController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY shellStateChanged)
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY shellStateChanged)
    Q_PROPERTY(bool hasActiveDifficulty READ hasActiveDifficulty NOTIFY shellStateChanged)
    Q_PROPERTY(QString activeDifficultyName READ activeDifficultyName NOTIFY shellStateChanged)
    Q_PROPERTY(QString currentFileLabel READ currentFileLabel NOTIFY shellStateChanged)
    Q_PROPERTY(QString cursorLabel READ cursorLabel NOTIFY shellStateChanged)
    Q_PROPERTY(QString titleDraft READ titleDraft WRITE setTitleDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString artistDraft READ artistDraft WRITE setArtistDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString firstDraft READ firstDraft WRITE setFirstDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString designerDraft READ designerDraft WRITE setDesignerDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString metadataExtraDraft READ metadataExtraDraft WRITE setMetadataExtraDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString difficultyLevelDraft READ difficultyLevelDraft WRITE setDifficultyLevelDraft NOTIFY shellStateChanged)
    Q_PROPERTY(QString difficultyDesignerDraft READ difficultyDesignerDraft WRITE setDifficultyDesignerDraft NOTIFY shellStateChanged)
    Q_PROPERTY(int validationErrorCount READ validationErrorCount NOTIFY shellStateChanged)
    Q_PROPERTY(int validationWarningCount READ validationWarningCount NOTIFY shellStateChanged)
    Q_PROPERTY(int muriIssueCount READ muriIssueCount NOTIFY shellStateChanged)
    Q_PROPERTY(QString previewSpeedLabel READ previewSpeedLabel NOTIFY shellStateChanged)
    Q_PROPERTY(bool previewPlaying READ previewPlaying NOTIFY shellStateChanged)
    Q_PROPERTY(double previewPositionSeconds READ previewPositionSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(double previewDurationSeconds READ previewDurationSeconds NOTIFY shellStateChanged)
    Q_PROPERTY(bool workspacePanelsSwapped READ workspacePanelsSwapped NOTIFY shellStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QVariantList previewStats READ previewStats NOTIFY shellStateChanged)
    Q_PROPERTY(QVariantList availableDifficultyOptions READ availableDifficultyOptions NOTIFY shellStateChanged)
    Q_PROPERTY(bool showBottomTabs READ showBottomTabs NOTIFY shellStateChanged)
    Q_PROPERTY(int bottomTabIndex READ bottomTabIndex WRITE setBottomTabIndex NOTIFY shellStateChanged)
    Q_PROPERTY(bool previewFullscreen READ previewFullscreen WRITE setPreviewFullscreen NOTIFY previewFullscreenChanged)
    Q_PROPERTY(OutlineListModel* outlineModel READ outlineModel CONSTANT)
    Q_PROPERTY(IssueListModel* validationModel READ validationModel CONSTANT)
    Q_PROPERTY(IssueListModel* muriModel READ muriModel CONSTANT)
    Q_PROPERTY(LegacyChartEditorSurface* chartEditorSurface READ chartEditorSurface CONSTANT)
    Q_PROPERTY(LegacyTimelineSurface* timelineSurface READ timelineSurface CONSTANT)
    Q_PROPERTY(QWindow* previewWindow READ previewWindow CONSTANT)

public:
    explicit QuickShellController(MainWindow* backend, QObject* parent = nullptr);

    QString windowTitle() const;
    QString currentPage() const;
    bool hasActiveDifficulty() const;
    QString activeDifficultyName() const;
    QString currentFileLabel() const;
    QString cursorLabel() const;
    QString titleDraft() const;
    QString artistDraft() const;
    QString firstDraft() const;
    QString designerDraft() const;
    QString metadataExtraDraft() const;
    QString difficultyLevelDraft() const;
    QString difficultyDesignerDraft() const;
    int validationErrorCount() const;
    int validationWarningCount() const;
    int muriIssueCount() const;
    QString previewSpeedLabel() const;
    bool previewPlaying() const;
    double previewPositionSeconds() const;
    double previewDurationSeconds() const;
    bool workspacePanelsSwapped() const;
    QString statusText() const;
    QVariantList previewStats() const;
    QVariantList availableDifficultyOptions() const;
    bool showBottomTabs() const;
    int bottomTabIndex() const;
    bool previewFullscreen() const;
    OutlineListModel* outlineModel() const;
    IssueListModel* validationModel() const;
    IssueListModel* muriModel() const;
    LegacyChartEditorSurface* chartEditorSurface() const;
    LegacyTimelineSurface* timelineSurface() const;
    QWindow* previewWindow() const;

    void setTitleDraft(const QString& text);
    void setArtistDraft(const QString& text);
    void setFirstDraft(const QString& text);
    void setDesignerDraft(const QString& text);
    void setMetadataExtraDraft(const QString& text);
    void setDifficultyLevelDraft(const QString& text);
    void setDifficultyDesignerDraft(const QString& text);
    void setBottomTabIndex(int index);
    void setPreviewFullscreen(bool fullscreen);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool confirmClose();
    Q_INVOKABLE void newFile();
    Q_INVOKABLE void openFile();
    Q_INVOKABLE void saveFile();
    Q_INVOKABLE void saveFileAs();
    Q_INVOKABLE void openPreferences();
    Q_INVOKABLE void openAbout();
    Q_INVOKABLE void openPreviewAudioSettings();
    Q_INVOKABLE void openPreviewVideoSettings();
    Q_INVOKABLE void runSyntaxCheck();
    Q_INVOKABLE void mirrorLeftRight();
    Q_INVOKABLE void mirrorUpDown();
    Q_INVOKABLE void rotate180();
    Q_INVOKABLE void rotate45CounterClockwise();
    Q_INVOKABLE void rotate45Clockwise();
    Q_INVOKABLE void normalizeWholeChart();
    Q_INVOKABLE void toggleBreakSelection();
    Q_INVOKABLE void toggleExSelection();
    Q_INVOKABLE void toggleFireworkSelection();
    Q_INVOKABLE void randomRotateSelection();
    Q_INVOKABLE void exportPreviewVideo();
    Q_INVOKABLE void batchExportPreviewVideo();
    Q_INVOKABLE void openLatencyDetector();
    Q_INVOKABLE void activateOutlineRow(int row);
    Q_INVOKABLE void addDifficulty(int difficultyId);
    Q_INVOKABLE void deleteDifficulty(int difficultyId);
    Q_INVOKABLE void activateIssue(bool muriList, int row);
    Q_INVOKABLE void copyIssue(bool muriList, int row);
    Q_INVOKABLE void toggleIssueIgnored(bool muriList, int row);
    Q_INVOKABLE void togglePreviewPlayback();
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void seekPreview(double second);
    Q_INVOKABLE void stepPreviewBy(double deltaSeconds);
    Q_INVOKABLE void setPreviewRate(double rate);
    Q_INVOKABLE void stepPreviewRate(int direction);
    Q_INVOKABLE void toggleWorkspacePanelsSwapped();
    Q_INVOKABLE void openOfficialChartMirror();
    Q_INVOKABLE void openSimaiWiki();

signals:
    void shellStateChanged();
    void statusTextChanged();
    void previewFullscreenChanged();

private:
    QListWidgetItem* issueItem(bool muriList, int row) const;
    void refreshFromBackend();
    void showCopiedStatusMessage();

    QPointer<MainWindow> backend_;
    QTimer* refreshTimer_ = nullptr;
    OutlineListModel* outlineModel_ = nullptr;
    IssueListModel* validationModel_ = nullptr;
    IssueListModel* muriModel_ = nullptr;
    LegacyChartEditorSurface* chartEditorSurface_ = nullptr;
    LegacyTimelineSurface* timelineSurface_ = nullptr;
    QString windowTitle_;
    QString currentPage_;
    bool hasActiveDifficulty_ = false;
    QString activeDifficultyName_;
    QString currentFileLabel_;
    QString cursorLabel_;
    QString titleDraft_;
    QString artistDraft_;
    QString firstDraft_;
    QString designerDraft_;
    QString metadataExtraDraft_;
    QString difficultyLevelDraft_;
    QString difficultyDesignerDraft_;
    int validationErrorCount_ = 0;
    int validationWarningCount_ = 0;
    int muriIssueCount_ = 0;
    QString previewSpeedLabel_;
    bool previewPlaying_ = false;
    double previewPositionSeconds_ = 0.0;
    double previewDurationSeconds_ = 0.0;
    bool workspacePanelsSwapped_ = false;
    QString statusText_;
    QVariantList previewStats_;
    QVariantList availableDifficultyOptions_;
    int bottomTabIndex_ = 0;
    bool previewFullscreen_ = false;
};
