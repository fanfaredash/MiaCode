#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QVariantMap>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QTextStream>

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
import MiaCode.UI

Item {
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
        property real exportStartSeconds: 0.0
        property real exportEndSeconds: 1.0
        property real contentDurationSeconds: 1.0
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
        function setExportStartText(text) { return text }
        function setExportEndText(text) { return text }
        function startExport() {}
        function cancelExport() {}
    }

    QtObject {
        id: pages
        property var exportSession: session
    }

    ExportVideoPage {
        anchors.fill: parent
        pages: pages
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
    const bool ok = verifyRealExportPageControls(err) && verifyRequestHostLoop(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_export_video_page_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
