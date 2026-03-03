#include "MainWindow.h"
#include "PlainCodeEditor.h"
#include "PreviewCanvas.h"
#include "PreviewIntegration.h"
#include "PreviewMediaController.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"

#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QScreen>
#include <QStatusBar>
#include <QStringList>
#include <QStringConverter>
#include <QTimer>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextOption>
#include <QToolBar>
#include <QVBoxLayout>

#include "../third_party/miniaudio/miniaudio.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef HAVE_QSCINTILLA
#include <QColor>
#include <Qsci/qsciscintillabase.h>
#include <Qsci/qsciscintilla.h>
#endif

namespace {
QString timestampLine(const QString& title)
{
    return QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
        .arg(title);
}

QFont editorFont()
{
    static const QString embeddedConsolasFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(":/fonts/consola.ttf");
        if (fontId < 0) {
            return QString();
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty()) {
            return QString();
        }
        return families.first();
    }();

    QFont font;
    if (!embeddedConsolasFamily.isEmpty()) {
        font.setFamily(embeddedConsolasFamily);
    } else {
        font.setFamily("Consolas");
        if (!QFontInfo(font).exactMatch()) {
            font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(11);
    return font;
}

QFont uiOutputFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(11);
    return font;
}

QProcessEnvironment pythonProcessEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    return env;
}

QString decodeProcessText(const QByteArray& data)
{
    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString utf8Text = utf8Decoder.decode(data);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }
    return QString::fromLocal8Bit(data);
}

QVector<float> buildWaveformPeaks(const QString& trackPath, int peakCount = 1024)
{
    QVector<float> peaks;
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath) || peakCount <= 0) {
        return peaks;
    }

    const QByteArray pathBytes = QFile::encodeName(trackPath);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (ma_decoder_init_file(pathBytes.constData(), &config, &decoder) != MA_SUCCESS) {
        return peaks;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return peaks;
    }

    peaks.fill(0.0f, peakCount);
    constexpr ma_uint64 kChunkFrames = 4096;
    QVector<float> buffer(static_cast<int>(kChunkFrames), 0.0f);
    ma_uint64 frameCursor = 0;

    while (frameCursor < totalFrames) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buffer.data(), kChunkFrames, &framesRead) != MA_SUCCESS || framesRead == 0) {
            break;
        }
        for (ma_uint64 i = 0; i < framesRead; ++i) {
            const ma_uint64 absoluteFrame = frameCursor + i;
            const int binIndex = qBound(
                0,
                static_cast<int>((absoluteFrame * peakCount) / qMax<ma_uint64>(1, totalFrames)),
                peakCount - 1
            );
            peaks[binIndex] = qMax(peaks[binIndex], qAbs(buffer[static_cast<int>(i)]));
        }
        frameCursor += framesRead;
    }

    ma_decoder_uninit(&decoder);
    return peaks;
}

#ifdef HAVE_QSCINTILLA
constexpr int kErrorIndicator = 8;
constexpr int kErrorMarker = 8;
#endif
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    const QString legacyPreviewEnv = qEnvironmentVariable("MAICODE_ENABLE_PYGAME_PREVIEW", qEnvironmentVariable("MAIMURI_ENABLE_PYGAME_PREVIEW")).trimmed();
    legacyPygamePreviewEnabled_ =
        legacyPreviewEnv == "1" || legacyPreviewEnv.compare("true", Qt::CaseInsensitive) == 0;

    setWindowModified(false);
    updateWindowTitle();
    QSize initialSize(1280, 800);
    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect workArea = screen->availableGeometry();
        initialSize.setWidth(qMin(initialSize.width(), qMax(1, workArea.width() - 16)));
        // Reserve extra vertical room for title bar/frame so close button always stays reachable.
        initialSize.setHeight(qMin(initialSize.height(), qMax(1, workArea.height() - 56)));
    }
    resize(initialSize);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* toolsMenu = menuBar()->addMenu("&Tools");
    auto* transformMenu = menuBar()->addMenu("&Transform");

    auto* toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    QAction* resetLayoutAction = nullptr;

    newAction_ = new QAction("New", this);
    newAction_->setShortcut(QKeySequence::New);
    connect(newAction_, &QAction::triggered, this, &MainWindow::onNewFile);
    fileMenu->addAction(newAction_);

    openAction_ = new QAction("Open...", this);
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::onOpenFile);
    fileMenu->addAction(openAction_);

    saveAction_ = new QAction("Save", this);
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onSaveFile);
    fileMenu->addAction(saveAction_);

    saveAsAction_ = new QAction("Save As...", this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onSaveFileAs);
    fileMenu->addAction(saveAsAction_);

    fileMenu->addSeparator();

    auto* quitAction = new QAction("Quit", this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    fileMenu->addAction(quitAction);

    validateAction_ = new QAction("Validate Simai", this);
    validateAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(validateAction_, &QAction::triggered, this, &MainWindow::onValidateSimai);
    toolsMenu->addAction(validateAction_);

    previewFromStartAction_ = new QAction("Preview From Start", this);
    previewFromStartAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(previewFromStartAction_, &QAction::triggered, this, &MainWindow::onPreviewFromStart);
    toolsMenu->addAction(previewFromStartAction_);

    previewFromCursorAction_ = new QAction("Preview From Cursor", this);
    previewFromCursorAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(previewFromCursorAction_, &QAction::triggered, this, &MainWindow::onPreviewFromCursor);
    toolsMenu->addAction(previewFromCursorAction_);

    pausePreviewAction_ = new QAction("Pause/Resume Preview", this);
    pausePreviewAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Space));
    connect(pausePreviewAction_, &QAction::triggered, this, &MainWindow::onTogglePreviewPause);
    toolsMenu->addAction(pausePreviewAction_);

    toolsMenu->addSeparator();

    toggleJudgeMarkersAction_ = new QAction("Show Judge Markers", this);
    toggleJudgeMarkersAction_->setCheckable(true);
    toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    connect(toggleJudgeMarkersAction_, &QAction::toggled, this, &MainWindow::onToggleJudgeMarkers);
    toolsMenu->addAction(toggleJudgeMarkersAction_);
    toggleJudgeMarkersAction_->setEnabled(false);
    toggleJudgeMarkersAction_->setVisible(false);

    toggleTouchTrailAction_ = new QAction("Show Touch Trail", this);
    toggleTouchTrailAction_->setCheckable(true);
    toggleTouchTrailAction_->setChecked(showTouchTrail_);
    connect(toggleTouchTrailAction_, &QAction::toggled, this, &MainWindow::onToggleTouchTrail);
    toolsMenu->addAction(toggleTouchTrailAction_);
    toggleTouchTrailAction_->setEnabled(false);
    toggleTouchTrailAction_->setVisible(false);

    toolsMenu->addSeparator();

    previewAudioSettingsAction_ = new QAction("Audio Settings...", this);
    connect(previewAudioSettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewAudioSettings);
    toolsMenu->addAction(previewAudioSettingsAction_);

    previewDisplaySettingsAction_ = new QAction("Display Settings...", this);
    connect(previewDisplaySettingsAction_, &QAction::triggered, this, &MainWindow::onPreviewDisplaySettings);
    toolsMenu->addAction(previewDisplaySettingsAction_);

    rotate45Action_ = new QAction("Rotate +45", this);
    rotate45Action_->setShortcuts(QList<QKeySequence>{
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
        QKeySequence(Qt::Key_F6),
    });
    connect(rotate45Action_, &QAction::triggered, this, &MainWindow::onRotate45);
    transformMenu->addAction(rotate45Action_);

#ifdef HAVE_QSCINTILLA
    auto* editor = new QsciScintilla(this);
    editor->setUtf8(true);
    editor->setFont(editorFont());
    editor->setMarginsFont(editorFont());
    editor->setMarginsForegroundColor(QColor("#D4D4D4"));
    editor->setMarginsBackgroundColor(QColor("#2D2D2D"));
    editor->setMarginWidth(0, 48);
    editor->setMarginWidth(1, 14);
    editor->setMarginMarkerMask(1, 1 << kErrorMarker);
    editor->markerDefine(QsciScintilla::Circle, kErrorMarker);
    editor->setMarkerBackgroundColor(QColor("#E74C3C"), kErrorMarker);
    editor->indicatorDefine(QsciScintilla::SquiggleIndicator, kErrorIndicator);
    editor->setIndicatorForegroundColor(QColor("#E74C3C"), kErrorIndicator);
    editor->setAnnotationDisplay(QsciScintilla::AnnotationStandard);
    editor->setWrapMode(QsciScintilla::WrapCharacter);
    editor->setWrapVisualFlags(QsciScintilla::WrapFlagNone);
    editor->setWrapIndentMode(QsciScintilla::WrapIndentFixed);
    editor->setText(QString());
    editorWidget_ = editor;
#else
    auto* editor = new PlainCodeEditor(this);
    editor->setFont(editorFont());
    editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor->setWordWrapMode(QTextOption::WrapAnywhere);
    editor->setPlainText(QString());
    editorWidget_ = editor;
#endif
    setCentralWidget(editorWidget_);

    auto* previewDock = new QDockWidget("Preview", this);
    previewDock->setObjectName("PreviewDock");
    previewCanvas_ = new PreviewCanvas(previewDock);
    previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
    previewMediaController_ = new PreviewMediaController(this);
    previewSfxRuntime_ = new QtPreviewSfxRuntime(this);
    previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
    previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
    connect(previewMediaController_, &PreviewMediaController::frameChanged, this, [this](const QImage& frame) {
        if (previewCanvas_ == nullptr) {
            return;
        }
        previewCanvas_->setMediaFrame(frame);
        if (!qtPreviewPlaying_) {
            previewCanvas_->update();
        }
    });
    connect(
        previewMediaController_,
        &PreviewMediaController::backgroundBrightnessChanged,
        previewCanvas_,
        &PreviewCanvas::setBackgroundBrightness
    );
    connect(previewMediaController_, &PreviewMediaController::playbackPositionChanged, this, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        applyQtPreviewPosition(second, true);
    });
    connect(previewMediaController_, &PreviewMediaController::playbackFinished, this, [this]() {
        if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
            qtPreviewPauseSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
        } else if (previewMediaController_ != nullptr) {
            qtPreviewPauseSecond_ = previewMediaController_->currentPlaybackSecond();
        }
        stopQtPreviewPlayback(true);
        statusBar()->showMessage("Qt preview reached the end of current media.");
    });
    previewDock->setWidget(previewCanvas_);
    addDockWidget(Qt::RightDockWidgetArea, previewDock);
    const int previewPaneWidth = qMax(320, static_cast<int>(width() * 0.36));
    previewDock->resize(previewPaneWidth, height());

    auto* timelineDock = new QDockWidget("Timeline", this);
    timelineDock->setObjectName("TimelineDock");
    timelineView_ = new TimelineView(timelineDock);
    timelineView_->setShowSlideTracks(true);
    connect(timelineView_, &TimelineView::ctrlClickNavigateRequested, this, &MainWindow::jumpToNearestTimelineNote);
    timelineDock->setWidget(timelineView_);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);

#ifdef HAVE_QSCINTILLA
    connect(qobject_cast<QsciScintilla*>(editorWidget_), &QsciScintilla::textChanged, this, [this]() {
        setWindowModified(true);
        scheduleTimelineRefresh();
    });
#else
    connect(qobject_cast<PlainCodeEditor*>(editorWidget_), &QPlainTextEdit::textChanged, this, [this]() {
        setWindowModified(true);
        scheduleTimelineRefresh();
    });
#endif

    outputView_ = nullptr;
    timelineDock->resize(qMax(480, width() - previewPaneWidth), 220);

    auto* errorDock = new QDockWidget("Validation Errors", this);
    errorDock->setObjectName("ValidationErrorsDock");
    errorList_ = new QListWidget(errorDock);
    errorList_->setFont(uiOutputFont());
    connect(errorList_, &QListWidget::itemActivated, this, &MainWindow::onErrorItemActivated);
    connect(errorList_, &QListWidget::itemClicked, this, &MainWindow::onErrorItemActivated);
    errorDock->setWidget(errorList_);
    addDockWidget(Qt::BottomDockWidgetArea, errorDock);
    splitDockWidget(timelineDock, errorDock, Qt::Horizontal);
    const int errorDockTargetWidth = qMax(240, previewPaneWidth / 2);
    errorDock->setMinimumWidth(errorDockTargetWidth);
    errorDock->resize(errorDockTargetWidth, 220);

    const auto applyDefaultDockLayout = [this, previewDock, timelineDock, errorDock]() {
        const int previewWidth = qMax(320, static_cast<int>(width() * 0.36));
        const int errorWidth = qMax(240, previewWidth / 2);
        const int timelineWidth = qMax(560, width() - previewWidth - errorWidth);

        previewDock->setFloating(false);
        timelineDock->setFloating(false);
        errorDock->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, previewDock);
        addDockWidget(Qt::BottomDockWidgetArea, timelineDock);
        addDockWidget(Qt::BottomDockWidgetArea, errorDock);
        splitDockWidget(timelineDock, errorDock, Qt::Horizontal);

        previewDock->resize(previewWidth, height());
        timelineDock->resize(timelineWidth, 220);
        errorDock->setMinimumWidth(errorWidth);
        errorDock->resize(errorWidth, 220);
        resizeDocks(QList<QDockWidget*>{timelineDock, errorDock}, QList<int>{timelineWidth, errorWidth}, Qt::Horizontal);
    };
    applyDefaultDockLayout();

    resetLayoutAction = new QAction("Reset Window Layout", this);
    toolsMenu->addAction(resetLayoutAction);
    connect(resetLayoutAction, &QAction::triggered, this, applyDefaultDockLayout);
    QTimer::singleShot(0, this, applyDefaultDockLayout);

    toolBar->addAction(openAction_);
    toolBar->addAction(saveAction_);
    toolBar->addAction(previewFromStartAction_);
    toolBar->addAction(previewFromCursorAction_);
    toolBar->addAction(pausePreviewAction_);
    statusBar()->addPermanentWidget(new QLabel("Current File:", this));
    currentFileLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(currentFileLabel_, 1);
    updateCurrentFileLabel();

    metadataRefreshTimer_ = new QTimer(this);
    metadataRefreshTimer_->setSingleShot(true);
    metadataRefreshTimer_->setInterval(0);
    connect(metadataRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshTimelineMetadata);

    qtPreviewTimer_ = new QTimer(this);
    qtPreviewTimer_->setInterval(16);
    qtPreviewTimer_->setTimerType(Qt::PreciseTimer);
    connect(qtPreviewTimer_, &QTimer::timeout, this, &MainWindow::onQtPreviewTick);

#ifdef HAVE_QSCINTILLA
    editorViewport_ = qobject_cast<QsciScintilla*>(editorWidget_)->viewport();
#else
    editorViewport_ = qobject_cast<PlainCodeEditor*>(editorWidget_)->viewport();
#endif
    if (editorViewport_ != nullptr) {
        editorViewport_->installEventFilter(this);
    }

#ifdef HAVE_QSCINTILLA
    statusBar()->showMessage("QScintilla detected.");
#else
    statusBar()->showMessage("QScintilla not found. Using PlainCodeEditor fallback.");
#endif

    loadPortableState();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->reloadAssets(previewAudioSettings_);
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    if (toggleJudgeMarkersAction_ != nullptr) {
        toggleJudgeMarkersAction_->setChecked(showJudgeMarkers_);
    }
    if (toggleTouchTrailAction_ != nullptr) {
        toggleTouchTrailAction_->setChecked(showTouchTrail_);
    }
    if (timelineView_ != nullptr) {
        timelineView_->setShowSlideTracks(true);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->reset();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
    }
    scheduleTimelineRefresh();
    if (legacyPygamePreviewEnabled_) {
        appendOutput("preview/bootstrap", "initializing resident preview session");
        bootstrapPreviewWindow();
        QTimer::singleShot(1500, this, [this]() {
            if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
                appendOutput("preview/bootstrap", "startup retry");
                bootstrapPreviewWindow();
            }
        });
    } else {
        appendOutput("preview/bootstrap", "legacy pygame preview disabled by default");
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveBeforeContinue()) {
        savePortableState();
        stopPreviewSession();
        event->accept();
    } else {
        event->ignore();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && (mouseEvent->modifiers() & Qt::ControlModifier)) {
            int line = 1;
            int col = 1;

#ifdef HAVE_QSCINTILLA
            auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
            const int pos = editor->SendScintilla(
                QsciScintillaBase::SCI_POSITIONFROMPOINTCLOSE,
                mouseEvent->position().x(),
                mouseEvent->position().y()
            );
            if (pos >= 0) {
                int lineIndex = 0;
                int columnIndex = 0;
                editor->lineIndexFromPosition(pos, &lineIndex, &columnIndex);
                editor->setCursorPosition(lineIndex, columnIndex);
                line = lineIndex + 1;
                col = columnIndex + 1;
            } else {
                const auto fallback = currentCursorLineCol();
                line = fallback.first;
                col = fallback.second;
            }
#else
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            QTextCursor cursor = editor->cursorForPosition(mouseEvent->pos());
            editor->setTextCursor(cursor);
            line = cursor.blockNumber() + 1;
            col = cursor.positionInBlock() + 1;
#endif
            seekTimelineToCursor(line, col);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::maybeSaveBeforeContinue()
{
    if (!isWindowModified()) {
        return true;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        "Unsaved Changes",
        "Current document has unsaved changes. Save before continue?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    if (choice == QMessageBox::Save) {
        return onSaveFile();
    }
    return choice == QMessageBox::Discard;
}

void MainWindow::onNewFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }
    setEditorText(QString());
    clearValidationErrors();
    clearValidationDecorations();
    currentEncoding_ = TextEncoding::Utf8;
    setCurrentFilePath(QString());
    setWindowModified(false);
    timelineCursorNotes_.clear();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->clearTimeline();
    }
    stopQtPreviewPlayback(false);
    if (timelineView_ != nullptr) {
        timelineView_->clear();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->reset();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setChartPath(QString());
    }
    scheduleTimelineRefresh();
    statusBar()->showMessage("New file.");
}

void MainWindow::onOpenFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open simai file",
        resolveInitialOpenDirectory(),
        "Simai (*.txt *.simai);;All Files (*.*)"
    );
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Open Failed", "Cannot open file:\n" + path);
        return;
    }

    const QByteArray bytes = file.readAll();
    QString text;
    TextEncoding encodingUsed = TextEncoding::Utf8;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            encodingUsed = TextEncoding::System;
        }
    }

    setEditorText(text);
    clearValidationErrors();
    clearValidationDecorations();
    currentEncoding_ = encodingUsed;
    setCurrentFilePath(path);
    setWindowModified(false);
    scheduleTimelineRefresh();
    statusBar()->showMessage(
        QString("Opened: %1 (%2)")
            .arg(QFileInfo(path).fileName())
            .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
    );
}

bool MainWindow::onSaveFile()
{
    if (currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(currentFilePath_);
}

bool MainWindow::onSaveFileAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save simai file",
        currentFilePath_.isEmpty() ? QString("chart.txt") : currentFilePath_,
        "Simai (*.txt *.simai);;All Files (*.*)"
    );
    if (path.isEmpty()) {
        return false;
    }
    return saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Save Failed", "Cannot write file:\n" + path);
        return false;
    }
    QByteArray data;
    if (currentEncoding_ == TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(editorText());
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(editorText());
    }
    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, "Save Failed", "Write failed:\n" + path);
        return false;
    }
    setCurrentFilePath(path);
    setWindowModified(false);
    statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    return true;
}

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    const QString original = editorText();
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    int endLine = qMax(0, editor->lines() - 1);
    int endIndex = qMax(0, editor->lineLength(endLine));
    editor->beginUndoAction();
    editor->setSelection(0, 0, endLine, endIndex);
    editor->replaceSelectedText(transformed);
    editor->endUndoAction();
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(transformed);
    cursor.endEditBlock();
#endif

    setWindowModified(true);
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        statusBar()->showMessage(QString("%1: select a segment first.").arg(opName));
        return false;
    }

    const QString original = editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    int changed = 0;
    const QString transformed = transform(selected, &changed);
    if (transformed == selected) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    int lineFrom = 0;
    int indexFrom = 0;
    int lineTo = 0;
    int indexTo = 0;
    editor->lineIndexFromPosition(begin, &lineFrom, &indexFrom);
    editor->lineIndexFromPosition(finish, &lineTo, &indexTo);
    editor->beginUndoAction();
    editor->setSelection(lineFrom, indexFrom, lineTo, indexTo);
    editor->replaceSelectedText(transformed);
    editor->endUndoAction();
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.setPosition(begin);
    cursor.setPosition(finish, QTextCursor::KeepAnchor);
    cursor.insertText(transformed);
    cursor.endEditBlock();
#endif

    setWindowModified(true);
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    int line = 0;
    int index = 0;
    editor->getCursorPosition(&line, &index);
    return {line + 1, index + 1};
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
#endif
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    if (startPos == nullptr || endPos == nullptr) {
        return false;
    }

#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    if (!editor->hasSelectedText()) {
        return false;
    }
    int lineFrom = 0;
    int indexFrom = 0;
    int lineTo = 0;
    int indexTo = 0;
    editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
    *startPos = editor->positionFromLineIndex(lineFrom, indexFrom);
    *endPos = editor->positionFromLineIndex(lineTo, indexTo);
    return *endPos > *startPos;
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }
    *startPos = cursor.selectionStart();
    *endPos = cursor.selectionEnd();
    return *endPos > *startPos;
#endif
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    if (editor->hasSelectedText()) {
        int lineFrom = 0;
        int indexFrom = 0;
        int lineTo = 0;
        int indexTo = 0;
        editor->getSelection(&lineFrom, &indexFrom, &lineTo, &indexTo);
        return {lineFrom + 1, indexFrom + 1};
    }
    return currentCursorLineCol();
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return currentCursorLineCol();
    }
    cursor.setPosition(cursor.selectionStart());
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
#endif
}

void MainWindow::setEditorText(const QString& text)
{
    QSignalBlocker blocker(editorWidget_);
#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    editor->setText(text);
    editor->emptyUndoBuffer();
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    editor->setPlainText(text);
    editor->document()->clearUndoRedoStacks();
#endif
}

void MainWindow::setCurrentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool pathChanged = normalizedPath != currentFilePath_;
    if (pathChanged) {
        stopQtPreviewPlayback(false);
    }
    currentFilePath_ = normalizedPath;
    if (!currentFilePath_.isEmpty()) {
        setLastOpenDirectory(currentFilePath_);

        const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
        if (QFileInfo::exists(siblingTrack)) {
            // Keep preview audio in sync with the currently opened chart directory.
            lastTrackPath_ = QDir::cleanPath(siblingTrack);
        } else {
            lastTrackPath_.clear();
        }
    } else {
        lastTrackPath_.clear();
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setChartPath(currentFilePath_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    if (timelineView_ != nullptr) {
        timelineView_->setWaveformData(buildWaveformPeaks(lastTrackPath_));
    }
    if (legacyPygamePreviewEnabled_ && pathChanged && !currentFilePath_.isEmpty()) {
        stopPreviewSession();
        if (ensurePreviewSessionStarted()) {
            sendPreviewPrepareCommand();
        }
    }
}

void MainWindow::updateWindowTitle()
{
    const QString namePart = currentFilePath_.isEmpty()
        ? QString("Untitled.simai")
        : QFileInfo(currentFilePath_).fileName();
    setWindowTitle(QString("maicode - %1[*]").arg(namePart));
}

void MainWindow::updateCurrentFileLabel()
{
    if (currentFileLabel_ == nullptr) {
        return;
    }
    if (currentFilePath_.isEmpty()) {
        currentFileLabel_->setText("(unsaved)");
    } else {
        currentFileLabel_->setText(QDir::toNativeSeparators(currentFilePath_));
    }
}

QString MainWindow::editorText() const
{
#ifdef HAVE_QSCINTILLA
    return qobject_cast<QsciScintilla*>(editorWidget_)->text();
#else
    return qobject_cast<PlainCodeEditor*>(editorWidget_)->toPlainText();
#endif
}

void MainWindow::clearValidationErrors()
{
    errorList_->clear();
}

void MainWindow::clearValidationDecorations()
{
#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    editor->clearAnnotations();
    editor->markerDeleteAll(kErrorMarker);
    editor->SendScintilla(QsciScintillaBase::SCI_SETINDICATORCURRENT, kErrorIndicator);
    editor->SendScintilla(QsciScintillaBase::SCI_INDICATORCLEARRANGE, 0, editor->length());
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    editor->setExtraSelections({});
#endif
}

void MainWindow::addValidationError(int line, int col, const QString& message)
{
    auto* item = new QListWidgetItem(QString("L%1 C%2  %3").arg(line).arg(col).arg(message), errorList_);
    item->setData(Qt::UserRole, line);
    item->setData(Qt::UserRole + 1, col);
}

void MainWindow::addValidationDecoration(int line, int col, const QString& message)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    const int lineIndex = line - 1;
    int colIndex = col - 1;
    const int lineLen = qMax(1, editor->lineLength(lineIndex));
    if (colIndex >= lineLen) {
        colIndex = lineLen - 1;
    }

    editor->markerAdd(lineIndex, kErrorMarker);
    editor->fillIndicatorRange(lineIndex, colIndex, lineIndex, colIndex + 1, kErrorIndicator);

    const QString oldAnno = editor->annotation(lineIndex);
    const QString merged = oldAnno.isEmpty() ? message : (oldAnno + "\n" + message);
    editor->annotate(lineIndex, merged);
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qMax(0, col - 1));
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection sel;
    sel.cursor = cursor;
    sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    sel.format.setUnderlineColor(QColor("#E74C3C"));
    sel.format.setToolTip(message);

    auto selections = editor->extraSelections();
    selections.append(sel);
    editor->setExtraSelections(selections);
#endif
}

void MainWindow::jumpToLocation(int line, int col)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

#ifdef HAVE_QSCINTILLA
    auto* editor = qobject_cast<QsciScintilla*>(editorWidget_);
    editor->setCursorPosition(line - 1, col - 1);
    editor->ensureLineVisible(line - 1);
    editor->setFocus();
#else
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + col - 1);
    cursor.select(QTextCursor::LineUnderCursor);
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    editor->setFocus();
#endif
}

QString MainWindow::resolvePreviewSessionScriptPath() const
{
    const QString envPath = qEnvironmentVariable("MAICODE_PREVIEW_SESSION_SCRIPT", qEnvironmentVariable("MAIMURI_PREVIEW_SESSION_SCRIPT"));
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
    }

    QStringList candidates;
    candidates << QDir::cleanPath(QDir::current().filePath("../MaiMuriDX/api_preview_session.py"));
    candidates << QDir::cleanPath(QDir::current().filePath("..\\MaiMuriDX\\api_preview_session.py"));
    candidates << QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("..\\..\\MaiMuriDX\\api_preview_session.py"));
    candidates << QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("..\\..\\..\\MaiMuriDX\\api_preview_session.py"));

    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

void MainWindow::scheduleTimelineRefresh()
{
    if (metadataRefreshTimer_ == nullptr) {
        return;
    }
    metadataRefreshTimer_->stop();
    metadataRefreshTimer_->start();
}

void MainWindow::refreshTimelineMetadata()
{
    if (timelineView_ == nullptr) {
        return;
    }
    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(editorText());
    QVector<TimelineBeatMarker> beatMarkers = nativeResult.beatMarkers;
    QVector<TimelineNoteMarker> noteMarkers = nativeResult.noteMarkers;
    timelineCursorNotes_.clear();
    timelineCursorNotes_.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        TimelineCursorNote cursorNote;
        cursorNote.line = qMax(1, marker.sourceLine);
        cursorNote.col = 1;
        cursorNote.lane = marker.lane;
        cursorNote.second = marker.second;
        timelineCursorNotes_.append(cursorNote);
    }

    std::sort(timelineCursorNotes_.begin(), timelineCursorNotes_.end(), [](const TimelineCursorNote& a, const TimelineCursorNote& b) {
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return a.second < b.second;
    });

    double durationSeconds = nativeResult.durationSeconds;
    if (durationSeconds <= 0.0) {
        for (const TimelineNoteMarker& marker : noteMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
            if (marker.endSecond > marker.second) {
                durationSeconds = qMax(durationSeconds, marker.endSecond);
            }
        }
        for (const TimelineBeatMarker& marker : beatMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
        }
    }

    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->configureTimeline(noteMarkers);
    }

    timelineView_->setTimelineData(beatMarkers, noteMarkers, durationSeconds);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setNoteMarkers(noteMarkers);
    }
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    if (timelineCursorNotes_.isEmpty()) {
        return -1.0;
    }

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        if (note.line > line || (note.line == line && note.col >= col)) {
            return note.second;
        }
    }
    return timelineCursorNotes_.constLast().second;
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    if (second < 0.0) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    timelineView_->setCursorSeconds(second);
    timelineView_->setPlayheadSeconds(second, true);
    statusBar()->showMessage(QString("Timeline seek: L%1 C%2 -> %3s").arg(line).arg(col).arg(second, 0, 'f', 3));
}

void MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    const double startSecond = qMax(0.0, second);
    qtPreviewStartSecond_ = startSecond;
    qtPreviewPauseSecond_ = startSecond;
    qtPreviewLastTimelineSecond_ = startSecond;
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
        timelineView_->setPlayheadSeconds(startSecond, true);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(startSecond);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(startSecond);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->startBackgroundTrack(startSecond);
        if (previewSfxRuntime_->hasBackgroundTrack()) {
            qtPreviewStartSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
            qtPreviewPauseSecond_ = qtPreviewStartSecond_;
            qtPreviewLastTimelineSecond_ = qtPreviewStartSecond_;
            qtPreviewPendingAudioCalibration_ = true;
            if (timelineView_ != nullptr) {
                timelineView_->setPlayheadSeconds(qtPreviewStartSecond_, true);
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
            if (previewMediaController_ != nullptr) {
                previewMediaController_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
        }
        previewSfxRuntime_->resetCursor(qtPreviewStartSecond_, !resumeFromPause);
        if (!resumeFromPause) {
            previewSfxRuntime_->drainEvents(qtPreviewStartSecond_);
        }
        previewSfxRuntime_->syncTouchholdVoices(qtPreviewStartSecond_);
    } else {
        qtPreviewPendingAudioCalibration_ = false;
    }

    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->startPlayback(qtPreviewStartSecond_);
    }

    qtPreviewElapsed_.restart();
    qtPreviewPlaying_ = true;
    if (qtPreviewTimer_ != nullptr && !qtPreviewTimer_->isActive()) {
        qtPreviewTimer_->start();
    }
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        qtPreviewPauseSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
        previewSfxRuntime_->pauseBackgroundTrack();
    } else if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        qtPreviewPauseSecond_ = previewMediaController_->currentPlaybackSecond();
    }
    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->pausePlayback();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (!keepPosition) {
        qtPreviewPauseSecond_ = 0.0;
    }
    qtPreviewPlaying_ = false;
    qtPreviewPendingAudioCalibration_ = false;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->stopAll();
    }
}

void MainWindow::applyQtPreviewPosition(double second, bool centerView)
{
    qtPreviewPauseSecond_ = second;
    if (timelineView_ != nullptr
        && (qtPreviewLastTimelineSecond_ < 0.0 || qAbs(second - qtPreviewLastTimelineSecond_) >= (1.0 / 30.0))) {
        timelineView_->setPlayheadSeconds(second, centerView);
        qtPreviewLastTimelineSecond_ = second;
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
}

void MainWindow::onQtPreviewTick()
{
    if (!qtPreviewPlaying_) {
        return;
    }
    if (qtPreviewPendingAudioCalibration_ && previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        const double calibratedSecond = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
        qtPreviewStartSecond_ = calibratedSecond;
        qtPreviewPauseSecond_ = calibratedSecond;
        qtPreviewLastTimelineSecond_ = -1.0;
        qtPreviewElapsed_.restart();
        applyQtPreviewPosition(calibratedSecond, true);
        if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
            previewMediaController_->setPlayheadSeconds(calibratedSecond);
        }
        previewSfxRuntime_->drainEvents(calibratedSecond);
        previewSfxRuntime_->syncTouchholdVoices(calibratedSecond);
        qtPreviewPendingAudioCalibration_ = false;
        return;
    }
    const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
    double second = qtPreviewStartSecond_ + elapsedSeconds;
    const double duration = timelineView_ != nullptr ? timelineView_->durationSeconds() : -1.0;
    if (duration > 0.0 && second > duration) {
        second = duration;
        applyQtPreviewPosition(second, true);
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->drainEvents(second);
        }
        stopQtPreviewPlayback(true);
        statusBar()->showMessage("Qt preview reached the end of current timeline.");
        return;
    }

    applyQtPreviewPosition(second, true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->drainEvents(second);
    }
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    if (timelineCursorNotes_.isEmpty()) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    const TimelineCursorNote* best = nullptr;
    bool foundSameLane = false;
    const bool preferLane = lane >= 1;
    const double target = qMax(0.0, second);

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        const bool sameLane = preferLane && note.lane == lane;
        if (preferLane) {
            if (sameLane && !foundSameLane) {
                best = &note;
                foundSameLane = true;
                continue;
            }
            if (!sameLane && foundSameLane) {
                continue;
            }
            if (!sameLane && !foundSameLane && best != nullptr) {
                // Still in all-note fallback mode.
            }
        }

        if (best == nullptr) {
            best = &note;
            continue;
        }

        const double bestDelta = qAbs(best->second - target);
        const double noteDelta = qAbs(note.second - target);
        if (noteDelta + 1e-9 < bestDelta) {
            best = &note;
            continue;
        }
        if (qAbs(noteDelta - bestDelta) <= 1e-9) {
            if (note.line < best->line || (note.line == best->line && note.col < best->col)) {
                best = &note;
            }
        }
    }

    if (best == nullptr) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    jumpToLocation(best->line, best->col);
    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(target, 0, 'f', 3)
            .arg(best->line)
            .arg(best->col)
    );
}

QString MainWindow::resolveDefaultTrackPath() const
{
    const QString envTrack = qEnvironmentVariable("MAICODE_TRACK_PATH", qEnvironmentVariable("MAIMURI_TRACK_PATH"));
    if (!envTrack.isEmpty() && QFileInfo::exists(envTrack)) {
        return envTrack;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
        if (QFileInfo::exists(siblingTrack)) {
            return siblingTrack;
        }
    }
    if (!lastTrackPath_.isEmpty() && QFileInfo::exists(lastTrackPath_)) {
        return lastTrackPath_;
    }
    return QString();
}

QString MainWindow::resolvePreviewSkinDir() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString assetSkinDir = QDir::cleanPath(appDir.filePath("..\\..\\assets\\skin"));
    if (QFileInfo::exists(QDir(assetSkinDir).filePath("tap.png"))) {
        return assetSkinDir;
    }
    return QString();
}

QString MainWindow::resolvePortableStateFilePath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(".maicode_state.json");
}

QString MainWindow::resolveInitialOpenDirectory() const
{
    if (!lastOpenDir_.isEmpty() && QDir(lastOpenDir_).exists()) {
        return lastOpenDir_;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString currentDir = QFileInfo(currentFilePath_).absolutePath();
        if (QDir(currentDir).exists()) {
            return currentDir;
        }
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QDir(appDir).exists()) {
        return appDir;
    }
    return QDir::currentPath();
}

void MainWindow::loadPortableState()
{
    lastOpenDir_.clear();
    previewAudioSettings_ = PreviewAudioSettings();
    showSlideTracks_ = true;
    showJudgeMarkers_ = false;
    showTouchTrail_ = false;
    previewBackgroundBrightness_ = 0.2;
    previewShowDebugInfo_ = true;

    QFile file(resolvePortableStateFilePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    const QString dir = root.value("last_open_dir").toString();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        lastOpenDir_ = QDir::cleanPath(dir);
    }
    const QString trackPath = root.value("last_track_path").toString();
    if (!trackPath.isEmpty() && QFileInfo::exists(trackPath)) {
        lastTrackPath_ = QDir::cleanPath(trackPath);
    }
    showSlideTracks_ = true;
    if (root.value("show_judge_markers").isBool()) {
        showJudgeMarkers_ = root.value("show_judge_markers").toBool(false);
    }
    if (root.value("show_touch_trail").isBool()) {
        showTouchTrail_ = root.value("show_touch_trail").toBool(false);
    }
    if (root.value("preview_background_brightness").isDouble()) {
        previewBackgroundBrightness_ = qBound(0.0, root.value("preview_background_brightness").toDouble(0.2), 1.0);
    }
    if (root.value("preview_show_debug_info").isBool()) {
        previewShowDebugInfo_ = root.value("preview_show_debug_info").toBool(true);
    }
    if (root.value("preview_audio").isObject()) {
        previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("preview_audio").toObject());
    } else {
        previewAudioSettings_.bgmVolume = root.value("bgm_volume").toDouble(previewAudioSettings_.bgmVolume);
        const double legacyAnswer = root.value("sfx_volume").toDouble(previewAudioSettings_.answerVolume);
        const double legacySlide = root.value("sfx_volume").toDouble(previewAudioSettings_.slideVolume);
        const double legacyBreak = root.value("sfx_volume").toDouble(previewAudioSettings_.breakVolume);
        const double legacyEx = root.value("sfx_volume").toDouble(previewAudioSettings_.exVolume);
        const double legacyTouch = root.value("sfx_volume").toDouble(previewAudioSettings_.touchVolume);
        const double legacyTouchhold = root.value("sfx_volume").toDouble(previewAudioSettings_.touchholdVolume);
        previewAudioSettings_.answerVolume = root.value("answer_volume").toDouble(legacyAnswer);
        previewAudioSettings_.slideVolume = root.value("slide_volume").toDouble(legacySlide);
        previewAudioSettings_.breakVolume = root.value("break_volume").toDouble(legacyBreak);
        previewAudioSettings_.exVolume = root.value("ex_volume").toDouble(legacyEx);
        previewAudioSettings_.touchVolume = root.value("touch_volume").toDouble(legacyTouch);
        previewAudioSettings_.touchholdVolume = root.value("touchhold_volume").toDouble(legacyTouchhold);
        previewAudioSettings_.normalize();
    }
}

void MainWindow::savePortableState() const
{
    const QString path = resolvePortableStateFilePath();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QJsonObject root;
    root.insert("last_open_dir", lastOpenDir_);
    root.insert("last_track_path", lastTrackPath_);
    root.insert("show_slide_tracks", true);
    root.insert("show_judge_markers", showJudgeMarkers_);
    root.insert("show_touch_trail", showTouchTrail_);
    root.insert("preview_background_brightness", previewBackgroundBrightness_);
    root.insert("preview_show_debug_info", previewShowDebugInfo_);
    root.insert("preview_audio", previewAudioSettings_.toJson());
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return;
    }
    file.commit();
}

void MainWindow::setLastOpenDirectory(const QString& pathOrDir)
{
    if (pathOrDir.isEmpty()) {
        return;
    }

    QString dirCandidate;
    const QFileInfo info(pathOrDir);
    if (info.isDir()) {
        dirCandidate = info.absoluteFilePath();
    } else {
        dirCandidate = info.absolutePath();
    }
    dirCandidate = QDir::cleanPath(dirCandidate);
    if (!QDir(dirCandidate).exists()) {
        return;
    }
    if (lastOpenDir_ == dirCandidate) {
        return;
    }
    lastOpenDir_ = dirCandidate;
    savePortableState();
}

QString MainWindow::transformRotate45(const QString& input, int* changedCount) const
{
    QString out;
    out.reserve(input.size());
    int changed = 0;

    int bpmDepth = 0;
    int beatsDepth = 0;
    int signatureDepth = 0;
    bool inComment = false;

    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input[i];
        const QChar next = (i + 1 < input.size()) ? input[i + 1] : QChar();

        if (ch == '\n') {
            inComment = false;
            out.append(ch);
            continue;
        }
        if (!inComment && ch == '|' && next == '|') {
            inComment = true;
            out.append(ch);
            out.append(next);
            ++i;
            continue;
        }
        if (inComment) {
            out.append(ch);
            continue;
        }

        if (ch == '(') {
            ++bpmDepth;
            out.append(ch);
            continue;
        }
        if (ch == ')' && bpmDepth > 0) {
            --bpmDepth;
            out.append(ch);
            continue;
        }
        if (ch == '{') {
            ++beatsDepth;
            out.append(ch);
            continue;
        }
        if (ch == '}' && beatsDepth > 0) {
            --beatsDepth;
            out.append(ch);
            continue;
        }
        if (ch == '[') {
            ++signatureDepth;
            out.append(ch);
            continue;
        }
        if (ch == ']' && signatureDepth > 0) {
            --signatureDepth;
            out.append(ch);
            continue;
        }

        if (bpmDepth == 0 && beatsDepth == 0 && signatureDepth == 0 && ch >= '1' && ch <= '8') {
            const int idx = ch.unicode() - '1';
            out.append(QChar('1' + (idx + 1) % 8));
            ++changed;
            continue;
        }

        out.append(ch);
    }
    if (changedCount != nullptr) {
        *changedCount = changed;
    }

    return out;
}

void MainWindow::onRotate45()
{
    applySelectionBatchTransform("Rotate +45", [this](const QString& text, int* changedCount) {
        return transformRotate45(text, changedCount);
    });
}

void MainWindow::bootstrapPreviewWindow()
{
    const QString scriptPath = resolvePreviewSessionScriptPath();
    appendOutput("preview/bootstrap", scriptPath.isEmpty() ? "script=(not found)" : ("script=" + scriptPath));
    if (!ensurePreviewSessionStarted()) {
        appendOutput("preview/bootstrap", "failed to start resident preview session");
        return;
    }
    appendOutput(
        "preview/bootstrap",
        QString("resident preview session started, pid=%1").arg(previewProcess_ != nullptr ? previewProcess_->processId() : -1)
    );
    previewArrangeRetryCount_ = 0;
    QTimer::singleShot(80, this, &MainWindow::arrangeWithPreviewWindow);
}

void MainWindow::arrangeWithPreviewWindow()
{
#ifdef Q_OS_WIN
    QScreen* screen = this->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        appendOutput("preview/layout", "no available screen");
        return;
    }

    const QRect workArea = screen->availableGeometry();
    const PreviewIntegration::SideBySideLayout layout = PreviewIntegration::computeSideBySideLayout(workArea);
    QRect editorFrameRect = layout.editorRect;
    const int frameLeftInset = qMax(0, geometry().left() - frameGeometry().left());
    const int frameTopInset = qMax(0, geometry().top() - frameGeometry().top());
    const int frameRightInset = qMax(0, frameGeometry().right() - geometry().right());
    const int frameBottomInset = qMax(0, frameGeometry().bottom() - geometry().bottom());
    const int frameExtraW = frameLeftInset + frameRightInset;
    const int frameExtraH = frameTopInset + frameBottomInset;

    // Strict frame-level alignment: editor frame starts exactly at layout.editorRect
    // so preview/editor never overlap.
    editorFrameRect.setTop(workArea.top());
    editorFrameRect.setBottom(workArea.bottom());
    if (editorFrameRect.left() < workArea.left()) {
        editorFrameRect.moveLeft(workArea.left());
    }
    if (editorFrameRect.right() > workArea.right()) {
        editorFrameRect.moveRight(workArea.right());
    }

    QRect editorRect(
        editorFrameRect.left() + frameLeftInset,
        editorFrameRect.top() + frameTopInset,
        qMax(320, editorFrameRect.width() - frameExtraW),
        qMax(320, editorFrameRect.height() - frameExtraH)
    );
    setGeometry(editorRect);

    QString detail;
    const qint64 pid = (previewProcess_ != nullptr && previewProcess_->state() == QProcess::Running)
        ? previewProcess_->processId()
        : -1;
    if (!PreviewIntegration::placePreviewWindow(pid, layout.previewRect, &detail)) {
        if (previewArrangeRetryCount_ < 30) {
            ++previewArrangeRetryCount_;
            QTimer::singleShot(120, this, &MainWindow::arrangeWithPreviewWindow);
        } else {
            appendOutput("preview/layout", "preview window placement failed: " + detail);
        }
        return;
    }

    if (previewArrangeRetryCount_ > 0) {
        appendOutput("preview/layout", QString("arranged after retry=%1 (%2)").arg(previewArrangeRetryCount_).arg(detail));
    } else {
        appendOutput("preview/layout", "arranged (" + detail + ")");
    }
    previewArrangeRetryCount_ = 0;
#endif
}

void MainWindow::onPreviewFromStart()
{
    refreshTimelineMetadata();
    if (!legacyPygamePreviewEnabled_) {
        if (timelineView_ != nullptr) {
            timelineView_->setCursorSeconds(0.0);
        }
        startQtPreviewPlayback(0.0, false);
        statusBar()->showMessage("Qt preview playback started from 0.00s.");
        return;
    }
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadSeconds(0.0, true);
    }
    startPreviewProcess("start", -1, -1);
}

void MainWindow::onPreviewFromCursor()
{
    const auto [line, col] = currentSelectionOrCursorLineCol();
    refreshTimelineMetadata();
    seekTimelineToCursor(line, col);
    const double second = timelineSecondForCursor(line, col);
    if (!legacyPygamePreviewEnabled_) {
        startQtPreviewPlayback(second, false);
        statusBar()->showMessage(QString("Qt preview playback started from %1s.").arg(second, 0, 'f', 2));
        return;
    }
    startPreviewProcess("cursor", line, col);
}

void MainWindow::onTogglePreviewPause()
{
    if (!legacyPygamePreviewEnabled_) {
        if (qtPreviewPlaying_) {
            stopQtPreviewPlayback(true);
            statusBar()->showMessage(QString("Qt preview paused at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
        } else {
            startQtPreviewPlayback(qtPreviewPauseSecond_, true);
            statusBar()->showMessage(QString("Qt preview resumed at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
        }
        return;
    }
    if (!ensurePreviewSessionStarted()) {
        return;
    }
    QJsonObject cmd{
        {"cmd", "pause_toggle"},
    };
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send pause command");
        return;
    }
    previewProcess_->waitForBytesWritten(1000);
}

void MainWindow::onToggleJudgeMarkers(bool checked)
{
    showJudgeMarkers_ = checked;
    savePortableState();
    sendPreviewConfigCommand();
    statusBar()->showMessage(showJudgeMarkers_ ? "Judge markers enabled." : "Judge markers hidden.");
}

void MainWindow::onToggleTouchTrail(bool checked)
{
    showTouchTrail_ = checked;
    savePortableState();
    sendPreviewConfigCommand();
    statusBar()->showMessage(showTouchTrail_ ? "Touch trail enabled." : "Touch trail hidden.");
}

void MainWindow::onPreviewAudioSettings()
{
    previewAudioSettings_.normalize();
    if (legacyPygamePreviewEnabled_) {
        ensurePreviewSessionStarted();
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Audio Settings");
    dialog.setModal(true);

    auto* rootLayout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();

    auto* bgmRow = new QWidget(&dialog);
    auto* bgmRowLayout = new QHBoxLayout(bgmRow);
    bgmRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* bgmSlider = new QSlider(Qt::Horizontal, bgmRow);
    bgmSlider->setRange(0, 100);
    bgmSlider->setValue(previewAudioSettings_.bgmPercent());
    auto* bgmLabel = new QLabel(QString::number(previewAudioSettings_.bgmPercent()) + "%", bgmRow);
    bgmLabel->setMinimumWidth(44);
    bgmRowLayout->addWidget(bgmSlider, 1);
    bgmRowLayout->addWidget(bgmLabel);
    formLayout->addRow("BGM Volume", bgmRow);

    auto* answerRow = new QWidget(&dialog);
    auto* answerRowLayout = new QHBoxLayout(answerRow);
    answerRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* answerSlider = new QSlider(Qt::Horizontal, answerRow);
    answerSlider->setRange(0, 100);
    answerSlider->setValue(previewAudioSettings_.answerPercent());
    auto* answerLabel = new QLabel(QString::number(previewAudioSettings_.answerPercent()) + "%", answerRow);
    answerLabel->setMinimumWidth(44);
    answerRowLayout->addWidget(answerSlider, 1);
    answerRowLayout->addWidget(answerLabel);
    formLayout->addRow("Answer Volume", answerRow);

    auto* slideRow = new QWidget(&dialog);
    auto* slideRowLayout = new QHBoxLayout(slideRow);
    slideRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* slideSlider = new QSlider(Qt::Horizontal, slideRow);
    slideSlider->setRange(0, 100);
    slideSlider->setValue(previewAudioSettings_.slidePercent());
    auto* slideLabel = new QLabel(QString::number(previewAudioSettings_.slidePercent()) + "%", slideRow);
    slideLabel->setMinimumWidth(44);
    slideRowLayout->addWidget(slideSlider, 1);
    slideRowLayout->addWidget(slideLabel);
    formLayout->addRow("Slide Volume", slideRow);

    auto* breakRow = new QWidget(&dialog);
    auto* breakRowLayout = new QHBoxLayout(breakRow);
    breakRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* breakSlider = new QSlider(Qt::Horizontal, breakRow);
    breakSlider->setRange(0, 100);
    breakSlider->setValue(previewAudioSettings_.breakPercent());
    auto* breakLabel = new QLabel(QString::number(previewAudioSettings_.breakPercent()) + "%", breakRow);
    breakLabel->setMinimumWidth(44);
    breakRowLayout->addWidget(breakSlider, 1);
    breakRowLayout->addWidget(breakLabel);
    formLayout->addRow("Break Volume", breakRow);

    auto* exRow = new QWidget(&dialog);
    auto* exRowLayout = new QHBoxLayout(exRow);
    exRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* exSlider = new QSlider(Qt::Horizontal, exRow);
    exSlider->setRange(0, 100);
    exSlider->setValue(previewAudioSettings_.exPercent());
    auto* exLabel = new QLabel(QString::number(previewAudioSettings_.exPercent()) + "%", exRow);
    exLabel->setMinimumWidth(44);
    exRowLayout->addWidget(exSlider, 1);
    exRowLayout->addWidget(exLabel);
    formLayout->addRow("EX Volume", exRow);

    auto* touchRow = new QWidget(&dialog);
    auto* touchRowLayout = new QHBoxLayout(touchRow);
    touchRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* touchSlider = new QSlider(Qt::Horizontal, touchRow);
    touchSlider->setRange(0, 100);
    touchSlider->setValue(previewAudioSettings_.touchPercent());
    auto* touchLabel = new QLabel(QString::number(previewAudioSettings_.touchPercent()) + "%", touchRow);
    touchLabel->setMinimumWidth(44);
    touchRowLayout->addWidget(touchSlider, 1);
    touchRowLayout->addWidget(touchLabel);
    formLayout->addRow("Touch Volume", touchRow);

    auto* touchholdRow = new QWidget(&dialog);
    auto* touchholdRowLayout = new QHBoxLayout(touchholdRow);
    touchholdRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* touchholdSlider = new QSlider(Qt::Horizontal, touchholdRow);
    touchholdSlider->setRange(0, 100);
    touchholdSlider->setValue(previewAudioSettings_.touchholdPercent());
    auto* touchholdLabel = new QLabel(QString::number(previewAudioSettings_.touchholdPercent()) + "%", touchholdRow);
    touchholdLabel->setMinimumWidth(44);
    touchholdRowLayout->addWidget(touchholdSlider, 1);
    touchholdRowLayout->addWidget(touchholdLabel);
    formLayout->addRow("TouchHold Volume", touchholdRow);

    rootLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(240);
    QString pendingAudition;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    connect(bgmSlider, &QSlider::valueChanged, &dialog, [this, bgmLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBgmPercent(value);
        bgmLabel->setText(QString::number(previewAudioSettings_.bgmPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply(QString());
    });
    connect(answerSlider, &QSlider::valueChanged, &dialog, [this, answerLabel, queueAudioApply](int value) {
        previewAudioSettings_.setAnswerPercent(value);
        answerLabel->setText(QString::number(previewAudioSettings_.answerPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("answer");
    });
    connect(slideSlider, &QSlider::valueChanged, &dialog, [this, slideLabel, queueAudioApply](int value) {
        previewAudioSettings_.setSlidePercent(value);
        slideLabel->setText(QString::number(previewAudioSettings_.slidePercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("slide");
    });
    connect(breakSlider, &QSlider::valueChanged, &dialog, [this, breakLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBreakPercent(value);
        breakLabel->setText(QString::number(previewAudioSettings_.breakPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("break");
    });
    connect(exSlider, &QSlider::valueChanged, &dialog, [this, exLabel, queueAudioApply](int value) {
        previewAudioSettings_.setExPercent(value);
        exLabel->setText(QString::number(previewAudioSettings_.exPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("ex");
    });
    connect(touchSlider, &QSlider::valueChanged, &dialog, [this, touchLabel, queueAudioApply](int value) {
        previewAudioSettings_.setTouchPercent(value);
        touchLabel->setText(QString::number(previewAudioSettings_.touchPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("touch");
    });
    connect(touchholdSlider, &QSlider::valueChanged, &dialog, [this, touchholdLabel, queueAudioApply](int value) {
        previewAudioSettings_.setTouchholdPercent(value);
        touchholdLabel->setText(QString::number(previewAudioSettings_.touchholdPercent()) + "%");
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->applyLevels(previewAudioSettings_);
        }
        savePortableState();
        queueAudioApply("touchhold");
    });

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, bgmSlider, answerSlider, slideSlider, breakSlider, exSlider, touchSlider, touchholdSlider, &pendingAudition]() {
        if (bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || touchholdSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        sendPreviewConfigCommand(handledLocally ? QString() : pendingAudition);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        sendPreviewConfigCommand(handledLocally ? QString() : pendingAudition);
        pendingAudition.clear();
    });

    dialog.exec();
}

void MainWindow::onPreviewDisplaySettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Display Settings");
    dialog.setModal(true);

    auto* rootLayout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();

    auto* brightnessRow = new QWidget(&dialog);
    auto* brightnessRowLayout = new QHBoxLayout(brightnessRow);
    brightnessRowLayout->setContentsMargins(0, 0, 0, 0);
    auto* brightnessSlider = new QSlider(Qt::Horizontal, brightnessRow);
    brightnessSlider->setRange(0, 100);
    brightnessSlider->setValue(qRound(previewBackgroundBrightness_ * 100.0));
    auto* brightnessLabel = new QLabel(QString::number(brightnessSlider->value()) + "%", brightnessRow);
    brightnessLabel->setMinimumWidth(44);
    brightnessRowLayout->addWidget(brightnessSlider, 1);
    brightnessRowLayout->addWidget(brightnessLabel);
    formLayout->addRow("Background / PV Brightness", brightnessRow);

    auto* debugCheck = new QCheckBox("Show preview debug info", &dialog);
    debugCheck->setChecked(previewShowDebugInfo_);
    formLayout->addRow(QString(), debugCheck);

    rootLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    connect(brightnessSlider, &QSlider::valueChanged, &dialog, [this, brightnessLabel](int value) {
        previewBackgroundBrightness_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        brightnessLabel->setText(QString::number(value) + "%");
        if (previewMediaController_ != nullptr) {
            previewMediaController_->setBackgroundBrightness(previewBackgroundBrightness_);
        } else if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightness(previewBackgroundBrightness_);
        }
        savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowDebugInfo_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        }
        savePortableState();
    });

    dialog.exec();
}

bool MainWindow::ensurePreviewSessionStarted()
{
    if (previewProcess_ != nullptr && previewProcess_->state() == QProcess::Running) {
        return true;
    }

    const QString scriptPath = resolvePreviewSessionScriptPath();
    if (scriptPath.isEmpty()) {
        appendOutput("preview/session-start", "script path not found");
        QMessageBox::warning(
            this,
            "Preview Session",
            "Cannot locate MaiMuriDX/api_preview_session.py.\n"
            "Set MAICODE_PREVIEW_SESSION_SCRIPT or run from repo workspace."
        );
        return false;
    }

    if (previewProcess_ != nullptr) {
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
    }

    previewProcess_ = new QProcess(this);
    previewProcess_->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    previewProcess_->setProcessEnvironment(pythonProcessEnvironment());
    previewProcess_->setProgram("python");
    previewProcess_->setArguments(QStringList{scriptPath});
    appendOutput(
        "preview/session-start",
        QString("program=python script=%1 cwd=%2")
            .arg(scriptPath, previewProcess_->workingDirectory())
    );
#ifdef Q_OS_WIN
    previewProcess_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    previewStdoutBuffer_.clear();
    previewStderrBuffer_.clear();
    connect(previewProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        onPreviewProcessFinished(exitCode);
    });
    connect(previewProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStdoutBuffer_ += decodeProcessText(previewProcess_->readAllStandardOutput());
        int lineBreak = previewStdoutBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStdoutBuffer_.left(lineBreak).trimmed();
            previewStdoutBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                if (line.contains("[session] ready")) {
                    previewArrangeRetryCount_ = 0;
                    QTimer::singleShot(40, this, &MainWindow::arrangeWithPreviewWindow);
                    sendPreviewConfigCommand();
                }
                if (!handlePreviewSessionLine(line)) {
                    appendOutput("preview/session", line);
                }
            }
            lineBreak = previewStdoutBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::readyReadStandardError, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStderrBuffer_ += decodeProcessText(previewProcess_->readAllStandardError());
        int lineBreak = previewStderrBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStderrBuffer_.left(lineBreak).trimmed();
            previewStderrBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                appendOutput("preview/session-stderr", line);
            }
            lineBreak = previewStderrBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendOutput(
            "preview/session-error",
            QString("process error code=%1 message=%2")
                .arg(static_cast<int>(error))
                .arg(previewProcess_ != nullptr ? previewProcess_->errorString() : QString("n/a"))
        );
    });

    previewProcess_->start();
    if (!previewProcess_->waitForStarted(3000)) {
        appendOutput(
            "preview/session-start-failed",
            QString("failed to start python preview session. error=%1")
                .arg(previewProcess_->errorString())
        );
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
        return false;
    }

    appendOutput("preview/session-start", QString("started pid=%1").arg(previewProcess_->processId()));
    statusBar()->showMessage("Preview session started.");
    return true;
}

void MainWindow::stopPreviewSession()
{
    if (previewProcess_ == nullptr) {
        return;
    }
    QProcess* process = previewProcess_;
    previewProcess_ = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::Running) {
        const QJsonObject quitCmd{{"cmd", "quit"}};
        QByteArray payload = QJsonDocument(quitCmd).toJson(QJsonDocument::Compact);
        payload.append('\n');
        process->write(payload);
        process->waitForBytesWritten(500);
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

bool MainWindow::sendPreviewCommand(const QString& mode, int cursorLine, int cursorCol, const QString& trackPath)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "preview"},
        {"mode", mode},
        {"track", trackPath},
        {"chart", editorText()},
        {"chart_name", currentFilePath_.isEmpty() ? QString("Untitled") : QFileInfo(currentFilePath_).fileName()},
        {"chart_path", currentFilePath_},
        {"volume", previewAudioSettings_.bgmVolume},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"render_profile", "studio"},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    if (mode == "cursor") {
        cmd.insert("cursor_line", cursorLine);
        cmd.insert("cursor_col", cursorCol);
    }

    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview command to session");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewPrepareCommand()
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    if (currentFilePath_.isEmpty()) {
        return false;
    }
    QJsonObject cmd{
        {"cmd", "prepare"},
        {"chart_path", currentFilePath_},
        {"render_profile", "studio"},
        {"background_brightness", previewBackgroundBrightness_},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview prepare command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewConfigCommand(const QString& audition)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "config"},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"background_brightness", previewBackgroundBrightness_},
    };
    if (!audition.isEmpty()) {
        cmd.insert("audition", audition);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview config command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::handlePreviewSessionLine(const QString& line)
{
    QString jsonText = line.trimmed();
    const int jsonStart = jsonText.indexOf('{');
    const int jsonEnd = jsonText.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart) {
        jsonText = jsonText.mid(jsonStart, jsonEnd - jsonStart + 1);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject payload = doc.object();
            if (payload.value("event").toString() == "playhead_limit") {
                if (timelineView_ != nullptr) {
                    const QJsonValue secondValue = payload.value("second");
                    if (secondValue.isDouble()) {
                        timelineView_->setPlayheadUpperLimitSeconds(secondValue.toDouble());
                    } else {
                        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
                    }
                }
                return true;
            }
        }
    }

    double second = 0.0;
    const PreviewIntegration::PlayheadParseResult parsed = PreviewIntegration::parsePlayheadEvent(line, &second);
    if (parsed == PreviewIntegration::PlayheadParseResult::NotPlayheadEvent) {
        return false;
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && timelineView_ != nullptr) {
        timelineView_->setPlayheadSeconds(second, true);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(second);
    }
    return true;
}

void MainWindow::startPreviewProcess(const QString& mode, int cursorLine, int cursorCol)
{
    if (!runValidateSimai()) {
        appendOutput("preview/blocked", "validation failed; preview canceled");
        statusBar()->showMessage("Preview canceled: fix validation errors first.");
        return;
    }

    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
    }
    QString trackPath = resolveDefaultTrackPath();
    if (trackPath.isEmpty()) {
        trackPath = QFileDialog::getOpenFileName(
            this,
            "Select Preview Track",
            QString(),
            "Audio (*.mp3 *.wav *.ogg);;All Files (*.*)"
        );
    }
    if (trackPath.isEmpty()) {
        return;
    }
    lastTrackPath_ = trackPath;

    if (!ensurePreviewSessionStarted()) {
        return;
    }
    if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
        appendOutput("preview/session", "retrying by restarting session process");
        stopPreviewSession();
        if (!ensurePreviewSessionStarted()) {
            return;
        }
        if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
            appendOutput("preview/session", "failed to send preview command after restart");
            return;
        }
    }

    statusBar()->showMessage(
        QString("Preview(%1) sent to resident session: %2").arg(mode).arg(QFileInfo(trackPath).fileName())
    );
    QTimer::singleShot(80, this, &MainWindow::arrangeWithPreviewWindow);
}

void MainWindow::onPreviewProcessFinished(int exitCode)
{
    if (previewProcess_ == nullptr) {
        return;
    }
    const QString restOut = decodeProcessText(previewProcess_->readAllStandardOutput()).trimmed();
    const QString restErr = decodeProcessText(previewProcess_->readAllStandardError()).trimmed();
    if (!restOut.isEmpty()) {
        appendOutput("preview/session", restOut);
    }
    if (!restErr.isEmpty()) {
        appendOutput("preview/session-stderr", restErr);
    }
    appendOutput("preview/session-exit", QString("exit_code=%1").arg(exitCode));
    statusBar()->showMessage("Preview session exited.");
    previewProcess_->deleteLater();
    previewProcess_ = nullptr;
}

void MainWindow::appendOutput(const QString& title, const QString& payload)
{
    if (outputView_ == nullptr) {
        return;
    }
    outputView_->appendPlainText(timestampLine(title));
    outputView_->appendPlainText(payload);
    outputView_->appendPlainText(QString());
}

void MainWindow::onErrorItemActivated(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const int line = item->data(Qt::UserRole).toInt();
    const int col = item->data(Qt::UserRole + 1).toInt();
    jumpToLocation(line, col);
}

bool MainWindow::runValidateSimai()
{
    clearValidationErrors();
    clearValidationDecorations();

    const SimaiNativeParseResult nativeResult = SimaiNativeParser::validateSyntax(editorText());
    QString payload;
    payload += QString("note_count=%1\nsyntax_error_count=%2")
        .arg(nativeResult.noteMarkers.size())
        .arg(nativeResult.errors.size());
    appendOutput("validate", payload);

    for (const SimaiNativeMessage& err : nativeResult.errors) {
        addValidationError(err.line, err.col, err.message);
        addValidationDecoration(err.line, err.col, err.message);
    }
    if (!nativeResult.errors.isEmpty()) {
        onErrorItemActivated(errorList_->item(0));
    }

    if (nativeResult.errors.isEmpty()) {
        statusBar()->showMessage("Validate Simai passed.");
        return true;
    } else {
        statusBar()->showMessage(QString("Validate Simai failed: %1 syntax error(s).").arg(nativeResult.errors.size()));
    }
    return false;
}

void MainWindow::onValidateSimai()
{
    (void)runValidateSimai();
}
