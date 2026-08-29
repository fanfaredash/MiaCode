#include "app/qml_ui/ChartTransformCommands.h"
#include "app/qml_ui/QmlShortcutCommands.h"
#include "app/qml_ui/QmlShortcutModel.h"
#include "ShortcutRegistry.h"

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

    // The bound ids and the dispatch table are now derived from one another, so
    // an id without a handler is unrepresentable — what can still drift is the
    // registry's hand-written editable-id list. A transform the registry offers
    // for rebinding but the table does not know is a shortcut the user can
    // customize and never fire; one the table knows but the registry does not
    // is a command with no configurable binding.
    QSet<QString> registryTransformIds;
    for (const auto& definition : ShortcutRegistry::instance().editableShortcuts()) {
        if (definition.id.startsWith(QStringLiteral("transform."))) {
            registryTransformIds.insert(definition.id);
        }
    }
    const QSet<QString> tableIds(boundIds.cbegin(), boundIds.cend());
    const QStringList registryOnly = QStringList(
        (registryTransformIds - tableIds).values());
    const QStringList tableOnly = QStringList((tableIds - registryTransformIds).values());
    if (!registryOnly.isEmpty()) out << "  rebindable but undispatched: " << registryOnly.join(QStringLiteral(", ")) << '\n';
    if (!tableOnly.isEmpty()) out << "  dispatched but not rebindable: " << tableOnly.join(QStringLiteral(", ")) << '\n';
    expect(!registryTransformIds.isEmpty() && registryOnly.isEmpty() && tableOnly.isEmpty(),
           QStringLiteral("the rebindable transform ids and the dispatch table describe the same set"), out, &failed);

    // And the table dispatches to a real transform, not an empty slot: a mirror
    // over one bar renumbers every button.
    int mirrored = 0;
    const QString sample = QStringLiteral("{8}1,2,3,4,");
    QString mirrorResult;
    for (const miacode::qml_ui::ChartTransformSpec& spec : miacode::qml_ui::chartTransformSpecs()) {
        if (spec.id == QStringLiteral("transform.mirror_lr") && spec.apply) {
            mirrorResult = spec.apply(sample, QString(), &mirrored);
        }
    }
    expect(mirrored > 0 && mirrorResult != sample,
           QStringLiteral("a table entry reaches the shared chart transform"), out, &failed);

    if (failed != 0) {
        out << "QmlShortcutBinding spec failed: " << failed << '\n';
        return 1;
    }
    out << "QmlShortcutBinding spec passed.\n";
    return 0;
}
