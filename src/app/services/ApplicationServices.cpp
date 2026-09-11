#include "ApplicationServices.h"

#include "app/ui/preferences/PreferenceDocument.h"

namespace miacode {

SimaiNativeValidationLocale uiValidationLocale()
{
    const QString token = PreferenceDocument::resolvedLanguageToken();
    if (token.startsWith(QStringLiteral("zh"))) {
        return SimaiNativeValidationLocale::Chinese;
    }
    if (token.startsWith(QStringLiteral("ja"))) {
        return SimaiNativeValidationLocale::Japanese;
    }
    return SimaiNativeValidationLocale::English;
}

ApplicationServices::ApplicationServices(QObject* parent)
    : QObject(parent)
    , workspace_(this)
    , files_(workspace_)
    , validationLocale_(uiValidationLocale())
    , analysis_(workspace_, validationLocale_, {}, -1.0, this)
    , editorSync_(this)
    , chartDropImport_(this)
    , uiRequests_(this)
    , jobProgress_(this)
    , previewAppearance_(this)
    , shellNotifications_(this)
{
}

}  // namespace miacode
