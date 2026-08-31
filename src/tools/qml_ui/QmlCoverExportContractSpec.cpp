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
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));

    expect(!page.isEmpty() && !sessionHeader.isEmpty() && !session.isEmpty()
               && !pageHost.isEmpty() && !mainWindow.isEmpty() && !renderer.isEmpty()
               && !bootstrap.isEmpty() && !composer.isEmpty(),
           QStringLiteral("v2 cover page, session and renderer sources are readable"), out, &failed);
    expect(page.contains(QStringLiteral("CoverComposer.qml"))
               && page.contains(QStringLiteral("selectionBinder"))
               && page.contains(QStringLiteral("exportCover()"))
               && !page.contains(QStringLiteral("qsTr(")),
           QStringLiteral("the QML page owns composer interaction, export and localization"), out, &failed);
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
    // The page leaves chartSceneBinder null on purpose, so the chart-frame layer
    // is drawn only by CoverComposer.qml's image://coverchart still. The export
    // renderer registers that provider on its own private engine; the live page
    // needs the same registration on the application engine or every chart frame
    // renders blank on the canvas while the exported PNG still contains it.
    expect(composer.contains(QStringLiteral("image://coverchart/"))
               && bootstrap.contains(QStringLiteral("registerCoverChartImageProvider")),
           QStringLiteral("the application QML engine serves the chart-frame image provider"),
           out, &failed);

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
