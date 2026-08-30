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

bool verifyAdjustMenuContract(QTextStream& err)
{
    const QString menu = readSource(QStringLiteral("src/app/qml_ui/chrome/MainMenu.qml"));
    const QString item = readSource(QStringLiteral("src/app/qml_ui/components/AppMenuItem.qml"));
    const QString commands = readSource(QStringLiteral("src/app/qml_ui/ChartTransformCommands.h"));
    const QString shortcuts = readSource(QStringLiteral("resources/shortcuts.json"));
    bool ok = require(!menu.isEmpty() && !item.isEmpty() && !commands.isEmpty() && !shortcuts.isEmpty(),
                      QStringLiteral("the Adjust-menu sources are readable"), err);
    if (!ok) {
        return false;
    }

    const int adjustStart = menu.indexOf(QStringLiteral("id: adjustMenu"));
    const int previewStart = menu.indexOf(QStringLiteral("id: previewMenu"));
    const QString adjust = adjustStart >= 0 && previewStart > adjustStart
        ? menu.mid(adjustStart, previewStart - adjustStart)
        : QString();
    ok &= require(!adjust.isEmpty(),
                  QStringLiteral("the top-level Adjust menu has an isolated source block"), err);
    ok &= require(!adjust.contains(QStringLiteral("delegate: AppMenuAction")),
                  QStringLiteral("dynamic Adjust rows do not use non-visual Action delegates"), err);
    ok &= require(adjust.count(QStringLiteral("delegate: AppMenuItem")) == 4,
                  QStringLiteral("all four Adjust-menu Repeaters create visual menu rows"), err);
    ok &= require(adjust.contains(QStringLiteral("shortcutText: root.shortcuts.displayText(modelData.id)"))
                      && adjust.contains(QStringLiteral("objectName: \"adjustTransform_\" + modelData.id")),
                  QStringLiteral("every dynamic transform row receives its shortcut spelling and identity"), err);
    ok &= require(item.contains(QStringLiteral("property string shortcutText"))
                      && item.contains(QStringLiteral("root.shortcutText.length > 0")),
                  QStringLiteral("AppMenuItem renders shortcuts supplied by a dynamic visual row"), err);
    ok &= require(commands.contains(QStringLiteral("transform.mirror_lr"))
                      && shortcuts.contains(QStringLiteral("\"transform.mirror_lr\""))
                      && shortcuts.contains(QStringLiteral("\"default\": \"Ctrl+J\"")),
                  QStringLiteral("the shared transform table still defines Ctrl+J for left/right mirror"), err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    const bool ok = verifyAdjustMenuContract(err);
    if (ok) {
        QTextStream out(stdout);
        out << "qml_main_menu_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
