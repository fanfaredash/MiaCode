#include <QDir>
#include <QFile>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    return condition;
}

QString readSource(const QString& relativePath)
{
    QFile file(QDir(QStringLiteral(MIACODE_SOURCE_ROOT)).filePath(relativePath));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool verifyIconButton(QTextStream& out)
{
    const QString source = readSource(QStringLiteral("src/app/ui/components/IconButton.qml"));
    bool ok = require(!source.isEmpty(), QStringLiteral("IconButton.qml is readable"), out);
    const int disabled = source.indexOf(QStringLiteral("!root.enabled"));
    const int active = source.indexOf(QStringLiteral("root.active || root.checked || root.hovered || root.visualFocus"));
    ok &= require(disabled >= 0 && active > disabled,
                  QStringLiteral("the default glyph color gives disabled state priority"), out);
    ok &= require(source.contains(QStringLiteral("property int glyphPixelSize: -1"))
                      && source.contains(QStringLiteral("property var glyphStateColors: null"))
                      && source.contains(QStringLiteral("font.pixelSize: root.glyphPixelSize > 0")),
                  QStringLiteral("glyph sizing and state colors remain explicit opt-in properties"), out);
    ok &= require(source.count(QStringLiteral("glyphPixelSize")) == 3,
                  QStringLiteral("glyphPixelSize is used only by the glyph text"), out);
    ok &= require(source.contains(QStringLiteral("root.glyphStateColors.disabled"))
                      && source.contains(QStringLiteral("root.glyphStateColors.pressed"))
                      && source.contains(QStringLiteral("root.glyphStateColors.hovered"))
                      && source.contains(QStringLiteral("root.glyphStateColors.focused"))
                      && source.contains(QStringLiteral("root.glyphStateColors.normal")),
                  QStringLiteral("comic state colors cover disabled, pressed, hovered, focused, and normal"), out);
    return ok;
}

bool verifyProgressOverlay(QTextStream& out)
{
    const QString source = readSource(QStringLiteral("src/app/ui/components/JobProgressOverlay.qml"));
    bool ok = require(!source.isEmpty(), QStringLiteral("JobProgressOverlay.qml is readable"), out);
    ok &= require(source.contains(QStringLiteral("root.progress.chartExport"))
                      && source.contains(QStringLiteral("!root.progress.chartExport"))
                      && !source.contains(QStringLiteral("taskTypeName === \"chartExport\"")),
                  QStringLiteral("the overlay consumes the typed chartExport property"), out);
    ok &= require(source.contains(QStringLiteral("visible: !root.chartExportActive"))
                      && source.contains(QStringLiteral("text: root.progress ? root.progress.label : \"\""))
                      && source.contains(QStringLiteral("wrapMode: Text.WordWrap")),
                  QStringLiteral("generic progress text keeps word wrapping"), out);
    ok &= require(!source.contains(QStringLiteral("onAboutToShow"))
                      && source.contains(QStringLiteral("onClicked: comic.selectPreviousFromHistory()"))
                      && source.contains(QStringLiteral("onClicked: comic.selectRandomNext()"))
                      && source.contains(QStringLiteral("enabled: comic.canSwitch && comic.canGoBack"))
                      && source.contains(QStringLiteral("enabled: comic.canSwitch")),
                  QStringLiteral("overlay buttons use history and random navigation without dialog-show initialization"), out);
    return ok;
}

bool verifyComicNavigation(QTextStream& out)
{
    const QString source = readSource(QStringLiteral("src/app/ui/components/JobProgressComic.qml"));
    bool ok = require(!source.isEmpty(), QStringLiteral("JobProgressComic.qml is readable"), out);
    ok &= require(source.contains(QStringLiteral("property var navigationHistory: []"))
                      && source.contains(QStringLiteral("property int navigationCursor: -1"))
                      && source.contains(QStringLiteral("readonly property bool canGoBack"))
                      && source.contains(QStringLiteral("function clearNavigationHistory()"))
                      && source.contains(QStringLiteral("function currentResourceIndex()"))
                      && source.contains(QStringLiteral("function recordCurrentResource(index)")),
                  QStringLiteral("comic navigation history stores bounded resource indices and a cursor"), out);

    const int initialize = source.indexOf(QStringLiteral("function initializeChartExport()"));
    const int initializeClear = source.indexOf(QStringLiteral("root.clearNavigationHistory()"), initialize);
    const int initializeRandom = source.indexOf(QStringLiteral("root.resources.selectRandomResource()"), initialize);
    const int initializeRecord = source.indexOf(QStringLiteral("function recordCurrentResource(index)"));
    ok &= require(initialize >= 0 && initializeClear > initialize
                      && initializeRandom > initializeClear
                      && source.indexOf(QStringLiteral("root.recordCurrentResource(root.currentResourceIndex())"), initialize)
                          > initializeRandom
                      && source.indexOf(QStringLiteral("root.navigationCursor = 0"), initialize) > initializeRandom
                      && initializeRecord >= 0,
                  QStringLiteral("first chart-export image clears history, selects randomly, and records the first item"), out);

    ok &= require(source.contains(QStringLiteral("function appendRandomResource()"))
                      && source.contains(QStringLiteral("root.pendingDirection = 1"))
                      && source.contains(QStringLiteral("root.resources.selectRandomResource()"))
                      && source.contains(QStringLiteral("function selectNextFromHistoryOrRandom()"))
                      && source.contains(QStringLiteral("root.selectNextFromHistoryOrRandom()"))
                      && source.contains(QStringLiteral("onTriggered: root.selectRandomNext()")),
                  QStringLiteral("timer and right button share the history-aware right-navigation entry"), out);

    const int rightHistory = source.indexOf(QStringLiteral(
        "root.navigationCursor + 1 < root.navigationHistory.length"));
    const int rightHistorySelect = source.indexOf(QStringLiteral(
        "root.resources.selectResource(nextIndex)"), rightHistory);
    const int rightRandom = source.indexOf(QStringLiteral("root.appendRandomResource()"), rightHistory);
    ok &= require(rightHistory >= 0 && rightHistorySelect > rightHistory && rightRandom > rightHistorySelect,
                  QStringLiteral("right navigation reads the existing right history before random selection at the end"), out);

    const int left = source.indexOf(QStringLiteral("function selectPreviousFromHistory()"));
    ok &= require(left >= 0
                      && source.indexOf(QStringLiteral(
                          "root.navigationHistory[root.navigationCursor - 1]"), left) > left
                      && source.indexOf(QStringLiteral("root.navigationCursor -= 1"), left) > left
                      && source.indexOf(QStringLiteral("root.resources.selectResource(previousIndex)"), left) > left
                      && source.indexOf(QStringLiteral("root.pendingDirection = -1"), left) > left,
                  QStringLiteral("left navigation moves the cursor back and selects the history item with negative direction"), out);

    const int incomingReady = source.indexOf(QStringLiteral("function handleIncomingReady()"));
    ok &= require(source.contains(QStringLiteral("function startImageTransition(direction)"))
                      && source.indexOf(QStringLiteral("if (!root.hasVisibleImage)"), incomingReady) > incomingReady
                      && source.indexOf(QStringLiteral("stageVisibleCommit()"), incomingReady) > incomingReady
                      && source.indexOf(QStringLiteral("root.startImageTransition("), incomingReady) > incomingReady
                      && source.contains(QStringLiteral("slideAnimation.start()")),
                  QStringLiteral("first image commits statically and later images enter slideAnimation"), out);

    ok &= require(source.contains(QStringLiteral("property bool chartExportInitialized: false"))
                      && source.contains(QStringLiteral("root.chartExportInitialized = true"))
                      && source.contains(QStringLiteral("root.chartExportInitialized = false"))
                      && source.contains(QStringLiteral("root.clearNavigationHistory()"))
                      && source.contains(QStringLiteral("root.navigationCursor = -1"))
                      && source.contains(QStringLiteral("visibleImage.source = \"\"")),
                  QStringLiteral("new tasks initialize independently and inactive comic regions clear pending image state"), out);
    return ok;
}

}  // namespace

int main()
{
    QTextStream out(stdout);
    const bool ok = verifyIconButton(out) && verifyProgressOverlay(out)
        && verifyComicNavigation(out);
    out << (ok ? "job_progress_controls_contract_spec ok\n"
               : "job_progress_controls_contract_spec failed\n");
    return ok ? 0 : 1;
}
