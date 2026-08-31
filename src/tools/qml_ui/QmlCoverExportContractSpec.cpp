// Static contract for the v2 cover-export page. It pins the page route, QML
// ownership and the permanent deletion of the former CoverStudio Widgets shell.

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace {

QString sourceRoot() { return QStringLiteral(MIACODE_SOURCE_ROOT); }

QString readSource(const QString& relativePath)
{
    QFile file(sourceRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

int matchingBrace(const QString& source, int openBrace)
{
    if (openBrace < 0 || openBrace >= source.size() || source.at(openBrace) != QLatin1Char('{')) {
        return -1;
    }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    bool lineComment = false;
    bool blockComment = false;
    QChar quote;
    for (int index = openBrace; index < source.size(); ++index) {
        const QChar current = source.at(index);
        const QChar next = index + 1 < source.size() ? source.at(index + 1) : QChar();
        if (lineComment) {
            if (current == QLatin1Char('\n')) lineComment = false;
            continue;
        }
        if (blockComment) {
            if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                blockComment = false;
                ++index;
            }
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (current == QLatin1Char('\\')) {
                escaped = true;
            } else if (current == quote) {
                inString = false;
            }
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
            lineComment = true;
            ++index;
            continue;
        }
        if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
            blockComment = true;
            ++index;
            continue;
        }
        if (current == QLatin1Char('"') || current == QLatin1Char('\'')) {
            inString = true;
            quote = current;
        } else if (current == QLatin1Char('{')) {
            ++depth;
        } else if (current == QLatin1Char('}') && --depth == 0) {
            return index;
        }
    }
    return -1;
}

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    if (!condition) ++*failed;
    return condition;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    const QString page = readSource(QStringLiteral("src/app/qml_ui/export/CoverExportPage.qml"));
    const QString sessionHeader = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlCoverExportSession.h"));
    const QString session = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlCoverExportSession.cpp"));
    const QString sceneRendererHeader = readSource(
        QStringLiteral("src/tools/cover_export/SceneFrameRenderer.h"));
    const QString sceneRenderer = readSource(
        QStringLiteral("src/tools/cover_export/SceneFrameRenderer.cpp"));
    const QString pageHost = readSource(QStringLiteral("src/app/qml_ui/QmlEditorPageHost.cpp"));
    const QString mainWindow = readSource(
        QStringLiteral("src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp"));
    const QString renderer = readSource(
        QStringLiteral("src/tools/cover_export/CoverCompositeRenderer.cpp"));
    const QString bootstrap = readSource(QStringLiteral("src/app/qml_ui/QmlUiBootstrap.cpp"));
    const QString composer = readSource(QStringLiteral("src/intro/qml/CoverComposer.qml"));
    const QString mainSplitView = readSource(QStringLiteral("src/app/qml_ui/layout/MainSplitView.qml"));
    const QString labeledSlider = readSource(QStringLiteral("src/app/qml_ui/components/LabeledSlider.qml"));
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));

    expect(!page.isEmpty() && !sessionHeader.isEmpty() && !session.isEmpty()
               && !pageHost.isEmpty() && !mainWindow.isEmpty() && !renderer.isEmpty()
               && !bootstrap.isEmpty() && !composer.isEmpty() && !mainSplitView.isEmpty()
               && !sceneRendererHeader.isEmpty() && !sceneRenderer.isEmpty()
               && !labeledSlider.isEmpty(),
           QStringLiteral("v2 cover page, session and renderer sources are readable"), out, &failed);
    expect(page.contains(QStringLiteral("CoverComposer.qml"))
               && page.contains(QStringLiteral("selectionBinder"))
               && page.contains(QStringLiteral("activeChartFrameKey"))
               && page.contains(QStringLiteral("chartSceneBinder"))
               && page.contains(QStringLiteral("toggleActiveLayerPlayback"))
               && page.contains(QStringLiteral("exportCover()"))
               && !page.contains(QStringLiteral("qsTr(")),
           QStringLiteral("the QML page owns composer interaction, chart playback, export and localization"), out, &failed);
    // The session used to export nine invokables the page never called, so the
    // features read as missing while the API looked complete. Each one now has
    // a control; this list is what stops that drifting apart again.
    QStringList unreachable;
    for (const QString& call : {
             QStringLiteral("resetLayout()"),
             QStringLiteral("openRecentLayout("),
             QStringLiteral("clearRecentLayouts()"),
             QStringLiteral("duplicateActiveLayer()"),
             QStringLiteral("raiseActiveLayer()"),
             QStringLiteral("lowerActiveLayer()"),
             QStringLiteral("setActiveLayerFrameBackgroundBrightness("),
             QStringLiteral("setActiveLayerFrameBackgroundTransparency("),
             QStringLiteral("longTextMode"),
         }) {
        if (!page.contains(call)) unreachable.append(call);
    }
    expect(unreachable.isEmpty(),
           QStringLiteral("every cover session capability has a control: %1")
               .arg(unreachable.isEmpty() ? QStringLiteral("all reachable")
                                          : unreachable.join(QStringLiteral(", "))),
           out, &failed);
    // Layout chrome, not decoration: these are the shared v2 types. Hand-rolled
    // label+slider rows lose the read-out and, worse, a direct `value:` binding
    // on AppSlider dies on the first drag.
    expect(page.contains(QStringLiteral("LabeledSlider"))
               && page.contains(QStringLiteral("LabeledCombo"))
               && page.contains(QStringLiteral("PanelHeader"))
               && page.contains(QStringLiteral("SplitHandle"))
               && page.contains(QStringLiteral("panelTab: true"))
               && !page.contains(QStringLiteral("AppSlider {")),
           QStringLiteral("the page is built from the shared v2 chrome"), out, &failed);
    for (const QString& contract : {
             QStringLiteral("Q_INVOKABLE void addChartFrameLayer()"),
             QStringLiteral("Q_INVOKABLE void browseActiveLayerImage()"),
             QStringLiteral("Q_INVOKABLE void saveLayout()"),
             QStringLiteral("Q_INVOKABLE void exportCover()"),
             QStringLiteral("Q_PROPERTY(double chartFrameDiskDiameter"),
             QStringLiteral("Q_PROPERTY(bool chartFramePlaying"),
             QStringLiteral("Q_PROPERTY(bool liveChartSceneBound"),
             QStringLiteral("Q_PROPERTY(double activeChartFrameSeconds"),
             QStringLiteral("Q_INVOKABLE void previewActiveLayerFrameSeconds("),
             QStringLiteral("Q_INVOKABLE void commitActiveLayerFrameSeconds()"),
             QStringLiteral("Q_INVOKABLE void beginActiveLayerKeySeek("),
             QStringLiteral("Q_INVOKABLE void endActiveLayerKeySeek()"),
             QStringLiteral("Q_INVOKABLE void bindLiveChartScene("),
         }) {
        expect(sessionHeader.contains(contract),
               QStringLiteral("cover session exposes %1").arg(contract), out, &failed);
    }
    expect(session.contains(QStringLiteral("uiRequests_->requestFile"))
               && session.contains(QStringLiteral("exportCoverComposite"))
               && !session.contains(QStringLiteral("QFileDialog"))
               && !session.contains(QStringLiteral("QMessageBox"))
               && !session.contains(QStringLiteral("QWidget")),
           QStringLiteral("cover session uses the QML request boundary and pure renderer"), out, &failed);
    expect(pageHost.contains(QStringLiteral("coverPageRequested"))
               && pageHost.contains(QStringLiteral("activePageId_ = QStringLiteral(\"cover\")"))
               && pageHost.contains(QStringLiteral("backend_->qmlExportSession_->leave()"))
               && !pageHost.contains(QStringLiteral("onExportCover()")),
           QStringLiteral("cover export routes into the QML page and releases a prior video session"), out, &failed);
    expect(mainWindow.contains(QStringLiteral("emit coverExportRequested"))
               && !mainWindow.contains(QStringLiteral("CoverStudioWindow")),
           QStringLiteral("all MainWindow cover entry points emit the QML route"), out, &failed);
    expect(renderer.contains(QStringLiteral("QQuickWindow"))
               && !renderer.contains(QStringLiteral("QWidget"))
               && !renderer.contains(QStringLiteral("createWindowContainer")),
           QStringLiteral("cover composition stays in an in-process Quick scene"), out, &failed);
    expect(composer.contains(QStringLiteral("image://coverchart/"))
               && composer.contains(QStringLiteral("liveChartSceneBound"))
               && composer.contains(QStringLiteral("centroid.position"))
               && !composer.contains(QStringLiteral("centroid.scenePressPosition"))
               && bootstrap.contains(QStringLiteral("registerCoverChartImageProvider")),
           QStringLiteral("the application QML engine serves chart stills and the composer uses local live state"),
           out, &failed);
    expect(labeledSlider.contains(QStringLiteral("signal released"))
               && labeledSlider.contains(QStringLiteral("onReleased")),
           QStringLiteral("the shared slider exposes a release boundary for hot scrubbing"), out, &failed);
    expect(mainSplitView.contains(QStringLiteral("visible: !root.coverExportActive"))
               && mainSplitView.contains(QStringLiteral("surfaceActive: !root.coverExportActive && !fullscreenPreview.visible"))
               && mainSplitView.contains(QStringLiteral("if (root.coverExportActive)"))
               && mainSplitView.contains(QStringLiteral("onCoverExportActiveChanged")),
           QStringLiteral("cover export hides the editor preview and blocks fullscreen preview"), out, &failed);
    expect(session.contains(QStringLiteral("CoverFrameSceneBinder"))
               && session.contains(QStringLiteral("renderVisibleChartFramesForExport"))
               && cmake.contains(QStringLiteral("CoverFramePlaybackController"))
               && cmake.contains(QStringLiteral("CoverFrameSceneBinder"))
               && cmake.contains(QStringLiteral("CoverFrameExportPlan")),
           QStringLiteral("the build contains the cover transport, binder and export plan"), out, &failed);

    // The live chart scene is the entry-time preview path. It must not depend on
    // an offscreen grab or a retry timer before the page becomes usable.
    const int seedStart = session.indexOf(
        QStringLiteral("void QmlCoverExportSession::seedFromDifficulty(int difficultyId)"));
    const int seedOpen = session.indexOf(QLatin1Char('{'), seedStart);
    const int seedClose = matchingBrace(session, seedOpen);
    const QString seedBody = seedOpen >= 0 && seedClose > seedOpen
                                 ? session.mid(seedOpen, seedClose - seedOpen)
                                 : QString();
    expect(!sessionHeader.contains(QStringLiteral("previewFrameRefreshTimer_"))
               && !sessionHeader.contains(QStringLiteral("scheduleVisibleChartFramePreview"))
               && !session.contains(QStringLiteral("scheduleVisibleChartFramePreview"))
               && !session.contains(QStringLiteral("refreshVisibleChartFramePreview"))
               && !seedBody.contains(QStringLiteral("renderVisibleChartFramesForPreview")),
           QStringLiteral("cover entry uses the live chart scene instead of an offscreen preview refresh"),
           out, &failed);
    const int seedPlaybackSync = seedBody.indexOf(QStringLiteral("syncPlaybackFromActiveLayer();"));
    const int seedSecondsSignal = seedBody.indexOf(
        QStringLiteral("emit activeChartFrameSecondsChanged();"), seedPlaybackSync);
    expect(seedPlaybackSync >= 0 && seedSecondsSignal > seedPlaybackSync,
           QStringLiteral("cover entry republishes the restored frame time after playback sync"),
           out, &failed);
    expect(sceneRendererHeader.contains(QStringLiteral("prepareCaptureWindow"))
               && sceneRendererHeader.contains(QStringLiteral("captureReady"))
               && sceneRenderer.contains(QStringLiteral("sceneGraphInitialized"))
               && sceneRenderer.contains(QStringLiteral("sceneGraphInvalidated"))
               && sceneRenderer.contains(QStringLiteral("sceneGraphError"))
               && sceneRenderer.contains(QStringLiteral("isExposed"))
               && sceneRenderer.contains(QStringLiteral("captureReady()"))
               && !sceneRenderer.contains(QStringLiteral("setPosition(-32000"))
               && !sceneRenderer.contains(QStringLiteral("QCoreApplication::processEvents"))
               && !sceneRenderer.contains(QStringLiteral("QThread::msleep")),
           QStringLiteral("secondary chart capture uses a usable Quick surface without nested event pumping"),
           out, &failed);

    const int applyStart = session.indexOf(
        QStringLiteral("bool QmlCoverExportSession::applyCompositionJson(const QJsonObject& root, bool reportErrors)"));
    const int applyOpen = session.indexOf(QLatin1Char('{'), applyStart);
    const int applyClose = matchingBrace(session, applyOpen);
    const QString applyBody = applyOpen >= 0 && applyClose > applyOpen
                                  ? session.mid(applyOpen, applyClose - applyOpen)
                                  : QString();
    expect(applyBody.contains(QStringLiteral("layout_->fromJson(state.layout)"))
               && applyBody.contains(QStringLiteral("visibleChartFrameLayers()"))
               && applyBody.contains(QStringLiteral("firstVisibleFrameKey"))
               && applyBody.contains(QStringLiteral("activeLayerKey_ = firstVisibleFrameKey")),
           QStringLiteral("layout restore promotes the first visible chart frame to the live active layer"),
           out, &failed);

    const int renderStart = session.indexOf(
        QStringLiteral("bool QmlCoverExportSession::renderChartFrame("));
    const int renderOpen = session.indexOf(QLatin1Char('{'), renderStart);
    const int renderClose = matchingBrace(session, renderOpen);
    const QString renderBody = renderOpen >= 0 && renderClose > renderOpen
                                   ? session.mid(renderOpen, renderClose - renderOpen)
                                   : QString();
    expect(seedBody.contains(QStringLiteral("clearLayerImage(layer->key())"))
               && session.contains(QStringLiteral("renderVisibleChartFramesForExport"))
               && session.contains(QStringLiteral("layout_->setLayerImage"))
               && !renderBody.contains(QStringLiteral("clearLayerImage(layer->key())")),
           QStringLiteral("new chart sessions discard stale stills while failed captures preserve the cache"),
           out, &failed);

    // Selection must be an explicit UI route rather than an incidental side
    // effect of the model. This catches regressions where a canvas tap changes
    // the active layer but leaves the inspector on the canvas tab.
    expect(page.contains(QStringLiteral("function selectLayerFromUi(key)"))
               && page.contains(QStringLiteral("root.selectLayerFromUi(layerRow.modelData.key)"))
               && composer.contains(QStringLiteral("canvas.layerSelectionCallback.call(canvas, key)")),
           QStringLiteral("all interactive layer selections route to the layer inspector"),
           out, &failed);

    // A live preview scene is paint-only inside the composer. It must not steal
    // the click/drag grab from the layer handlers, and unloading an old Repeater
    // delegate must not detach a newer live scene.
    expect(composer.contains(QStringLiteral("enabled: false"))
               && composer.contains(QStringLiteral("if (!item)"))
               && composer.contains(QStringLiteral("return")),
           QStringLiteral("live chart scene keeps layer input and binding teardown stable"),
           out, &failed);

    // Keyboard transport is focus-driven, so the chart controls need an
    // explicit focus handoff and a way to yield to the inline numeric editor.
    expect(page.contains(QStringLiteral("forceActiveFocus"))
               && page.contains(QStringLiteral("valueEditing"))
               && labeledSlider.contains(QStringLiteral("valueEditing")),
           QStringLiteral("chart transport acquires focus without stealing numeric text input"),
           out, &failed);
    expect(page.contains(QStringLiteral(
                   "enabled: root.activeLayer && root.activeLayer.frameBgMode === \"image\""))
               && page.contains(QStringLiteral(
                   "enabled: root.activeLayer && root.activeLayer.frameBgMode === \"transparent\"")),
           QStringLiteral("chart-frame brightness and transparency follow their background modes"),
           out, &failed);

    expect(page.contains(QStringLiteral("property: \"chartSceneBinder\"; value: root.session;"))
               && page.contains(QStringLiteral("chartFrameAvailable")),
           QStringLiteral("the composer receives the complete cover session binding facade"), out, &failed);
    expect(composer.contains(QStringLiteral("syncLiveChartBinding"))
               && composer.contains(QStringLiteral("boundBinder"))
               && composer.contains(QStringLiteral("onChartSceneBinderChanged"))
               && composer.contains(QStringLiteral("onItemChanged")),
           QStringLiteral("the live chart loader synchronizes binder and item lifetimes"), out, &failed);
    expect(!page.contains(QStringLiteral("text: UiText.text(\"cover.difficulty_card\")"))
               && page.contains(QStringLiteral("id: cardSettings"))
               && page.contains(QStringLiteral("visible: root.inspectorTab === \"layer\""))
               && page.contains(QStringLiteral("root.inspectorTab = \"layer\"")),
           QStringLiteral("difficulty-card settings live below the layer inspector and layer selection opens it"),
           out, &failed);
    expect(session.contains(QStringLiteral("QString::fromUtf8(preset.label)"))
               && !session.contains(QStringLiteral("QString::fromLatin1(preset.label)")),
           QStringLiteral("cover resolution labels decode UTF-8 without mojibake"), out, &failed);

    const int canvasTab = page.indexOf(QStringLiteral("text: UiText.text(\"cover.canvas\")"));
    const int layerTab = page.indexOf(QStringLiteral("text: UiText.text(\"cover.layer\")"));
    const int presetTab = page.indexOf(QStringLiteral("text: UiText.text(\"cover.manage_presets\")"));
    expect(page.count(QStringLiteral("panelTab: true")) == 3
               && canvasTab >= 0 && canvasTab < layerTab && layerTab < presetTab
               && !page.contains(QStringLiteral("text: UiText.text(\"cover.difficulty_card\")")),
           QStringLiteral("the inspector exposes exactly canvas, layer and preset tabs in order"), out,
           &failed);

    const int layerSection = page.indexOf(QStringLiteral("// ---- 图层 ----"));
    const int inspectorOpen = page.indexOf(QStringLiteral("ColumnLayout {\n                            id: inspector"));
    const int inspectorClose = matchingBrace(page, page.indexOf(QLatin1Char('{'), inspectorOpen));
    const int cardSection = page.indexOf(QStringLiteral("id: cardSettings"));
    const int presetSection = page.indexOf(QStringLiteral("// ---- 预设 ----"));
    const int layerRowSelection = page.indexOf(
        QStringLiteral("root.selectLayerFromUi(layerRow.modelData.key)"));
    const int layerRowTabRoute = page.lastIndexOf(
        QStringLiteral("root.inspectorTab = \"layer\""), layerRowSelection);
    expect(page.count(QStringLiteral("Flickable {")) == 1
               && layerSection >= 0 && inspectorOpen >= 0 && inspectorClose > inspectorOpen
               && cardSection > layerSection && cardSection < inspectorClose
               && presetSection > cardSection && presetSection < inspectorClose
               && layerRowSelection >= 0 && layerRowTabRoute >= 0
               && layerRowTabRoute < layerRowSelection
               && page.contains(QStringLiteral("contentHeight: inspector.implicitHeight")),
           QStringLiteral("layer and difficulty settings share one dynamically sized inspector"), out,
           &failed);

    const int syncStart = composer.indexOf(QStringLiteral("function syncLiveChartBinding()"));
    const int syncOpen = composer.indexOf(QLatin1Char('{'), syncStart);
    const int syncClose = matchingBrace(composer, syncOpen);
    const QString syncBody = syncOpen >= 0 && syncClose > syncOpen
                                 ? composer.mid(syncOpen, syncClose - syncOpen)
                                 : QString();
    const int unbindCall = syncBody.indexOf(QStringLiteral("boundBinder.unbindLiveChartScene"));
    const int binderCapture = syncBody.indexOf(QStringLiteral("var nextBinder = canvas.chartSceneBinder"));
    const int bindCall = syncBody.indexOf(QStringLiteral("boundBinder.bindLiveChartScene"));
    expect(syncStart >= 0 && syncClose > syncOpen && binderCapture >= 0 && unbindCall > binderCapture
               && bindCall > unbindCall,
           QStringLiteral("live chart rebinding captures the new identity before replacing the old binding"),
           out, &failed);

    const int bindFacade = session.indexOf(
        QStringLiteral("void QmlCoverExportSession::bindLiveChartScene(QObject* scene)"));
    const int facadeOpen = session.indexOf(QLatin1Char('{'), bindFacade);
    const int facadeClose = matchingBrace(session, facadeOpen);
    const QString facadeBody = facadeOpen >= 0 && facadeClose > facadeOpen
                                   ? session.mid(facadeOpen, facadeClose - facadeOpen)
                                   : QString();
    const int setLayerFlags = facadeBody.indexOf(QStringLiteral("liveScene->setLayerFlags"));
    const int setLiveFrameState = facadeBody.indexOf(QStringLiteral("liveScene->setFrameState"));
    const int setBinderFrameState = facadeBody.indexOf(QStringLiteral("sceneBinder_->setFrameState"));
    const int bindFacadeScene = facadeBody.indexOf(QStringLiteral("sceneBinder_->bindLiveChartScene"));
    expect(bindFacade >= 0 && facadeClose > facadeOpen && setLayerFlags >= 0
               && setLiveFrameState > setLayerFlags && setBinderFrameState > setLiveFrameState
               && bindFacadeScene > setBinderFrameState,
           QStringLiteral("the session facade wires overlay layers, shared frame state and scene binding"), out,
           &failed);

    const QStringList removedFiles{
        QStringLiteral("src/tools/cover_export/CoverComposerView.cpp"),
        QStringLiteral("src/tools/cover_export/CoverComposerView.h"),
        QStringLiteral("src/tools/cover_export/CoverStudioPanel.cpp"),
        QStringLiteral("src/tools/cover_export/CoverStudioPanel.h"),
        QStringLiteral("src/tools/cover_export/CoverStudioWindow.cpp"),
        QStringLiteral("src/tools/cover_export/CoverStudioWindow.h"),
        QStringLiteral("src/tools/cover_export/ExportCoverDialog.cpp"),
        QStringLiteral("src/tools/cover_export/ExportCoverDialog.h"),
    };
    QStringList present;
    for (const QString& path : removedFiles) {
        if (QFileInfo::exists(sourceRoot() + QLatin1Char('/') + path)) present.append(path);
    }
    expect(present.isEmpty(), QStringLiteral("the former CoverStudio Widgets shell is deleted"), out,
           &failed);

    QStringList cmakeLeftovers;
    for (const QString& token : {QStringLiteral("CoverStudioWindow"),
                                 QStringLiteral("CoverStudioPanel"),
                                 QStringLiteral("CoverComposerView"),
                                 QStringLiteral("ExportCoverDialog")}) {
        if (cmake.contains(token)) cmakeLeftovers.append(token);
    }
    expect(cmake.contains(QStringLiteral("CoverExportPage.qml"))
               && cmake.contains(QStringLiteral("QmlCoverExportSession"))
               && cmakeLeftovers.isEmpty(),
           QStringLiteral("the build contains only the QML cover surface"), out, &failed);

    if (failed != 0) {
        out << "QmlCoverExportContract spec failed: " << failed << '\n';
        return 1;
    }
    out << "QmlCoverExportContract spec passed.\n";
    return 0;
}
