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
    // The shared button keeps only the generic state-color hook: the comic-only
    // color override it briefly carried is gone, so no single control can bypass
    // the shared state calculation.
    ok &= require(!source.contains(QStringLiteral("glyphColorOverride")),
                  QStringLiteral("the shared button carries no comic-only glyph color override"), out);
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

    // One control remains, and it is gated by the resource count and the comic
    // component's own switchability.
    ok &= require(!source.contains(QStringLiteral("onAboutToShow"))
                      && source.contains(QStringLiteral("id: nextButton"))
                      && source.contains(QStringLiteral("onClicked: comic.selectRandomNext()"))
                      && source.contains(QStringLiteral("enabled: comic.canSwitch"))
                      && source.contains(QStringLiteral("root.comicResources.resourceCount >= 2")),
                  QStringLiteral("the next button drives random selection off the resource count"), out);
    ok &= require(!source.contains(QStringLiteral("previousButton"))
                      && !source.contains(QStringLiteral("qml.previous_comic"))
                      && !source.contains(QStringLiteral("qml.show_previous_comic"))
                      && !source.contains(QStringLiteral("canGoBack")),
                  QStringLiteral("the previous button and its gating are removed"), out);

    // Comic colors are computed next to the comic controls, so the shared theme
    // keeps only generic tokens while the state-color hook is still used.
    ok &= require(!source.contains(QStringLiteral("Theme.comicButtonBackground"))
                      && !source.contains(QStringLiteral("Theme.comicButtonLightGlyph"))
                      && !source.contains(QStringLiteral("Theme.comicButtonDarkGlyph"))
                      && source.contains(QStringLiteral("glyphStateColors: root.comicButtonGlyphStateColors"))
                      && source.contains(QStringLiteral("stateColors: root.comicButtonStateColors")),
                  QStringLiteral("comic button colors are computed locally over the shared state-color hook"), out);

    // The comic component is handed the job identity, not just the task type, so
    // it can re-initialize for every job rather than for every type change.
    ok &= require(source.contains(QStringLiteral("taskToken: root.progress ? root.progress.token : 0")),
                  QStringLiteral("the overlay hands the comic component the job token"), out);
    return ok;
}

// A generic job must keep exactly the progress controls it had before the comic
// region existed, and must be unable to reach the comic at all.
bool verifyGenericTaskSurface(QTextStream& out)
{
    const QString source = readSource(QStringLiteral("src/app/ui/components/JobProgressOverlay.qml"));
    bool ok = require(!source.isEmpty(), QStringLiteral("JobProgressOverlay.qml is readable"), out);
    ok &= require(source.contains(QStringLiteral("objectName: \"jobProgressBar\""))
                      && source.contains(QStringLiteral("value: root.progress ? root.progress.percent : 0"))
                      && source.contains(QStringLiteral("indeterminate: !!root.progress && root.progress.indeterminate")),
                  QStringLiteral("the progress bar keeps reading the typed job state"), out);
    ok &= require(source.contains(QStringLiteral("closePolicy: Popup.NoAutoClose"))
                      && source.contains(QStringLiteral("visible: !!root.progress && root.progress.cancellable"))
                      && source.contains(QStringLiteral("root.progress.requestCancel()")),
                  QStringLiteral("the generic cancel footer and close policy are unchanged"), out);
    // The comic row only has height under the chart-export state, so a generic
    // job collapses it to zero and never shows the frame or its button.
    ok &= require(source.contains(QStringLiteral("root.chartExportActive && root.hasComicResources")),
                  QStringLiteral("the comic row collapses for every non-chart-export job"), out);
    return ok;
}

bool verifySingleRandomRotation(QTextStream& out)
{
    const QString source = readSource(QStringLiteral("src/app/ui/components/JobProgressComic.qml"));
    bool ok = require(!source.isEmpty(), QStringLiteral("JobProgressComic.qml is readable"), out);

    // The timer and the next button share one random entry point.
    ok &= require(source.contains(QStringLiteral("function selectRandomNext()"))
                      && source.contains(QStringLiteral("root.resources.selectRandomResource()"))
                      && source.contains(QStringLiteral("onTriggered: root.selectRandomNext()")),
                  QStringLiteral("the timer and the next button share the random selection entry"), out);

    // The image still loads asynchronously and enters from the right.
    ok &= require(source.contains(QStringLiteral("incomingImage.x = root.width"))
                      && source.contains(QStringLiteral("to: -root.width"))
                      && source.contains(QStringLiteral("asynchronous: true"))
                      && source.contains(QStringLiteral("slideAnimation.start()")),
                  QStringLiteral("the image enters from the right after an asynchronous load"), out);
    const int incomingReady = source.indexOf(QStringLiteral("function handleIncomingReady()"));
    ok &= require(incomingReady >= 0
                      && source.indexOf(QStringLiteral("if (!root.hasVisibleImage)"), incomingReady) > incomingReady
                      && source.indexOf(QStringLiteral("stageVisibleCommit()"), incomingReady) > incomingReady
                      && source.indexOf(QStringLiteral("root.startImageTransition()"), incomingReady) > incomingReady,
                  QStringLiteral("the first image commits statically and later images slide"), out);

    // Model changes still drive the incoming image and the tracked index.
    ok &= require(source.contains(QStringLiteral("function syncFromModel()"))
                      && source.contains(QStringLiteral("function modelIndexForUrl(url)"))
                      && source.contains(QStringLiteral("incomingImage.source = modelUrl")),
                  QStringLiteral("model changes still feed the incoming image and index sync"), out);

    // Navigation history and sequential movement are gone, including the
    // leftward direction state they used to drive.
    ok &= require(!source.contains(QStringLiteral("navigationHistory"))
                      && !source.contains(QStringLiteral("navigationCursor"))
                      && !source.contains(QStringLiteral("selectPreviousFromHistory"))
                      && !source.contains(QStringLiteral("selectNextFromHistoryOrRandom"))
                      && !source.contains(QStringLiteral("canGoBack"))
                      && !source.contains(QStringLiteral("pendingDirection")),
                  QStringLiteral("navigation history, previous selection, and the direction state are removed"), out);

    // Only a chart export reaches the carousel, so a generic job never shows the
    // comic region and never triggers a scan from it.
    ok &= require(source.contains(QStringLiteral(
                      "visible: root.active && root.chartExportActive && root.hasResources")),
                  QStringLiteral("the comic area is gated on the chart-export state"), out);

    // Each job initializes the carousel exactly once, keyed on the job token.
    // A chart export replacing another never changes the task type, so a
    // chartExportActive edge alone would leave the previous job's image up.
    ok &= require(source.contains(QStringLiteral("property real taskToken: 0"))
                      && source.contains(QStringLiteral("property real initializedTaskToken: 0"))
                      && source.contains(QStringLiteral("function ensureChartExportInitialized()"))
                      && source.contains(QStringLiteral("if (root.initializedTaskToken === root.taskToken)"))
                      && source.contains(QStringLiteral("root.initializedTaskToken = root.taskToken"))
                      && source.contains(QStringLiteral(
                          "onTaskTokenChanged: ensureChartExportInitialized()")),
                  QStringLiteral("each job re-initializes once, keyed on the job token"), out);

    // Initialization clears the previous job's state before selecting again.
    const int ensureInit = source.indexOf(QStringLiteral("function ensureChartExportInitialized()"));
    const int selectRandom = source.indexOf(QStringLiteral("root.resources.selectRandomResource()"), ensureInit);
    ok &= require(ensureInit >= 0
                      && source.indexOf(QStringLiteral("stopBanner()"), ensureInit) > ensureInit
                      && source.indexOf(QStringLiteral("root.ensureResourcesScanned()"), ensureInit) > ensureInit
                      && selectRandom > ensureInit,
                  QStringLiteral("initialization clears the previous image before selecting the next"), out);

    // Task completion, cancellation, failure and re-entry all funnel through the
    // same teardown: stop the timer and animation and drop both images.
    const int stopBanner = source.indexOf(QStringLiteral("function stopBanner()"));
    ok &= require(stopBanner >= 0
                      && source.indexOf(QStringLiteral("slideTimer.stop()"), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("slideAnimation.stop()"), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("incomingImage.source = \"\""), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("incomingImage.x = root.width"), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("visibleImage.source = \"\""), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("root.currentIndex = -1"), stopBanner) > stopBanner
                      && source.indexOf(QStringLiteral("root.switching = false"), stopBanner) > stopBanner,
                  QStringLiteral("task end stops the timer and animation and clears both images"), out);

    // An empty directory hides the region; a single image never rotates.
    ok &= require(source.contains(QStringLiteral("&& root.resources.resourceCount >= 2")),
                  QStringLiteral("a single-image directory never starts the rotation"), out);
    const int updateTimer = source.indexOf(QStringLiteral("function updateTimer()"));
    ok &= require(updateTimer >= 0
                      && source.indexOf(QStringLiteral("root.resources.resourceCount >= 2"), updateTimer) > updateTimer,
                  QStringLiteral("the slide timer requires at least two resources"), out);
    return ok;
}

bool verifyModelRandomSelection(QTextStream& out)
{
    const QString header = readSource(QStringLiteral("src/app/services/ComicResourceModel.h"));
    const QString implementation = readSource(QStringLiteral("src/app/services/ComicResourceModel.cpp"));
    bool ok = require(!header.isEmpty() && !implementation.isEmpty(),
                      QStringLiteral("the resource model sources are readable"), out);

    // Only the random entry point plus indexed access remain.
    ok &= require(header.contains(QStringLiteral("Q_INVOKABLE void selectRandomResource();"))
                      && header.contains(QStringLiteral("Q_INVOKABLE void selectResource(int index);"))
                      && header.contains(QStringLiteral("Q_INVOKABLE QString imageUrlAt(int index) const;"))
                      && header.contains(QStringLiteral("Q_PROPERTY(QString currentImageUrl"))
                      && header.contains(QStringLiteral("Q_PROPERTY(int resourceCount")),
                  QStringLiteral("the model keeps the random and indexed carousel interface"), out);
    ok &= require(!header.contains(QStringLiteral("selectNextResource"))
                      && !header.contains(QStringLiteral("selectPreviousResource"))
                      && !implementation.contains(QStringLiteral("selectNextResource"))
                      && !implementation.contains(QStringLiteral("selectPreviousResource")),
                  QStringLiteral("the sequential next and previous interfaces are removed"), out);

    // Random selection draws from the remaining candidates and shifts past the
    // current one, so a rotation cannot repeat the visible image.
    ok &= require(implementation.contains(QStringLiteral("bounded(resources_.size() - 1)"))
                      && implementation.contains(QStringLiteral(
                          "randomOffset >= currentIndex_ ? randomOffset + 1 : randomOffset")),
                  QStringLiteral("random selection excludes the current resource"), out);
    return ok;
}

// The comic region is driven from typed service state, so the job identity and
// the chart-export flag have to stay published properties rather than being
// inferred in QML.
bool verifyTaskIdentityIsTyped(QTextStream& out)
{
    const QString service = readSource(QStringLiteral("src/app/services/JobProgressService.h"));
    bool ok = require(!service.isEmpty(), QStringLiteral("JobProgressService.h is readable"), out);
    ok &= require(service.contains(QStringLiteral("Q_PROPERTY(bool chartExport READ chartExport NOTIFY changed)"))
                      && service.contains(QStringLiteral(
                          "Q_PROPERTY(quint64 token READ token NOTIFY changed)")),
                  QStringLiteral("the chart-export flag and the job token are typed service properties"), out);
    return ok;
}

}  // namespace

int main()
{
    QTextStream out(stdout);
    const bool ok = verifyIconButton(out) && verifyProgressOverlay(out)
        && verifyGenericTaskSurface(out) && verifySingleRandomRotation(out)
        && verifyModelRandomSelection(out) && verifyTaskIdentityIsTyped(out);
    out << (ok ? "job_progress_controls_contract_spec ok\n"
               : "job_progress_controls_contract_spec failed\n");
    return ok ? 0 : 1;
}
