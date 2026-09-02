#include <QFile>
#include <QString>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyQmlFontContract(QTextStream& err)
{
    const QString header = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlExportSession.h"));
    const QString implementation = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlExportSession.cpp"));
    const QString page = readSource(
        QStringLiteral("src/app/qml_ui/export/ExportVideoPage.qml"));
    const QString previewSettingsHeader = readSource(
        QStringLiteral("src/app/qml_ui/preview/QmlPreviewSettingsModel.h"));
    const QString previewSettingsImplementation = readSource(
        QStringLiteral("src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp"));
    const QString previewSettingsDialog = readSource(
        QStringLiteral("src/app/qml_ui/preview/PreviewSettingsDialog.qml"));
    const QString fontLibraryHeader = readSource(
        QStringLiteral("src/tools/video_export/FontLibrary.h"));
    const QString fontLibraryImplementation = readSource(
        QStringLiteral("src/tools/video_export/FontLibrary.cpp"));
    bool ok = require(!header.isEmpty() && !implementation.isEmpty() && !page.isEmpty()
                          && !previewSettingsHeader.isEmpty()
                          && !previewSettingsImplementation.isEmpty()
                          && !previewSettingsDialog.isEmpty() && !fontLibraryHeader.isEmpty()
                          && !fontLibraryImplementation.isEmpty(),
                      QStringLiteral("the QML font settings sources are readable"), err);

    for (const QString& contract : {
             QStringLiteral("Q_PROPERTY(QVariantList fontLibraryOptions"),
             QStringLiteral("Q_PROPERTY(QVariantList skinOptions"),
             QStringLiteral("Q_PROPERTY(int hudFontAreaIndex"),
             QStringLiteral("Q_PROPERTY(QString hudFontPath"),
             QStringLiteral("Q_PROPERTY(QString introFontDisplayPath"),
             QStringLiteral("Q_PROPERTY(QString introFontBodyPath"),
             QStringLiteral("Q_INVOKABLE void importIntroFont()"),
             QStringLiteral("Q_INVOKABLE void resetIntroFonts()"),
             QStringLiteral("Q_INVOKABLE void importHudFont()"),
             QStringLiteral("Q_INVOKABLE void resetHudFont()"),
         }) {
        ok &= require(header.contains(contract),
                      QStringLiteral("QmlExportSession exposes %1").arg(contract), err);
    }

    // The live redraw goes through MainWindow's narrow surface method now — the
    // QML layer no longer reaches previewCanvas_ directly — so both ends are
    // pinned: the session asks, and the playback coordinator still calls
    // update().
    const QString playbackSurfaceContract = readSource(
        QStringLiteral("src/app/runtime/playback/SurfaceContract.cpp"));
    ok &= require(
        implementation.contains(QStringLiteral("fontLibraryEntries("))
            && implementation.contains(QStringLiteral("importFontFileIntoLibrary(selectedPath)"))
            && implementation.contains(QStringLiteral("refreshIntroState()"))
            && implementation.contains(QStringLiteral("setPreviewHudCustomFontPath(area, path)"))
            && implementation.contains(QStringLiteral("preview()->refreshSurfaces()"))
            && playbackSurfaceContract.contains(
                QStringLiteral("void miacode::runtime::PlaybackCoordinator::refreshSurfaces()"))
            && playbackSurfaceContract.contains(QStringLiteral("scene_->update()")),
        QStringLiteral("the export session uses the shared library and redraws the live preview"),
        err);
    ok &= require(
        !implementation.contains(QStringLiteral("QFileDialog"))
            && !implementation.contains(QStringLiteral("QMessageBox"))
            && implementation.contains(QStringLiteral("uiRequests_->requestFile")),
        QStringLiteral("font import stays on the QML file-request boundary without Widgets dialogs"), err);
    ok &= require(
        fontLibraryHeader.contains(QStringLiteral("FontImportResult importFontFileIntoLibrary"))
            && fontLibraryImplementation.contains(QStringLiteral("FontImportResult importFontFileIntoLibrary")),
        QStringLiteral("the reusable font library owns validation and portable copying"), err);
    for (const QString& control : {
             QStringLiteral("introDisplayFontCombo"),
             QStringLiteral("introBodyFontCombo"),
             QStringLiteral("introFontImportButton"),
             QStringLiteral("exportSkinCombo"),
             QStringLiteral("hudFontAreaCombo"),
             QStringLiteral("hudFontCombo"),
             QStringLiteral("hudFontImportButton"),
             QStringLiteral("hudFontResetButton"),
         }) {
        ok &= require(page.contains(control),
                      QStringLiteral("the v2 export page owns %1").arg(control), err);
    }
    ok &= require(page.contains(QStringLiteral("id: \"skin\"")),
                  QStringLiteral("global skin and HUD settings have a v2 export-page entry"), err);

    for (const QString& contract : {
             QStringLiteral("Q_PROPERTY(QVariantList skinOptions"),
             QStringLiteral("Q_PROPERTY(QVariantList fontLibraryOptions"),
             QStringLiteral("Q_PROPERTY(int hudFontAreaIndex"),
             QStringLiteral("Q_PROPERTY(QString hudFontPath"),
             QStringLiteral("Q_INVOKABLE void importHudFont()"),
             QStringLiteral("Q_INVOKABLE void resetHudFont()"),
         }) {
        ok &= require(previewSettingsHeader.contains(contract),
                      QStringLiteral("QmlPreviewSettingsModel exposes %1").arg(contract), err);
    }
    ok &= require(
        previewSettingsImplementation.contains(
            QStringLiteral("setPreviewHudCustomFontPath(area, path)"))
            // The preview settings page reaches the live surfaces through
            // miacode::v2::PreviewSurface now; it no longer knows MainWindow.
            && previewSettingsImplementation.contains(
                QStringLiteral("surface()->refreshSurfaces()"))
            && previewSettingsImplementation.contains(QStringLiteral("uiRequests_->requestFile")),
        QStringLiteral("HUD font updates use the QML request boundary and redraw the live preview"), err);
    for (const QString& control : {
             QStringLiteral("previewSkinCombo"),
             QStringLiteral("previewHudFontAreaCombo"),
             QStringLiteral("previewHudFontCombo"),
             QStringLiteral("previewHudFontImportButton"),
             QStringLiteral("previewHudFontResetButton"),
         }) {
        ok &= require(previewSettingsDialog.contains(control),
                      QStringLiteral("Preview Settings owns %1").arg(control), err);
    }
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    const bool ok = verifyQmlFontContract(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_export_font_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
