#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#define MIACODE_SOURCE_ROOT "."
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) {
        out << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString readFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool testArchiveBoundary(QTextStream& out)
{
    const QString cmake = readFile(QStringLiteral("CMakeLists.txt"));
    const QString windowsPackage = readFile(QStringLiteral("scripts/build/package-win.ps1"));
    const QString uiText = readFile(QStringLiteral("src/app/ui/UiText.cpp"));
    const QString fixture = readFile(QStringLiteral("tools/extensions/extension-api-registry.json"));
    return require(!cmake.contains(QStringLiteral("ExtensionManager.cpp"))
                     && !cmake.contains(QStringLiteral("EmbeddedExtensionRuntime.cpp"))
                     && !cmake.contains(QStringLiteral("ExtensionOpenBridge.cpp")),
                 QStringLiteral("product CMake has no extension host sources"), out)
        && require(!windowsPackage.contains(QStringLiteral("extensions\\README.md")),
                   QStringLiteral("Windows package has no extension deployment"), out)
        && require(cmake.contains(QStringLiteral("TARGET_FILE_DIR:MiaCode>/extensions")),
                   QStringLiteral("product build removes stale extension payload"), out)
        && require(!uiText.contains(QStringLiteral("scanExtensionLanguagePacks")),
                   QStringLiteral("UiText has no external extension language scan"), out)
        && require(!fixture.isEmpty(), QStringLiteral("archive API fixture exists"), out);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const bool ok = testArchiveBoundary(out);
    if (ok) {
        out << "extension_product_boundary_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
