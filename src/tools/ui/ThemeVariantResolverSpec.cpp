#include "app/ui/theme/ThemeVariantResolver.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) out << "FAIL: " << message << Qt::endl;
    return condition;
}

bool testDeterministicMappings(QTextStream& out)
{
    using miacode::ui::ThemeVariant;
    using miacode::ui::ThemeVariantResolver;
    return require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::Light, Qt::ColorScheme::Dark)
                       == ThemeVariant::Light,
                   QStringLiteral("explicit light wins"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::Dark, Qt::ColorScheme::Light)
                       == ThemeVariant::Dark,
                   QStringLiteral("explicit dark wins"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::System, Qt::ColorScheme::Light)
                       == ThemeVariant::Light,
                   QStringLiteral("system light"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::System, Qt::ColorScheme::Dark)
                       == ThemeVariant::Dark,
                   QStringLiteral("system dark"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::System, Qt::ColorScheme::Unknown)
                       == ThemeVariant::Dark,
                   QStringLiteral("unknown system scheme falls back to dark"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::Legacy, Qt::ColorScheme::Light)
                       == ThemeVariant::Dark,
                   QStringLiteral("legacy stays dark against a light system scheme"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::Legacy, Qt::ColorScheme::Dark)
                       == ThemeVariant::Dark,
                   QStringLiteral("legacy stays dark against a dark system scheme"), out)
        && require(ThemeVariantResolver::resolve(PreferenceDocument::ThemePreference::Legacy, Qt::ColorScheme::Unknown)
                       == ThemeVariant::Dark,
                   QStringLiteral("legacy stays dark when the system scheme is unknown"), out);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const bool ok = testDeterministicMappings(out);
    if (ok) out << "theme_variant_resolver_spec ok" << Qt::endl;
    return ok ? 0 : 1;
}
