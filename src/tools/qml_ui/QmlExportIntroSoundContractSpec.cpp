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

bool verifySessionContract(QTextStream& err)
{
    const QString header = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlExportSession.h"));
    const QString implementation = readSource(
        QStringLiteral("src/app/qml_ui/export/QmlExportSession.cpp"));
    const QString settings = readSource(
        QStringLiteral("src/tools/video_export/VideoExportSettings.cpp"));
    bool ok = require(!header.isEmpty() && !implementation.isEmpty() && !settings.isEmpty(),
                      QStringLiteral("export intro-sound contract sources are readable"), err);

    for (const QString& property : {
             QStringLiteral("Q_PROPERTY(QVariantList introSoundOptions"),
             QStringLiteral("Q_PROPERTY(QString introSoundFileName"),
             QStringLiteral("Q_PROPERTY(double introSoundVolume"),
         }) {
        ok &= require(header.contains(property),
                      QStringLiteral("QmlExportSession exposes %1").arg(property), err);
    }
    ok &= require(
        header.contains(QStringLiteral("Q_INVOKABLE void importIntroSound()")),
        QStringLiteral("QmlExportSession exposes the intro-sound import action"),
        err);
    // The reload-vs-levels split still exists, it just spans the appearance
    // seam now: the session publishes the new intro sound and asks for a level
    // push, and the window turns those into reloadAssets / applyLevels. Both
    // halves are pinned so the distinction cannot quietly collapse into one.
    const QString frameBootstrap = readSource(
        QStringLiteral("src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp"));
    const QString previewSettings = readSource(
        QStringLiteral("src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp"));
    ok &= require(
        implementation.contains(QStringLiteral("normalizeIntroSoundFileName(fileName)"))
            && implementation.contains(QStringLiteral("setSelectedIntroSoundFileName(normalized)"))
            && implementation.contains(
                QStringLiteral("appearance_->setIntroSoundFileName(normalized)"))
            && implementation.contains(QStringLiteral("setSelectedIntroSoundVolume(normalized)"))
            && implementation.contains(QStringLiteral("backend_->applyPreviewSfxLevels()"))
            && frameBootstrap.contains(QStringLiteral("PreviewAppearanceState::introSoundChanged"))
            && frameBootstrap.contains(QStringLiteral("applyPreviewSfxLevels(/*reloadAssets=*/true)"))
            && previewSettings.contains(QStringLiteral("previewSfxRuntime_->reloadAssets"))
            && previewSettings.contains(QStringLiteral("previewSfxRuntime_->applyLevels")),
        QStringLiteral("the QML session mirrors Widgets preview_sfx reload and level-update semantics"),
        err);
    ok &= require(
        settings.count(QStringLiteral("intro_sound_volume")) >= 2
            && settings.contains(QStringLiteral("target->introSoundFileName = source.introSoundFileName"))
            && settings.contains(QStringLiteral("target->introSoundVolume = qBound")),
        QStringLiteral("shared preferences and difficulty reseeding keep intro-sound settings"),
        err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    const bool ok = verifySessionContract(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_export_intro_sound_contract_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
