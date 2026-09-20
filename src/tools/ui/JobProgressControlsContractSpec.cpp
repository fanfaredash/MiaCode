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
    ok &= require(source.contains(QStringLiteral("wrapMode: Text.WordWrap")),
                  QStringLiteral("generic progress text keeps word wrapping"), out);
    return ok;
}

}  // namespace

int main()
{
    QTextStream out(stdout);
    const bool ok = verifyIconButton(out) && verifyProgressOverlay(out);
    out << (ok ? "job_progress_controls_contract_spec ok\n"
               : "job_progress_controls_contract_spec failed\n");
    return ok ? 0 : 1;
}
