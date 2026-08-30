#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QVariantMap>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTextStream>
#include <QtTest/QTest>

#include <memory>

#ifndef MIACODE_QML_SPEC_IMPORT_ROOT
#error "MIACODE_QML_SPEC_IMPORT_ROOT must be defined"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

std::unique_ptr<QObject> createHarness(QQmlEngine& engine, QTextStream& err)
{
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Controls
        import MiaCode.UI

ApplicationWindow {
    visible: true
    width: 900
    height: 700

    QtObject {
        id: fakeRequests
        objectName: "fakeUiRequests"
        signal fileRequested(string requestId, var request)
        signal noticeRequested(string requestId, var notice)
        property string resolvedId: ""
        property string resolvedPath: ""
        property string cancelledId: ""
        property string noticeResolvedId: ""
        property bool noticeActionChosen: false
        function submitFileResult(requestId, fileUrl) {
            resolvedId = requestId
            resolvedPath = fileUrl.toString()
        }
        function cancelFileRequest(requestId) { cancelledId = requestId }
        function submitNoticeResult(requestId, actionChosen) {
            noticeResolvedId = requestId
            noticeActionChosen = actionChosen
        }
    }

    QtObject {
        id: session
        objectName: "fakeExportSession"
        property var uiRequests: fakeRequests
        property var difficulties: []
        property int selectedDifficultyId: 5
        property string activeTab: "export"
        property string settingsTab: "intro"
        property string unavailableReason: ""
        property bool exportRunning: false
        property string outputPath: "out.mp4"
        property int resolutionIndex: 0
        property var resolutionOptions: [{ label: "1024x1024", width: 1024, height: 1024 }]
        property int fps: 60
        property var fpsOptions: [30, 60, 120]
        property int audioBitrateKbps: 192
        property var audioBitrateOptions: [128, 192, 320]
        property int presetIndex: 1
        property var presetOptions: ["Fast", "High quality"]
        property int sizePresetIndex: 0
        property var sizePresetOptions: ["Standard"]
        property real backgroundBrightnessOuter: 0.5
        property real backgroundBrightnessInner: 0.2
        property real layoutSquareScale: 0.95
        property int backgroundScaleModeIndex: 0
        property var backgroundScaleModeOptions: ["Fill"]
        property bool smoothBrightness: true
        property bool showTimestamp: true
        property bool showObjectStatsHud: false
        property bool showChartInfoHud: false
        property bool fixHudTextLayout: false
        property bool clockCountEnabled: false
        property real tapFlowSpeed: 7.5
        property real touchFlowSpeed: 7.5
        property var skinOptions: []
        property int skinIndex: -1
        property var judgeEffectOptions: []
        property int judgeEffectIndex: 0
        property var outlineOptions: []
        property int outlineIndex: 1
        property bool introEnabled: true
        property int introBackgroundModeIndex: 0
        property string introCustomBackgroundPath: ""
        property bool introBlurBackground: true
        property int introModeIndex: 0
        property bool introCardShadow: false
        property bool introLevelTextRender: false
        property var introSoundOptions: [
            { label: "Default intro sound", fileName: "" },
            { label: "custom.wav", fileName: "custom.wav" }
        ]
        property int introSoundIndex: 1
        property string introSoundFileName: "custom.wav"
        property real introSoundVolume: 1.25
        property string introSoundLabel: "Intro sound"
        property string introSoundVolumeLabel: "Intro sound volume"
        property string introSoundImportLabel: "Import"
        property real exportStartSeconds: 60.0
        property real exportEndSeconds: 180.0
        property real contentDurationSeconds: 240.0
        property bool fullRangeExport: true
        property var chartDirectories: []
        property var batchDifficultyChecks: []
        property string batchOutputDirectory: ""
        property int importRequests: 0

        function selectDifficulty(id) {}
        function browseBatchOutputDirectory() {}
        function addChartDirectories() {}
        function clearChartDirectories() {}
        function removeChartDirectory(index) {}
        function setBatchDifficultyChecked(id, checked) {}
        function browseOutputPath() {}
        function openSkinDirectory() {}
        function openJudgeLineDirectory() {}
        function browseIntroBackground() {}
        function importIntroSound() { importRequests += 1 }
        function setExportStartToCurrentPreview() {}
        function setExportEndToCurrentPreview() {}
        function setExportStartText(text) {
            const value = Number(text)
            if (!isNaN(value)) exportStartSeconds = value
            return exportStartSeconds.toFixed(3)
        }
        function setExportEndText(text) {
            const value = Number(text)
            if (!isNaN(value)) exportEndSeconds = value
            return exportEndSeconds.toFixed(3)
        }
        function startExport() {}
        function cancelExport() {}
    }

    QtObject {
        id: previewSession
        objectName: "fakePreviewSession"
        property real positionSeconds: 120.0
        property bool playing: true
        property var scrubCalls: []
        function beginScrub() {
            playing = false
            scrubCalls = scrubCalls.concat(["begin"])
        }
        function updateScrub(second) {
            positionSeconds = second
            scrubCalls = scrubCalls.concat(["update"])
        }
        function endScrub(second) {
            positionSeconds = second
            scrubCalls = scrubCalls.concat(["end"])
        }
    }

    QtObject {
        id: pages
        property var exportSession: session
    }

    ExportVideoPage {
        anchors.fill: parent
        pages: pages
        previewSession: previewSession
    }

    // The shell hosts one of these for every page; instantiate it directly so
    // the request loop is verified against the real component rather than
    // against whichever page happens to embed it.
    UiRequestHost {
        objectName: "exportUiRequestHost"
        requests: fakeRequests
    }
}
)QML", QUrl(QStringLiteral("qrc:/QmlExportVideoPageSpec.qml")));
    if (component.status() == QQmlComponent::Error) {
        for (const QQmlError& error : component.errors()) {
            err << "FAIL: export page harness load: " << error.toString() << Qt::endl;
        }
        return {};
    }
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        for (const QQmlError& error : component.errors()) {
            err << "FAIL: export page harness create: " << error.toString() << Qt::endl;
        }
    }
    return object;
}

std::unique_ptr<QObject> createChoiceSequenceHarness(QQmlEngine& engine, QTextStream& err)
{
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import MiaCode.UI

ApplicationWindow {
    visible: true
    width: 480
    height: 320

    QtObject {
        id: fakeRequests
        objectName: "choiceSequenceRequests"
        signal choiceRequested(string requestId, var request)
        property string resolvedId: ""
        property string resolvedAnswer: ""

        function submitChoiceResult(requestId, choiceId) {
            resolvedId = requestId
            resolvedAnswer = choiceId
            if (requestId === "choice-1") {
                choiceRequested("choice-2", {
                    title: "Second unsaved difficulty",
                    text: "Discard this difficulty?",
                    choices: [{ id: "discard", label: "Discard", role: "destructive" }],
                    dismissChoiceId: "cancel"
                })
            }
        }
    }

    UiRequestHost {
        objectName: "choiceSequenceHost"
        requests: fakeRequests
    }
}
)QML", QUrl(QStringLiteral("qrc:/ChoiceSequenceHarness.qml")));
    if (component.status() == QQmlComponent::Error) {
        for (const QQmlError& error : component.errors()) {
            err << "FAIL: choice sequence harness load: " << error.toString() << Qt::endl;
        }
        return {};
    }
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        for (const QQmlError& error : component.errors()) {
            err << "FAIL: choice sequence harness create: " << error.toString() << Qt::endl;
        }
    }
    return object;
}

bool verifyRealExportPageControls(QTextStream& err)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    const std::unique_ptr<QObject> root = createHarness(engine, err);
    if (!root) {
        return false;
    }
    QCoreApplication::processEvents();

    QObject* session = root->findChild<QObject*>(QStringLiteral("fakeExportSession"));
    QObject* combo = root->findChild<QObject*>(QStringLiteral("introSoundCombo"));
    QObject* importButton = root->findChild<QObject*>(QStringLiteral("introSoundImportButton"));
    QObject* volumeSlider = root->findChild<QObject*>(QStringLiteral("introSoundVolumeSlider"));
    bool ok = require(
        session != nullptr && combo != nullptr && importButton != nullptr && volumeSlider != nullptr,
        QStringLiteral("the real ExportVideoPage creates all three intro-sound controls"),
        err);
    if (!ok) {
        return false;
    }

    ok &= require(
        combo->property("currentIndex").toInt() == 1
            && qAbs(volumeSlider->property("value").toDouble() - 125.0) <= 1e-9,
        QStringLiteral("the combo and slider bind to the session filename index and 0..200 percent volume"),
        err);
    ok &= require(
        combo->property("enabled").toBool()
            && importButton->property("enabled").toBool()
            && volumeSlider->property("enabled").toBool(),
        QStringLiteral("intro-sound controls are enabled for an enabled full-range intro"),
        err);
    ok &= require(
        combo->property("focusPolicy").toInt() == Qt::StrongFocus
            && importButton->property("focusPolicy").toInt() == Qt::StrongFocus
            && volumeSlider->property("focusPolicy").toInt() == Qt::StrongFocus,
        QStringLiteral("every intro-sound control is keyboard focusable"),
        err);

    combo->setProperty("currentIndex", 0);
    QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0));
    ok &= require(
        session->property("introSoundIndex").toInt() == 0,
        QStringLiteral("activating the real combo writes the selected option to the session"),
        err);

    QMetaObject::invokeMethod(importButton, "clicked");
    ok &= require(
        session->property("importRequests").toInt() == 1,
        QStringLiteral("the real import button invokes the session import action"),
        err);

    volumeSlider->setProperty("value", 175.0);
    QMetaObject::invokeMethod(volumeSlider, "moved");
    ok &= require(
        qAbs(session->property("introSoundVolume").toDouble() - 1.75) <= 1e-9,
        QStringLiteral("moving the real slider writes the independent 0..2 volume multiplier"),
        err);

    session->setProperty("introEnabled", false);
    QCoreApplication::processEvents();
    ok &= require(
        !combo->property("enabled").toBool()
            && !importButton->property("enabled").toBool()
            && !volumeSlider->property("enabled").toBool(),
        QStringLiteral("turning the intro off disables its sound controls"),
        err);

    session->setProperty("introEnabled", true);
    session->setProperty("fullRangeExport", false);
    QCoreApplication::processEvents();
    ok &= require(
        !combo->property("enabled").toBool(),
        QStringLiteral("a partial single export disables intro sound settings"),
        err);

    session->setProperty("activeTab", QStringLiteral("batch"));
    QCoreApplication::processEvents();
    ok &= require(
        combo->property("enabled").toBool()
            && importButton->property("enabled").toBool()
            && volumeSlider->property("enabled").toBool(),
        QStringLiteral("batch export keeps intro sound settings available regardless of the single range"),
        err);
    return ok;
}

bool verifyVisualRangeSelectorDragsTheSharedPreview(QTextStream& err)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    const std::unique_ptr<QObject> root = createHarness(engine, err);
    auto* window = root ? qobject_cast<QQuickWindow*>(root.get()) : nullptr;
    if (!require(window != nullptr,
                 QStringLiteral("the export range harness creates a real QML window"), err)) {
        return false;
    }
    window->show();
    Q_UNUSED(QTest::qWaitForWindowExposed(window));
    QCoreApplication::processEvents();

    QObject* session = root->findChild<QObject*>(QStringLiteral("fakeExportSession"));
    QObject* preview = root->findChild<QObject*>(QStringLiteral("fakePreviewSession"));
    if (!require(session != nullptr && preview != nullptr,
                 QStringLiteral("the export range harness exposes its export and preview sessions"), err)) {
        return false;
    }
    session->setProperty("settingsTab", QStringLiteral("range"));
    QCoreApplication::processEvents();

    auto* selector = root->findChild<QQuickItem*>(QStringLiteral("exportRangeSelector"));
    auto* lane = root->findChild<QQuickItem*>(QStringLiteral("exportRangeLane"));
    auto* startHandle = root->findChild<QQuickItem*>(QStringLiteral("exportRangeStartHandle"));
    auto* endHandle = root->findChild<QQuickItem*>(QStringLiteral("exportRangeEndHandle"));
    QObject* startField = root->findChild<QObject*>(QStringLiteral("exportRangeStartField"));
    if (!require(selector != nullptr && lane != nullptr && startHandle != nullptr && endHandle != nullptr
                     && startField != nullptr,
                 QStringLiteral("the real export page creates both visual range handles and the numeric start field"), err)) {
        return false;
    }

    const QPoint pressPoint = startHandle->mapToScene(
        QPointF(startHandle->width() * 0.5, startHandle->height() * 0.5)).toPoint();
    const QPoint dragPoint = lane->mapToScene(
        QPointF(lane->width() * 0.4, lane->height() * 0.5)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pressPoint);
    QTest::mouseMove(window, dragPoint, 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragPoint);
    QCoreApplication::processEvents();

    const QStringList scrubCalls = preview->property("scrubCalls").toStringList();
    bool ok = require(session->property("exportStartSeconds").toDouble() > 90.0
                          && session->property("exportStartSeconds").toDouble()
                              < session->property("exportEndSeconds").toDouble(),
                      QStringLiteral("dragging the start handle changes only the clamped export start"), err);
    ok &= require(!preview->property("playing").toBool()
                      && scrubCalls.size() >= 3
                      && scrubCalls.first() == QStringLiteral("begin")
                      && scrubCalls.last() == QStringLiteral("end")
                      && qAbs(preview->property("positionSeconds").toDouble()
                              - session->property("exportStartSeconds").toDouble()) < 0.001,
                  QStringLiteral("a range drag pauses and scrubs the same preview time source"), err);

    preview->setProperty("scrubCalls", QStringList());
    const QPoint endPressPoint = endHandle->mapToScene(
        QPointF(endHandle->width() * 0.5, endHandle->height() * 0.5)).toPoint();
    const QPoint endDragPoint = lane->mapToScene(
        QPointF(lane->width() * 0.7, lane->height() * 0.5)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, endPressPoint);
    QTest::mouseMove(window, endDragPoint, 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, endDragPoint);
    QCoreApplication::processEvents();

    const QStringList endScrubCalls = preview->property("scrubCalls").toStringList();
    ok &= require(session->property("exportEndSeconds").toDouble()
                          > session->property("exportStartSeconds").toDouble()
                      && session->property("exportEndSeconds").toDouble() < 180.0,
                  QStringLiteral("dragging the end handle changes only the clamped export end"), err);
    ok &= require(endScrubCalls.size() >= 3
                      && endScrubCalls.first() == QStringLiteral("begin")
                      && endScrubCalls.last() == QStringLiteral("end")
                      && qAbs(preview->property("positionSeconds").toDouble()
                              - session->property("exportEndSeconds").toDouble()) < 0.001,
                  QStringLiteral("the end handle uses the same paused preview scrub lifecycle"), err);

    startField->setProperty("text", QStringLiteral("30"));
    QMetaObject::invokeMethod(startField, "editingFinished");
    QCoreApplication::processEvents();
    ok &= require(qAbs(session->property("exportStartSeconds").toDouble() - 30.0) < 0.001,
                  QStringLiteral("the retained numeric start field updates the visual range source of truth"), err);
    return ok;
}

// The page must own the whole pick/notice loop in QML: a service request has to
// configure and open a real Qt Quick dialog, and the dialog's answer has to go
// back to the service. Nothing here may reach a Widgets dialog.
bool verifyRequestHostLoop(QTextStream& err)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    const std::unique_ptr<QObject> root = createHarness(engine, err);
    if (!root) {
        return false;
    }
    QCoreApplication::processEvents();

    QObject* requests = root->findChild<QObject*>(QStringLiteral("fakeUiRequests"));
    QObject* host = root->findChild<QObject*>(QStringLiteral("exportUiRequestHost"));
    QObject* fileDialog = root->findChild<QObject*>(QStringLiteral("uiRequestFileDialog"));
    QObject* folderDialog = root->findChild<QObject*>(QStringLiteral("uiRequestFolderDialog"));
    QObject* noticeDialog = root->findChild<QObject*>(QStringLiteral("uiRequestNoticeDialog"));
    bool ok = require(requests != nullptr && host != nullptr && fileDialog != nullptr
                          && folderDialog != nullptr && noticeDialog != nullptr,
                      QStringLiteral("the real export page hosts file, folder and notice dialogs"),
                      err);
    if (!ok) {
        return false;
    }

    QVariantMap saveRequest;
    saveRequest.insert(QStringLiteral("title"), QStringLiteral("Choose output"));
    saveRequest.insert(QStringLiteral("startPath"), QStringLiteral("/tmp/out.mp4"));
    saveRequest.insert(QStringLiteral("nameFilters"), QStringList{QStringLiteral("MP4 (*.mp4)")});
    saveRequest.insert(QStringLiteral("saveMode"), true);
    saveRequest.insert(QStringLiteral("selectFolder"), false);
    QMetaObject::invokeMethod(requests, "fileRequested", Q_ARG(QString, QStringLiteral("file-1")),
                              Q_ARG(QVariant, QVariant(saveRequest)));
    QCoreApplication::processEvents();
    ok &= require(host->property("activeFileRequestId").toString() == QStringLiteral("file-1")
                      && fileDialog->property("title").toString() == QStringLiteral("Choose output")
                      && fileDialog->property("nameFilters").toStringList()
                          == QStringList{QStringLiteral("MP4 (*.mp4)")},
                  QStringLiteral("a save request configures the real file dialog and records its id"),
                  err);

    QMetaObject::invokeMethod(fileDialog, "accepted");
    QCoreApplication::processEvents();
    ok &= require(requests->property("resolvedId").toString() == QStringLiteral("file-1")
                      && host->property("activeFileRequestId").toString().isEmpty(),
                  QStringLiteral("accepting the dialog returns the pick to the service and clears the id"),
                  err);

    QVariantMap folderRequest;
    folderRequest.insert(QStringLiteral("title"), QStringLiteral("Choose folder"));
    folderRequest.insert(QStringLiteral("startPath"), QString());
    folderRequest.insert(QStringLiteral("nameFilters"), QStringList());
    folderRequest.insert(QStringLiteral("saveMode"), false);
    folderRequest.insert(QStringLiteral("selectFolder"), true);
    QMetaObject::invokeMethod(requests, "fileRequested", Q_ARG(QString, QStringLiteral("file-2")),
                              Q_ARG(QVariant, QVariant(folderRequest)));
    QCoreApplication::processEvents();
    ok &= require(host->property("activeFolderRequestId").toString() == QStringLiteral("file-2")
                      && folderDialog->property("title").toString() == QStringLiteral("Choose folder"),
                  QStringLiteral("a folder request routes to the folder dialog, not the file dialog"),
                  err);

    QMetaObject::invokeMethod(folderDialog, "rejected");
    QCoreApplication::processEvents();
    ok &= require(requests->property("cancelledId").toString() == QStringLiteral("file-2")
                      && host->property("activeFolderRequestId").toString().isEmpty(),
                  QStringLiteral("dismissing the folder dialog cancels the request instead of dropping it"),
                  err);

    QVariantMap notice;
    notice.insert(QStringLiteral("severity"), QStringLiteral("warning"));
    notice.insert(QStringLiteral("title"), QStringLiteral("Batch export"));
    notice.insert(QStringLiteral("text"), QStringLiteral("2 of 3 exported"));
    notice.insert(QStringLiteral("details"), QStringLiteral("failed: chart-c"));
    notice.insert(QStringLiteral("actionLabel"), QString());
    QMetaObject::invokeMethod(requests, "noticeRequested",
                              Q_ARG(QString, QStringLiteral("notice-1")),
                              Q_ARG(QVariant, QVariant(notice)));
    QCoreApplication::processEvents();
    ok &= require(noticeDialog->property("title").toString() == QStringLiteral("Batch export")
                      && noticeDialog->property("text").toString()
                          == QStringLiteral("2 of 3 exported")
                      && noticeDialog->property("informativeText").toString()
                          == QStringLiteral("failed: chart-c"),
                  QStringLiteral("a notice renders through the QML message dialog with its detail block"),
                  err);

    // An actionable notice must offer the extra button and report which one the
    // viewer picked, otherwise "open the folder I just wrote" silently no-ops.
    QVariantMap actionable = notice;
    actionable.insert(QStringLiteral("actionLabel"), QStringLiteral("Open folder"));
    QMetaObject::invokeMethod(requests, "noticeRequested",
                              Q_ARG(QString, QStringLiteral("notice-2")),
                              Q_ARG(QVariant, QVariant(actionable)));
    QCoreApplication::processEvents();
    ok &= require(noticeDialog->property("actionLabel").toString()
                      == QStringLiteral("Open folder"),
                  QStringLiteral("an actionable notice carries its action into the dialog"), err);

    QMetaObject::invokeMethod(noticeDialog, "rejected");
    QCoreApplication::processEvents();
    ok &= require(requests->property("noticeResolvedId").toString() == QStringLiteral("notice-2")
                      && !requests->property("noticeActionChosen").toBool(),
                  QStringLiteral("dismissing an actionable notice resolves it as not-chosen"), err);
    return ok;
}

bool verifySynchronousChoiceSequenceKeepsTheNextDialogOpen(QTextStream& err)
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(MIACODE_QML_SPEC_IMPORT_ROOT));
    const std::unique_ptr<QObject> root = createChoiceSequenceHarness(engine, err);
    if (!root) {
        return false;
    }
    QCoreApplication::processEvents();

    QObject* requests = root->findChild<QObject*>(QStringLiteral("choiceSequenceRequests"));
    QObject* host = root->findChild<QObject*>(QStringLiteral("choiceSequenceHost"));
    QObject* choiceDialog = root->findChild<QObject*>(QStringLiteral("uiRequestChoiceDialog"));
    bool ok = require(requests != nullptr && host != nullptr && choiceDialog != nullptr,
                      QStringLiteral("the choice-sequence harness creates the real request host and dialog"),
                      err);
    if (!ok) {
        return false;
    }

    QVariantMap firstChoice;
    firstChoice.insert(QStringLiteral("title"), QStringLiteral("First unsaved difficulty"));
    firstChoice.insert(QStringLiteral("text"), QStringLiteral("Discard this difficulty?"));
    firstChoice.insert(QStringLiteral("choices"), QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("discard")},
                    {QStringLiteral("label"), QStringLiteral("Discard")},
                    {QStringLiteral("role"), QStringLiteral("destructive")}},
    });
    firstChoice.insert(QStringLiteral("dismissChoiceId"), QStringLiteral("cancel"));
    QMetaObject::invokeMethod(requests, "choiceRequested",
                              Q_ARG(QString, QStringLiteral("choice-1")),
                              Q_ARG(QVariant, QVariant(firstChoice)));
    QCoreApplication::processEvents();
    ok &= require(host->property("activeChoiceId").toString() == QStringLiteral("choice-1")
                      && choiceDialog->property("visible").toBool(),
                  QStringLiteral("the first choice request opens the real choice dialog"), err);

    QMetaObject::invokeMethod(choiceDialog, "resolve",
                              Q_ARG(QVariant, QVariant(QStringLiteral("discard"))));
    QCoreApplication::processEvents();
    ok &= require(requests->property("resolvedId").toString() == QStringLiteral("choice-1")
                      && requests->property("resolvedAnswer").toString()
                          == QStringLiteral("discard")
                      && host->property("activeChoiceId").toString()
                          == QStringLiteral("choice-2")
                      && choiceDialog->property("visible").toBool()
                      && choiceDialog->property("title").toString()
                          == QStringLiteral("Second unsaved difficulty"),
                  QStringLiteral("a synchronous next choice remains open after the previous choice closes"),
                  err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef Q_OS_WIN
    // Qt 6.8's offscreen/minimal QPA initialization can hang before main-loop
    // entry on Windows. This harness creates no QQuickWindow, so the native
    // platform plugin remains headless while allowing the real controls to load.
    qputenv("QT_QPA_PLATFORM", "windows");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyRealExportPageControls(err) && verifyVisualRangeSelectorDragsTheSharedPreview(err)
        && verifyRequestHostLoop(err)
        && verifySynchronousChoiceSequenceKeepsTheNextDialogOpen(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_export_video_page_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
