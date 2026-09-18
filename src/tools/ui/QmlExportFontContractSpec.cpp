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
        QStringLiteral("src/app/ui/export/ExportSession.h"));
    const QString implementation = readSource(
        QStringLiteral("src/app/ui/export/ExportSession.cpp"));
    const QString page = readSource(
        QStringLiteral("src/app/ui/export/ExportVideoPage.qml"));
    const QString previewSettingsHeader = readSource(
        QStringLiteral("src/app/ui/preview/PreviewSettingsModel.h"));
    const QString previewSettingsImplementation = readSource(
        QStringLiteral("src/app/ui/preview/PreviewSettingsModel.cpp"));
    const QString previewSettingsDialog = readSource(
        QStringLiteral("src/app/ui/preview/PreviewSettingsDialog.qml"));
    const QString fontLibraryHeader = readSource(
        QStringLiteral("src/tools/video_export/FontLibrary.h"));
    const QString fontLibraryImplementation = readSource(
        QStringLiteral("src/tools/video_export/FontLibrary.cpp"));
    const QString previewHudStateHeader = readSource(
        QStringLiteral("src/core/scene/PreviewHudState.h"));
    const QString previewHudStateImplementation = readSource(
        QStringLiteral("src/core/scene/PreviewHudState.cpp"));
    bool ok = require(!header.isEmpty() && !implementation.isEmpty() && !page.isEmpty()
                          && !previewSettingsHeader.isEmpty()
                          && !previewSettingsImplementation.isEmpty()
                          && !previewSettingsDialog.isEmpty() && !fontLibraryHeader.isEmpty()
                          && !fontLibraryImplementation.isEmpty()
                          && !previewHudStateHeader.isEmpty()
                          && !previewHudStateImplementation.isEmpty(),
                      QStringLiteral("the QML font settings sources are readable"), err);

    for (const QString& contract : {
             QStringLiteral("Q_PROPERTY(QVariantList fontLibraryOptions"),
             QStringLiteral("Q_PROPERTY(QString introFontDisplayPath"),
             QStringLiteral("Q_PROPERTY(QString introFontBodyPath"),
             QStringLiteral("Q_INVOKABLE void importIntroFont()"),
             QStringLiteral("Q_INVOKABLE void resetIntroFonts()"),
         }) {
        ok &= require(header.contains(contract),
                      QStringLiteral("ExportSession exposes %1").arg(contract), err);
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
            && playbackSurfaceContract.contains(
                QStringLiteral("void miacode::runtime::PlaybackCoordinator::refreshSurfaces()"))
            && playbackSurfaceContract.contains(QStringLiteral("scene_->update()")),
        QStringLiteral("the export session uses the shared library and redraws the live preview"),
        err);
    ok &= require(
        previewSettingsImplementation.contains(QStringLiteral("previewHudFontAreaChoices()"))
            && previewSettingsImplementation.contains(QStringLiteral("QStringLiteral(\"areaId\")"))
            && previewHudStateHeader.contains(QStringLiteral("CenterDisplay"))
            && previewHudStateImplementation.contains(
                QStringLiteral("hud_font_area.center_display"))
            && !previewSettingsImplementation.contains(
                QStringLiteral("static_cast<miacode::preview::scene::PreviewHudFontArea>(hudFontAreaIndex_)")),
        QStringLiteral("preview settings share areaId mapping, including CenterDisplay, without index-to-enum casts"),
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
    const QString appearancePages = readSource(
        QStringLiteral("src/app/ui/preview/PreviewAppearancePages.qml"));
    ok &= require(!appearancePages.isEmpty(),
                  QStringLiteral("the shared appearance pages source is readable"), err);
    for (const QString& control : {
             QStringLiteral("introDisplayFontCombo"),
             QStringLiteral("introBodyFontCombo"),
             QStringLiteral("introFontImportButton"),
         }) {
        ok &= require(page.contains(control),
                      QStringLiteral("the v2 export page owns %1").arg(control), err);
    }
    ok &= require(page.contains(QStringLiteral("PreviewAppearancePages")),
                  QStringLiteral("the export page hosts the shared video/gameplay/skin form"), err);
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
                      QStringLiteral("PreviewSettingsModel exposes %1").arg(contract), err);
    }
    ok &= require(
        previewSettingsImplementation.contains(
            QStringLiteral("setPreviewHudCustomFontPath(area, path)"))
            // The preview settings page reaches the live surfaces through
            // miacode::PreviewSurface now; it no longer knows MainWindow.
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
        ok &= require(appearancePages.contains(control),
                      QStringLiteral("the shared appearance form owns %1").arg(control), err);
    }
    ok &= require(previewSettingsDialog.contains(QStringLiteral("PreviewAppearancePages")),
                  QStringLiteral("Preview Settings hosts the shared appearance form"), err);

    // The export page and Preview Settings both write the one live preview render state,
    // so each has to hear the other's writes. Without that, the dialog keeps showing the
    // values it read when it was built, and the export task keeps its page-entry copy and
    // writes it back over the newer value on the next edit or export.
    const QString shellNotifications = readSource(QStringLiteral("src/app/services/ShellNotifications.h"));
    const QString renderSettingsWriter = readSource(
        QStringLiteral("src/app/runtime/preview/WarmupAndSettings.cpp"));
    const QString exportFlow = readSource(QStringLiteral("src/app/runtime/export/ExportFlow.cpp"));
    ok &= require(shellNotifications.contains(QStringLiteral("void previewRenderSettingsChanged();")),
                  QStringLiteral("the shell announces preview render setting changes"), err);
    ok &= require(renderSettingsWriter.contains(QStringLiteral("previewRenderSettingsChanged()"))
                      && exportFlow.contains(QStringLiteral("previewRenderSettingsChanged()")),
                  QStringLiteral("both render-setting writers (Preview Settings, export page) announce writes"), err);
    ok &= require(previewSettingsImplementation.contains(
                      QStringLiteral("&miacode::ShellNotifications::previewRenderSettingsChanged"))
                      && implementation.contains(
                          QStringLiteral("&miacode::ShellNotifications::previewRenderSettingsChanged")),
                  QStringLiteral("Preview Settings and the export session re-read announced render settings"), err);
    ok &= require(previewSettingsDialog.contains(QStringLiteral("previewSettings.refresh()")),
                  QStringLiteral("opening Preview Settings re-reads every value"), err);
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
