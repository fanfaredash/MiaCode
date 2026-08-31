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

    expect(page.contains(QStringLiteral("property: \"chartSceneBinder\"; value: root.session;"))
               && page.contains(QStringLiteral("liveChartSceneBound")),
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
