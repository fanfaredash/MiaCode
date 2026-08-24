#include "app/qml_ui/QmlShortcutCommands.h"
#include "app/qml_ui/QmlShortcutModel.h"
#include "ShortcutRegistry.h"

#include <QFile>
#include <QGuiApplication>
#include <QKeySequence>
#include <QSet>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    out.flush();
    if (!condition) ++*failed;
    return condition;
}

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    miacode::qml_ui::QmlShortcutModel model;
    const QStringList boundIds = miacode::qml_ui::qmlShortcutCommandIds();

    // Every id v2 binds must be one the registry actually knows, or the QML
    // Shortcut resolves to an empty sequence and is silently inert — the exact
    // shape of the gap that left chart transforms unreachable in v2.
    QStringList unknown;
    for (const QString& id : boundIds) {
        if (model.sequence(id).isEmpty()) unknown.append(id);
    }
    if (!unknown.isEmpty()) out << "  unknown ids: " << unknown.join(QStringLiteral(", ")) << '\n';
    expect(unknown.isEmpty(),
           QStringLiteral("every bound command id resolves to a real registry sequence"), out, &failed);

    const QSet<QString> uniqueIds(boundIds.cbegin(), boundIds.cend());
    expect(!boundIds.isEmpty() && boundIds.size() == uniqueIds.size(),
           QStringLiteral("the bound command table has no duplicate ids"), out, &failed);

    // Portable text is what QML's Shortcut.sequence parses; native text is what
    // a menu row shows. They are different strings on macOS and must not be
    // swapped at the call site.
    const QString portable = model.sequence(QStringLiteral("transform.mirror_lr"));
    const QString native = model.displayText(QStringLiteral("transform.mirror_lr"));
    expect(!portable.isEmpty() && !native.isEmpty()
               && portable == QKeySequence(portable, QKeySequence::PortableText)
                                  .toString(QKeySequence::PortableText),
           QStringLiteral("a binding is exposed in both portable and display spellings"), out, &failed);

    // An unknown id falls back to what the caller supplied rather than binding
    // something arbitrary, and stays empty when nothing was supplied.
    expect(model.sequence(QStringLiteral("no.such.command")).isEmpty()
               && model.sequence(QStringLiteral("no.such.command"), QStringLiteral("Ctrl+B"))
                      == QStringLiteral("Ctrl+B"),
           QStringLiteral("an unknown id stays inert unless the caller supplies a fallback"),
           out, &failed);

    expect(!model.standardDisplayText(static_cast<int>(QKeySequence::Save)).isEmpty(),
           QStringLiteral("standard-key menu rows can render their platform spelling"), out, &failed);

    // The dispatcher must own every id the QML side binds, or the shortcut
    // fires into nothing.
    const QString dispatcher =
        readSource(QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp"));
    QStringList undispatched;
    for (const QString& id : boundIds) {
        if (!dispatcher.contains(QStringLiteral("\"%1\"").arg(id))) undispatched.append(id);
    }
    if (!undispatched.isEmpty()) {
        out << "  ids with no dispatcher entry: " << undispatched.join(QStringLiteral(", ")) << '\n';
    }
    expect(!dispatcher.isEmpty() && undispatched.isEmpty(),
           QStringLiteral("every bound command id has a backend dispatcher entry"), out, &failed);

    if (failed != 0) {
        out << "QmlShortcutBinding spec failed: " << failed << '\n';
        return 1;
    }
    out << "QmlShortcutBinding spec passed.\n";
    return 0;
}
