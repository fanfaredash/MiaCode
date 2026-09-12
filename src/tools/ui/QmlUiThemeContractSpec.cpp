// Contract regression for how a theme change reaches the shell.
//
// Reported symptom: changing 主题 in 偏好设置 only repainted the timeline.
//
// Cause: there are two ways the resolved theme can change, and only one of them
// told QML about it.
//
//   * the OS changes its colour scheme -> QStyleHints::colorSchemeChanged ->
//     WorkbenchSettings::reloadTheme() -> darkTheme_ updated -> themeChanged()
//     -> every QML component rebinds.
//   * the user picks a theme -> WorkbenchSettings::setThemeToken() ->
//     PreferenceDocument::setPreferredTheme() ... and nothing else. PreferenceDocument only stores and
//     persists the preference; it notifies no one.
//
// The timeline still followed the user's choice because it is a C++ QSG item
// that reads UiTheme::colors() — derived live from the stored preference — on
// its next repaint. It never goes through the QML darkTheme property. So the one
// surface that bypassed the notification was the only one that updated.
//
// This is a source contract rather than a behavioural one: WorkbenchSettings pulls
// in MainWindowShared for its editor font metrics, so it cannot be linked into a
// Core-only spec. The check is narrow on purpose — it pins that the
// user-initiated path ends in the same notification the OS-initiated path uses.

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
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

// The body of one function, from its definition to the next one.
QString functionBody(const QString& source, const QString& signature)
{
    const qsizetype start = source.indexOf(signature);
    if (start < 0) {
        return QString();
    }
    const qsizetype end = source.indexOf(QStringLiteral("\n}\n"), start);
    return source.mid(start, (end >= 0 ? end : source.size()) - start);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);

    const QString settings = readFile(QStringLiteral("src/app/ui/layout/WorkbenchSettings.cpp"));
    const QString settingsHeader = readFile(QStringLiteral("src/app/ui/layout/WorkbenchSettings.h"));
    const QString themeQml = readFile(QStringLiteral("src/app/ui/theme/Theme.qml"));
    const QString preferencesStore = readFile(QStringLiteral("src/app/ui/preferences/PreferenceDocument.cpp"));
    bool ok = require(!settings.isEmpty() && !settingsHeader.isEmpty() && !themeQml.isEmpty()
                          && !preferencesStore.isEmpty(),
                      QStringLiteral("the settings, Theme.qml and PreferenceDocument sources are readable"), err);
    if (!ok) {
        return 1;
    }

    // The premise: PreferenceDocument stores the preference and notifies nobody. If that
    // ever changes, this whole contract can be reconsidered — but while it
    // holds, every writer of the preference owes the shell a notification.
    const QString setPreferredTheme =
        functionBody(preferencesStore, QStringLiteral("void PreferenceDocument::setPreferredTheme("));
    ok &= require(!setPreferredTheme.isEmpty()
                      && !setPreferredTheme.contains(QStringLiteral("emit ")),
                  QStringLiteral("PreferenceDocument::setPreferredTheme still only stores and persists, "
                                 "which is why its callers must publish the change"), err);

    // The OS-initiated path.
    ok &= require(settings.contains(QStringLiteral("QStyleHints::colorSchemeChanged"))
                      && settings.contains(QStringLiteral("&WorkbenchSettings::reloadTheme")),
                  QStringLiteral("an OS colour-scheme change still reloads the theme"), err);

    const QString reloadTheme =
        functionBody(settings, QStringLiteral("void WorkbenchSettings::reloadTheme()"));
    ok &= require(reloadTheme.contains(QStringLiteral("darkTheme_ = next"))
                      && reloadTheme.contains(QStringLiteral("publishedThemeToken_ = nextToken"))
                      && reloadTheme.contains(QStringLiteral("emit themeChanged()")),
                  QStringLiteral("reloadTheme updates the bound value and publishes it"), err);

    // Dark and 旧版 are both dark appearances. The boolean alone cannot tell
    // QML to rebind palettes, so the stored token must be part of the publish.
    ok &= require(reloadTheme.contains(QStringLiteral("publishedThemeToken_ == nextToken")),
                  QStringLiteral("reloadTheme also publishes when the palette token changes "
                                 "without flipping darkTheme"), err);
    ok &= require(settingsHeader.contains(QStringLiteral("themeToken READ themeToken NOTIFY themeChanged")),
                  QStringLiteral("themeToken is bindable so QML can select the 旧版 palette live"), err);

    ok &= require(themeQml.contains(QStringLiteral("legacyColors"))
                      && themeQml.contains(QStringLiteral("themeToken === \"legacy\"")),
                  QStringLiteral("Theme.qml keeps 旧版 as a standalone palette, not a dark/light alias"), err);

    // The user-initiated path has to end in the same place. Without this the
    // preference is stored, the timeline repaints from it, and every QML
    // surface keeps the old palette until the next restart.
    const QString setThemeToken =
        functionBody(settings, QStringLiteral("void WorkbenchSettings::setThemeToken("));
    ok &= require(setThemeToken.contains(QStringLiteral("PreferenceDocument::setPreferredTheme(next)")),
                  QStringLiteral("picking a theme stores the preference"), err);
    ok &= require(setThemeToken.contains(QStringLiteral("reloadTheme()")),
                  QStringLiteral("picking a theme also publishes it — the user-initiated path "
                                 "must end in the same notification as the OS-initiated one, "
                                 "or only the timeline repaints"), err);

    if (ok) {
        QTextStream(stdout) << "qml_ui_theme_contract_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
