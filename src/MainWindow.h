#pragma once

#include <functional>
#include <utility>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QVector>

#include "PreviewAudioSettings.h"

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class PlainCodeEditor;
class PreviewCanvas;
class PreviewMediaController;
class QPlainTextEdit;
class QProcess;
class QTimer;
class QtPreviewSfxRuntime;
class TimelineView;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onValidateSimai();
    void onNewFile();
    void onOpenFile();
    bool onSaveFile();
    bool onSaveFileAs();
    void onRotate45();
    void onPreviewFromStart();
    void onPreviewFromCursor();
    void onTogglePreviewPause();
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onPreviewAudioSettings();
    void onPreviewDisplaySettings();
    void onErrorItemActivated(QListWidgetItem* item);
    void onPreviewProcessFinished(int exitCode);

private:
    using BatchTransform = std::function<QString(const QString&, int*)>;
    enum class TextEncoding {
        Utf8,
        System,
    };

    bool maybeSaveBeforeContinue();
    bool saveToPath(const QString& path);
    bool applyBatchTransform(const QString& opName, const BatchTransform& transform);
    bool applySelectionBatchTransform(const QString& opName, const BatchTransform& transform);
    bool ensurePreviewSessionStarted();
    void stopPreviewSession();
    bool sendPreviewCommand(const QString& mode, int cursorLine, int cursorCol, const QString& trackPath);
    bool sendPreviewPrepareCommand();
    bool sendPreviewConfigCommand(const QString& audition = QString());
    void startPreviewProcess(const QString& mode, int cursorLine, int cursorCol);
    bool handlePreviewSessionLine(const QString& line);
    void bootstrapPreviewWindow();
    void arrangeWithPreviewWindow();
    std::pair<int, int> currentCursorLineCol() const;
    std::pair<int, int> currentSelectionOrCursorLineCol() const;
    bool currentSelectionRange(int* startPos, int* endPos) const;
    void setEditorText(const QString& text);
    void setCurrentFilePath(const QString& path);
    void updateWindowTitle();
    void updateCurrentFileLabel();
    void scheduleTimelineRefresh();
    void refreshTimelineMetadata();
    void seekTimelineToCursor(int line, int col);
    void jumpToNearestTimelineNote(double second, int lane);
    void startQtPreviewPlayback(double second, bool resumeFromPause = false);
    void stopQtPreviewPlayback(bool keepPosition = true);
    void applyQtPreviewPosition(double second, bool centerView);
    void onQtPreviewTick();
    double timelineSecondForCursor(int line, int col) const;
    void jumpToLocation(int line, int col);
    QString transformRotate45(const QString& input, int* changedCount = nullptr) const;
    QString editorText() const;
    QString resolvePreviewSessionScriptPath() const;
    QString resolveDefaultTrackPath() const;
    QString resolvePreviewSkinDir() const;
    QString resolvePortableStateFilePath() const;
    QString resolveInitialOpenDirectory() const;
    void loadPortableState();
    void savePortableState() const;
    void setLastOpenDirectory(const QString& pathOrDir);
    bool runValidateSimai();
    void appendOutput(const QString& title, const QString& payload);
    void clearValidationErrors();
    void clearValidationDecorations();
    void addValidationError(int line, int col, const QString& message);
    void addValidationDecoration(int line, int col, const QString& message);

    struct TimelineCursorNote {
        int line = 1;
        int col = 1;
        int lane = -1;
        double second = 0.0;
    };

    QWidget* editorWidget_ = nullptr;
    PreviewCanvas* previewCanvas_ = nullptr;
    PreviewMediaController* previewMediaController_ = nullptr;
    QtPreviewSfxRuntime* previewSfxRuntime_ = nullptr;
    TimelineView* timelineView_ = nullptr;
    QPlainTextEdit* outputView_ = nullptr;
    QListWidget* errorList_ = nullptr;
    QAction* validateAction_ = nullptr;
    QAction* newAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* rotate45Action_ = nullptr;
    QAction* previewFromStartAction_ = nullptr;
    QAction* previewFromCursorAction_ = nullptr;
    QAction* pausePreviewAction_ = nullptr;
    QAction* toggleJudgeMarkersAction_ = nullptr;
    QAction* toggleTouchTrailAction_ = nullptr;
    QAction* previewAudioSettingsAction_ = nullptr;
    QAction* previewDisplaySettingsAction_ = nullptr;
    QLabel* currentFileLabel_ = nullptr;
    QTimer* metadataRefreshTimer_ = nullptr;
    QTimer* qtPreviewTimer_ = nullptr;
    QObject* editorViewport_ = nullptr;
    QProcess* previewProcess_ = nullptr;
    QString previewStdoutBuffer_;
    QString previewStderrBuffer_;
    QString currentFilePath_;
    QString lastOpenDir_;
    QString lastTrackPath_;
    QVector<TimelineCursorNote> timelineCursorNotes_;
    TextEncoding currentEncoding_ = TextEncoding::Utf8;
    int previewArrangeRetryCount_ = 0;
    bool legacyPygamePreviewEnabled_ = false;
    bool qtPreviewPlaying_ = false;
    bool qtPreviewPendingAudioCalibration_ = false;
    double qtPreviewStartSecond_ = 0.0;
    double qtPreviewPauseSecond_ = 0.0;
    double qtPreviewLastTimelineSecond_ = -1.0;
    QElapsedTimer qtPreviewElapsed_;
    bool showSlideTracks_ = true;
    bool showJudgeMarkers_ = false;
    bool showTouchTrail_ = false;
    double previewBackgroundBrightness_ = 0.2;
    bool previewShowDebugInfo_ = true;
    PreviewAudioSettings previewAudioSettings_;
};
