#include "app/ui/ThemeVariantResolver.h"

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
    return require(ThemeVariantResolver::resolve(UiText::ThemePreference::Light, Qt::ColorScheme::Dark)
                       == ThemeVariant::Light,
                   QStringLiteral("explicit light wins"), out)
        && require(ThemeVariantResolver::resolve(UiText::ThemePreference::Dark, Qt::ColorScheme::Light)
                       == ThemeVariant::Dark,
                   QStringLiteral("explicit dark wins"), out)
        && require(ThemeVariantResolver::resolve(UiText::ThemePreference::System, Qt::ColorScheme::Light)
                       == ThemeVariant::Light,
                   QStringLiteral("system light"), out)
        && require(ThemeVariantResolver::resolve(UiText::ThemePreference::System, Qt::ColorScheme::Dark)
                       == ThemeVariant::Dark,
                   QStringLiteral("system dark"), out)
        && require(ThemeVariantResolver::resolve(UiText::ThemePreference::System, Qt::ColorScheme::Unknown)
                       == ThemeVariant::Dark,
                   QStringLiteral("unknown system scheme falls back to dark"), out);
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
