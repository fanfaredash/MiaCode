// Stage 4 guard: the supported QML shell must not regain dead Widget-only
// adapters that were reachable only through the hidden MainWindow.

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

bool require(bool condition, const QString& message)
{
    if (!condition) {
        err() << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString readFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool appSourceTreeContainsLegacyResidue()
{
    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT) + QStringLiteral("/src/app");
    QDirIterator files(root,
        QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                                   QStringLiteral("*.inc"), QStringLiteral("*.mm")},
                       QDir::Files,
                       QDirIterator::Subdirectories);
    bool clean = true;
    while (files.hasNext()) {
        const QString path = files.next();
        if (QFileInfo(path).fileName() == QStringLiteral("Stage4WidgetResidueSpec.cpp")) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            clean &= require(false, QStringLiteral("could not read an app production source file"));
            continue;
        }
        const QString source = QString::fromUtf8(file.readAll());
        clean &= require(!source.contains(QStringLiteral("BusySpinner")),
                         QStringLiteral("app source no longer contains BusySpinner: ") + path);
        clean &= require(!source.contains(QStringLiteral("tickOutlineBusySpinner")),
                         QStringLiteral("app source no longer contains the hidden spinner tick: ")
                             + path);
        clean &= require(!source.contains(QStringLiteral("HudFontSettings")),
                         QStringLiteral("app source no longer contains the Widget HUD-font adapter: ")
                             + path);
        clean &= require(!source.contains(QStringLiteral("FlowLayout")),
                         QStringLiteral("app source no longer contains the unused FlowLayout helper: ")
                             + path);
        clean &= require(!source.contains(QStringLiteral("EditableValueLabel")),
                         QStringLiteral("app source no longer contains the retired editable value label: ")
                             + path);
    }
    return clean;
}

}  // namespace

int main()
{
    const QString cmake = readFile(QStringLiteral("CMakeLists.txt"));
    bool ok = require(!cmake.isEmpty(), QStringLiteral("CMakeLists.txt is readable"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/BusySpinner.h")),
                  QStringLiteral("MiaCode no longer lists BusySpinner.h"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/BusySpinner.cpp")),
                  QStringLiteral("MiaCode no longer lists BusySpinner.cpp"));
    ok &= require(!cmake.contains(QStringLiteral("src/tools/video_export/HudFontSettings.h")),
                  QStringLiteral("MiaCode no longer lists HudFontSettings.h"));
    ok &= require(!cmake.contains(QStringLiteral("src/tools/video_export/HudFontSettings.cpp")),
                  QStringLiteral("MiaCode no longer lists HudFontSettings.cpp"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/FlowLayout.h")),
                  QStringLiteral("MiaCode no longer lists FlowLayout.h"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/FlowLayout.cpp")),
                  QStringLiteral("MiaCode no longer lists FlowLayout.cpp"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/EditableValueLabel.h")),
                  QStringLiteral("MiaCode no longer lists EditableValueLabel.h"));
    ok &= require(!cmake.contains(QStringLiteral("src/app/ui/EditableValueLabel.cpp")),
                  QStringLiteral("MiaCode no longer lists EditableValueLabel.cpp"));
    ok &= require(cmake.contains(QStringLiteral("src/tools/video_export/FontLibrary.h"))
                      && cmake.contains(QStringLiteral("src/tools/video_export/FontLibrary.cpp")),
                  QStringLiteral("the non-Widget FontLibrary remains in MiaCode"));
    ok &= require(cmake.contains(QStringLiteral("src/core/scene/PreviewHudState.h"))
                      && cmake.contains(QStringLiteral("src/core/scene/PreviewHudState.cpp")),
                  QStringLiteral("the HUD render-state service remains in MiaCode"));
    const QString qmlExportSession = readFile(
        QStringLiteral("src/app/qml_ui/export/QmlExportSession.cpp"));
    const QString qmlPreviewSettings = readFile(
        QStringLiteral("src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp"));
    const QString qmlEditableValue = readFile(
        QStringLiteral("src/app/qml_ui/components/EditableValue.qml"));
    ok &= require(qmlExportSession.contains(QStringLiteral("setPreviewHudCustomFontPath"))
                      && qmlPreviewSettings.contains(QStringLiteral("setPreviewHudCustomFontPath")),
                  QStringLiteral("QML export and preview settings retain the HUD update path"));
    ok &= require(qmlEditableValue.contains(QStringLiteral("signal committed"))
                      && qmlEditableValue.contains(QStringLiteral("onDoubleClicked")),
                  QStringLiteral("QML retains the editable value replacement"));
    const QStringList deletedFiles = {
        QStringLiteral("src/app/ui/BusySpinner.h"),
        QStringLiteral("src/app/ui/BusySpinner.cpp"),
        QStringLiteral("src/tools/video_export/HudFontSettings.h"),
        QStringLiteral("src/tools/video_export/HudFontSettings.cpp"),
        QStringLiteral("src/app/ui/FlowLayout.h"),
        QStringLiteral("src/app/ui/FlowLayout.cpp"),
        QStringLiteral("src/app/ui/EditableValueLabel.h"),
        QStringLiteral("src/app/ui/EditableValueLabel.cpp"),
    };
    for (const QString& deletedFile : deletedFiles) {
        ok &= require(!QFileInfo::exists(QStringLiteral(MIACODE_SOURCE_ROOT)
                                          + QLatin1Char('/') + deletedFile),
                      QStringLiteral("deleted stage-4 residue is absent: ") + deletedFile);
    }
    ok &= appSourceTreeContainsLegacyResidue();
    return ok ? 0 : 1;
}
