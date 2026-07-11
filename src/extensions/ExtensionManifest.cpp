#include "ExtensionManifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace miacode::extensions {
namespace {

QString readRequiredString(const QJsonObject& object, const QString& key, QStringList* errors)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        if (errors != nullptr) {
            errors->append(QStringLiteral("Missing or invalid '%1'.").arg(key));
        }
        return QString();
    }
    return value.toString().trimmed();
}

QStringList readStringArray(const QJsonObject& object, const QString& key)
{
    QStringList result;
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString() && !item.toString().trimmed().isEmpty()) {
            result.append(item.toString().trimmed());
        }
    }
    return result;
}

QJsonObject manifestObjectFromPackageJson(const QJsonObject& packageRoot)
{
    const QJsonValue nested = packageRoot.value(QStringLiteral("miacodeExtension"));
    if (nested.isObject()) {
        QJsonObject object = nested.toObject();
        for (const QString& key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("publisher"), QStringLiteral("main")}) {
            if (!object.contains(key) && packageRoot.contains(key)) {
                object.insert(key, packageRoot.value(key));
            }
        }
        return object;
    }
    return packageRoot;
}

QString manifestPathForRoot(const QString& extensionRootPath)
{
    const QDir dir(extensionRootPath);
    const QString miacodeManifest = dir.filePath(QStringLiteral("miacode-extension.json"));
    if (QFileInfo::exists(miacodeManifest)) {
        return miacodeManifest;
    }
    const QString packageManifest = dir.filePath(QStringLiteral("package.json"));
    if (QFileInfo::exists(packageManifest)) {
        return packageManifest;
    }
    return QString();
}

bool isSupportedMenuLocation(const QString& location)
{
    return location == QStringLiteral("tools/menu")
        || location == QStringLiteral("menubar/beforeHelp");
}

bool isSupportedPermission(const QString& permission)
{
    static const QSet<QString> allowed{
        QStringLiteral("app.read"),
        QStringLiteral("ui.message"),
        QStringLiteral("ui.prompt"),
        QStringLiteral("ui.status"),
        QStringLiteral("ui.contribute"),
        QStringLiteral("events.subscribe"),
        QStringLiteral("providers.register"),
        QStringLiteral("workspace.read"),
        QStringLiteral("workspace.write"),
        QStringLiteral("document.edit"),
        QStringLiteral("editor.read"),
        QStringLiteral("editor.edit"),
        QStringLiteral("commands.read"),
        QStringLiteral("commands.execute"),
        QStringLiteral("diagnostics.run"),
        QStringLiteral("analysis.run"),
        QStringLiteral("timeline.read"),
        QStringLiteral("timeline.control"),
        QStringLiteral("preview.read"),
        QStringLiteral("preview.control"),
        QStringLiteral("resources.read"),
        QStringLiteral("resources.write"),
        QStringLiteral("export.read"),
        QStringLiteral("export.write"),
        QStringLiteral("tasks.run"),
        QStringLiteral("logs.read"),
        QStringLiteral("logs.write"),
        QStringLiteral("filesystem.read"),
        QStringLiteral("filesystem.write"),
        QStringLiteral("network.fetch"),
        QStringLiteral("settings.read"),
        QStringLiteral("settings.write"),
        QStringLiteral("extensions.manage"),
        QStringLiteral("app.lifecycle"),
        QStringLiteral("backup.read"),
        QStringLiteral("backup.write"),
        QStringLiteral("clipboard.read"),
        QStringLiteral("clipboard.write"),
        QStringLiteral("filesystem.unsafe"),
        QStringLiteral("network.unsafe"),
        QStringLiteral("process.manage"),
        QStringLiteral("shell.execute"),
        QStringLiteral("native.unsafe"),
        QStringLiteral("internal.inspect"),
        QStringLiteral("internal.call"),
        QStringLiteral("internal.raw"),
        QStringLiteral("renderer.raw"),
        QStringLiteral("export.raw"),
        QStringLiteral("security.override"),
        QStringLiteral("updates.modify"),
    };
    return allowed.contains(permission);
}

}  // namespace

QString ExtensionManifest::qualifiedId() const
{
    return publisher.trimmed().isEmpty() ? id : QStringLiteral("%1.%2").arg(publisher, id);
}

bool ExtensionManifest::needsJavaScriptRuntime() const
{
    return !main.trimmed().isEmpty()
        && (!commands.isEmpty() || !activationEvents.isEmpty());
}

QJsonObject ExtensionManifest::toRuntimeJson() const
{
    QJsonArray activationArray;
    for (const QString& event : activationEvents) {
        activationArray.append(event);
    }
    QJsonArray commandArray;
    for (const ExtensionCommandContribution& command : commands) {
        commandArray.append(command.id);
    }
    QJsonArray permissionArray;
    for (const QString& permission : permissions) {
        permissionArray.append(permission);
    }
    QJsonObject object{
        {QStringLiteral("id"), id},
        {QStringLiteral("qualifiedId"), qualifiedId()},
        {QStringLiteral("name"), name},
        {QStringLiteral("version"), version},
        {QStringLiteral("publisher"), publisher},
        {QStringLiteral("main"), main.trimmed().isEmpty() ? QString() : QDir(rootPath).absoluteFilePath(main)},
        {QStringLiteral("rootPath"), rootPath},
        {QStringLiteral("activationEvents"), activationArray},
        {QStringLiteral("commands"), commandArray},
        {QStringLiteral("permissions"), permissionArray},
    };
    return object;
}

bool isValidExtensionIdentifier(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9._-]{1,95}$"));
    return pattern.match(value.trimmed()).hasMatch();
}

ExtensionManifestParseResult loadExtensionManifest(const QString& extensionRootPath)
{
    ExtensionManifestParseResult result;
    const QString cleanRoot = QDir::cleanPath(extensionRootPath);
    const QString manifestPath = manifestPathForRoot(cleanRoot);
    if (manifestPath.isEmpty()) {
        result.error = QStringLiteral("No miacode-extension.json or package.json found.");
        return result;
    }

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot read manifest: %1").arg(file.errorString());
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("Manifest JSON parse failed: %1").arg(parseError.errorString());
        return result;
    }

    const QJsonObject rootObject = manifestObjectFromPackageJson(document.object());
    QStringList errors;
    ExtensionManifest manifest;
    manifest.id = readRequiredString(rootObject, QStringLiteral("id"), &errors);
    manifest.name = readRequiredString(rootObject, QStringLiteral("name"), &errors);
    manifest.version = readRequiredString(rootObject, QStringLiteral("version"), &errors);
    manifest.publisher = readRequiredString(rootObject, QStringLiteral("publisher"), &errors);
    manifest.main = rootObject.value(QStringLiteral("main")).toString().trimmed();
    manifest.rootPath = QFileInfo(cleanRoot).absoluteFilePath();
    manifest.manifestPath = QFileInfo(manifestPath).absoluteFilePath();

    if (!manifest.id.isEmpty() && !isValidExtensionIdentifier(manifest.id)) {
        errors.append(QStringLiteral("Invalid 'id'. Use lowercase letters, numbers, '.', '_' or '-'."));
    }
    if (!manifest.publisher.isEmpty() && !isValidExtensionIdentifier(manifest.publisher)) {
        errors.append(QStringLiteral("Invalid 'publisher'. Use lowercase letters, numbers, '.', '_' or '-'."));
    }
    if (!manifest.main.isEmpty() && !QFileInfo::exists(QDir(manifest.rootPath).absoluteFilePath(manifest.main))) {
        errors.append(QStringLiteral("Main entry does not exist: %1").arg(manifest.main));
    }

    const QJsonObject engines = rootObject.value(QStringLiteral("engines")).toObject();
    manifest.engineMiaCode = engines.value(QStringLiteral("miacode")).toString().trimmed();
    if (manifest.engineMiaCode.isEmpty()) {
        errors.append(QStringLiteral("Missing 'engines.miacode'."));
    }
    manifest.activationEvents = readStringArray(rootObject, QStringLiteral("activationEvents"));
    manifest.permissions = readStringArray(rootObject, QStringLiteral("permissions"));
    manifest.permissions.removeDuplicates();
    for (const QString& permission : manifest.permissions) {
        if (!isSupportedPermission(permission)) {
            errors.append(QStringLiteral("Unsupported permission '%1'.").arg(permission));
        }
    }

    const QJsonObject contributes = rootObject.value(QStringLiteral("contributes")).toObject();
    const QJsonArray commandArray = contributes.value(QStringLiteral("commands")).toArray();
    QSet<QString> commandIds;
    for (const QJsonValue& value : commandArray) {
        const QJsonObject commandObject = value.toObject();
        ExtensionCommandContribution command;
        command.id = commandObject.value(QStringLiteral("command")).toString().trimmed();
        command.title = commandObject.value(QStringLiteral("title")).toString().trimmed();
        command.category = commandObject.value(QStringLiteral("category")).toString().trimmed();
        if (command.id.isEmpty() || command.title.isEmpty()) {
            errors.append(QStringLiteral("Each contributes.commands item needs 'command' and 'title'."));
            continue;
        }
        if (!isValidExtensionIdentifier(command.id)) {
            errors.append(QStringLiteral("Invalid command id '%1'.").arg(command.id));
            continue;
        }
        if (commandIds.contains(command.id)) {
            errors.append(QStringLiteral("Duplicate command id '%1'.").arg(command.id));
            continue;
        }
        commandIds.insert(command.id);
        manifest.commands.append(command);
    }

    const QJsonObject menusObject = contributes.value(QStringLiteral("menus")).toObject();
    for (auto it = menusObject.constBegin(); it != menusObject.constEnd(); ++it) {
        const QString location = it.key().trimmed();
        if (!isSupportedMenuLocation(location)) {
            errors.append(QStringLiteral("Unsupported menu location '%1'.").arg(location));
            continue;
        }
        const QJsonArray items = it.value().toArray();
        for (const QJsonValue& value : items) {
            const QString command = value.toObject().value(QStringLiteral("command")).toString().trimmed();
            if (command.isEmpty()) {
                errors.append(QStringLiteral("Menu contribution in '%1' is missing command.").arg(location));
                continue;
            }
            if (!commandIds.contains(command)) {
                errors.append(QStringLiteral("Menu contribution references unknown command '%1'.").arg(command));
                continue;
            }
            manifest.menus.append(ExtensionMenuContribution{location, command});
        }
    }

    const QJsonArray languageArray = contributes.value(QStringLiteral("languages")).toArray();
    QSet<QString> languageIds;
    for (const QJsonValue& value : languageArray) {
        const QJsonObject languageObject = value.toObject();
        ExtensionLanguageContribution language;
        language.id = languageObject.value(QStringLiteral("id")).toString().trimmed().toLower();
        language.label = languageObject.value(QStringLiteral("label")).toString().trimmed();
        language.translations = languageObject.value(QStringLiteral("translations")).toString().trimmed();
        if (language.id.isEmpty() || language.label.isEmpty() || language.translations.isEmpty()) {
            errors.append(QStringLiteral("Each contributes.languages item needs 'id', 'label', and 'translations'."));
            continue;
        }
        if (!isValidExtensionIdentifier(language.id)) {
            errors.append(QStringLiteral("Invalid language id '%1'.").arg(language.id));
            continue;
        }
        if (languageIds.contains(language.id)) {
            errors.append(QStringLiteral("Duplicate language id '%1'.").arg(language.id));
            continue;
        }
        const QString translationsPath = QDir(manifest.rootPath).absoluteFilePath(language.translations);
        if (!QFileInfo::exists(translationsPath)) {
            errors.append(QStringLiteral("Language translations file does not exist: %1").arg(language.translations));
            continue;
        }
        languageIds.insert(language.id);
        manifest.languages.append(language);
    }

    if (manifest.main.isEmpty() && (!manifest.commands.isEmpty() || !manifest.activationEvents.isEmpty())) {
        errors.append(QStringLiteral("Command or activated extensions require a 'main' entry."));
    }
    if (manifest.main.isEmpty() && manifest.languages.isEmpty()) {
        errors.append(QStringLiteral("Missing 'main'. Pure data extensions must contribute at least one language."));
    }

    if (!errors.isEmpty()) {
        result.error = errors.join(QStringLiteral(" "));
        return result;
    }
    result.ok = true;
    result.manifest = manifest;
    return result;
}

QStringList defaultExtensionSearchPaths()
{
    QStringList paths;
    const QString userDir = userExtensionDirectoryPath();
    if (!userDir.isEmpty()) {
        paths.append(userDir);
    }
    const QString envPaths = qEnvironmentVariable("MIACODE_EXTENSION_DEV_PATHS");
    for (const QString& path : envPaths.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
        paths.append(QDir::cleanPath(path));
    }
    const QDir appDir(QCoreApplication::applicationDirPath());
    paths.append(appDir.filePath(QStringLiteral("extensions-dev")));
    paths.removeDuplicates();
    return paths;
}

QString userExtensionDirectoryPath()
{
    const QString appPath = QCoreApplication::applicationDirPath();
    if (appPath.isEmpty()) {
        return QString();
    }

    QDir installRoot(appPath);
    if (installRoot.dirName().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0) {
        installRoot.cdUp();
    }
    return installRoot.filePath(QStringLiteral("extensions"));
}

}  // namespace miacode::extensions
