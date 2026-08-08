#include "ExtensionManager.h"

#include <utility>

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "ExtensionOpenBridge.h"
#include "UiTheme.h"
#include "UiText.h"

namespace miacode::extensions {
namespace {

constexpr int kDevtoolsParamsPreviewMaxDepth = 4;
constexpr int kDevtoolsParamsPreviewMaxObjectMembers = 24;
constexpr int kDevtoolsParamsPreviewMaxArrayItems = 12;
constexpr int kDevtoolsParamsPreviewMaxStringChars = 512;
constexpr int kDevtoolsParamsPreviewMaxJsonChars = 2048;

bool activationIncludesStartup(const ExtensionManifest& manifest)
{
    return manifest.activationEvents.contains(QStringLiteral("onStartupFinished"))
        || manifest.activationEvents.contains(QStringLiteral("*"));
}

QString displayLabelForCommand(const ExtensionCommandContribution& command)
{
    if (command.category.trimmed().isEmpty()) {
        return command.title;
    }
    return QStringLiteral("%1: %2").arg(command.category, command.title);
}

QString localizedText(const QString& key, const QString& fallback)
{
    const QString translated = UiText::text(key);
    return translated.isEmpty() ? fallback : translated;
}

QJsonValue devtoolsPreviewValue(const QJsonValue& value, int depth)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.size() <= kDevtoolsParamsPreviewMaxStringChars) {
            return text;
        }
        return QStringLiteral("%1...<truncated %2 chars>")
            .arg(text.left(kDevtoolsParamsPreviewMaxStringChars))
            .arg(text.size() - kDevtoolsParamsPreviewMaxStringChars);
    }
    if (depth <= 0) {
        if (value.isArray()) {
            return QStringLiteral("<array>");
        }
        if (value.isObject()) {
            return QStringLiteral("<object>");
        }
        return value;
    }
    if (value.isArray()) {
        const QJsonArray source = value.toArray();
        QJsonArray preview;
        const int count = qMin(source.size(), kDevtoolsParamsPreviewMaxArrayItems);
        for (int index = 0; index < count; ++index) {
            preview.append(devtoolsPreviewValue(source.at(index), depth - 1));
        }
        if (source.size() > count) {
            preview.append(QJsonObject{
                {QStringLiteral("_truncatedItems"), source.size() - count},
            });
        }
        return preview;
    }
    if (value.isObject()) {
        const QJsonObject source = value.toObject();
        QJsonObject preview;
        int count = 0;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
            if (count >= kDevtoolsParamsPreviewMaxObjectMembers) {
                preview.insert(QStringLiteral("_truncatedKeys"), source.size() - count);
                break;
            }
            preview.insert(it.key(), devtoolsPreviewValue(it.value(), depth - 1));
            ++count;
        }
        return preview;
    }
    return value;
}

QString devtoolsParamsPreview(const QJsonObject& params)
{
    const QJsonObject preview = devtoolsPreviewValue(params, kDevtoolsParamsPreviewMaxDepth).toObject();
    QString text = QString::fromUtf8(QJsonDocument(preview).toJson(QJsonDocument::Compact));
    if (text.size() > kDevtoolsParamsPreviewMaxJsonChars) {
        text = QStringLiteral("%1...<truncated %2 chars>")
            .arg(text.left(kDevtoolsParamsPreviewMaxJsonChars))
            .arg(text.size() - kDevtoolsParamsPreviewMaxJsonChars);
    }
    return text;
}

QString compactJsonObject(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool jsonObjectBelongsToExtension(const QJsonObject& object, const QString& extensionId)
{
    if (extensionId.trimmed().isEmpty()) {
        return false;
    }
    return object.value(QStringLiteral("extensionId")).toString() == extensionId
        || object.value(QStringLiteral("ownerId")).toString() == extensionId;
}

bool isRawOrExperimentalCall(const QJsonObject& call)
{
    const QString method = call.value(QStringLiteral("method")).toString();
    const QString permission = call.value(QStringLiteral("permission")).toString();
    return method.startsWith(QStringLiteral("experimental/"))
        || method.startsWith(QStringLiteral("renderer/raw"))
        || method.startsWith(QStringLiteral("internal/raw"))
        || method.startsWith(QStringLiteral("export/raw"))
        || method.startsWith(QStringLiteral("native/"))
        || method.startsWith(QStringLiteral("security/"))
        || method.startsWith(QStringLiteral("updates/"))
        || permission.contains(QStringLiteral("raw"), Qt::CaseInsensitive)
        || permission.contains(QStringLiteral("experimental"), Qt::CaseInsensitive);
}

QJsonArray filterObjectsByExtension(const QJsonArray& values, const QString& extensionId)
{
    QJsonArray filtered;
    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        if (jsonObjectBelongsToExtension(object, extensionId)) {
            filtered.append(object);
        }
    }
    return filtered;
}

QJsonArray filterContributionsByKindPrefix(const QJsonArray& values, const QString& prefix)
{
    QJsonArray filtered;
    for (const QJsonValue& value : values) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("kind")).toString().startsWith(prefix)) {
            filtered.append(object);
        }
    }
    return filtered;
}

QJsonObject disabledExtensionsObject()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    return root.value(QStringLiteral("extensions")).toObject()
        .value(QStringLiteral("disabled")).toObject();
}

bool isDisabledByUser(const QString& qualifiedId)
{
    return disabledExtensionsObject().value(qualifiedId).toBool(false);
}

void saveDisabledExtensionState(const QString& qualifiedId, bool disabled)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject extensions = root.value(QStringLiteral("extensions")).toObject();
    QJsonObject disabledObject = extensions.value(QStringLiteral("disabled")).toObject();
    if (disabled) {
        disabledObject.insert(qualifiedId, true);
    } else {
        disabledObject.remove(qualifiedId);
    }
    extensions.insert(QStringLiteral("disabled"), disabledObject);
    root.insert(QStringLiteral("extensions"), extensions);
    UiText::savePreferencesObject(root);
}

QString canonicalDirectoryPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool pathIsInsideDirectory(const QString& path, const QString& directory)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanDirectory = QDir::cleanPath(directory);
    return cleanPath.compare(cleanDirectory, Qt::CaseInsensitive) == 0
        || cleanPath.startsWith(cleanDirectory + QDir::separator(), Qt::CaseInsensitive)
        || cleanPath.startsWith(cleanDirectory + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool extensionLocalFilePath(
    const QString& extensionRootPath,
    const QString& resourcePath,
    QString* resolvedPath,
    QString* error)
{
    const QString trimmed = resourcePath.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    const QString root = canonicalDirectoryPath(extensionRootPath);
    const QFileInfo rawInfo(trimmed);
    const QString candidate = rawInfo.isAbsolute()
        ? trimmed
        : QDir(extensionRootPath).absoluteFilePath(trimmed);
    const QFileInfo candidateInfo(candidate);
    const QString canonicalFile = candidateInfo.canonicalFilePath();
    if (canonicalFile.isEmpty() || !candidateInfo.exists() || !candidateInfo.isFile()) {
        if (error != nullptr) {
            *error = QStringLiteral("Pet overlay resource does not exist or is not a file: %1").arg(resourcePath);
        }
        return false;
    }
    const QString cleanFile = QDir::cleanPath(canonicalFile);
    if (!pathIsInsideDirectory(cleanFile, root)) {
        if (error != nullptr) {
            *error = QStringLiteral("Pet overlay resource must stay inside the extension directory: %1").arg(resourcePath);
        }
        return false;
    }
    if (resolvedPath != nullptr) {
        *resolvedPath = cleanFile;
    }
    return true;
}

QString firstPetOverlayResourcePath(const QJsonObject& object)
{
    static const QStringList keys{
        QStringLiteral("image"),
        QStringLiteral("src"),
        QStringLiteral("resource"),
        QStringLiteral("path"),
    };
    for (const QString& key : keys) {
        const QString path = object.value(key).toString().trimmed();
        if (!path.isEmpty()) {
            return path;
        }
    }
    return QString();
}

QJsonArray petOverlayFrameArray(const QJsonObject& object)
{
    if (object.value(QStringLiteral("frames")).isArray()) {
        return object.value(QStringLiteral("frames")).toArray();
    }
    const QJsonObject sprite = object.value(QStringLiteral("sprite")).toObject();
    if (sprite.value(QStringLiteral("frames")).isArray()) {
        return sprite.value(QStringLiteral("frames")).toArray();
    }
    return {};
}

bool preparePetOverlay(QJsonObject* overlay, const QString& extensionRootPath, QString* error)
{
    if (overlay == nullptr) {
        return false;
    }
    overlay->insert(QStringLiteral("kind"), QStringLiteral("ui/petOverlay"));

    const QString imagePath = firstPetOverlayResourcePath(*overlay);
    if (!imagePath.isEmpty()) {
        QString resolvedImagePath;
        if (!extensionLocalFilePath(extensionRootPath, imagePath, &resolvedImagePath, error)) {
            return false;
        }
        overlay->insert(QStringLiteral("resolvedImagePath"), resolvedImagePath);
    }

    QJsonArray resolvedFrames;
    for (const QJsonValue& value : petOverlayFrameArray(*overlay)) {
        QString framePath;
        QJsonObject frameObject;
        if (value.isObject()) {
            frameObject = value.toObject();
            framePath = firstPetOverlayResourcePath(frameObject);
        } else {
            framePath = value.toString().trimmed();
        }
        if (framePath.isEmpty()) {
            continue;
        }
        QString resolvedFramePath;
        if (!extensionLocalFilePath(extensionRootPath, framePath, &resolvedFramePath, error)) {
            return false;
        }
        if (value.isObject()) {
            frameObject.insert(QStringLiteral("resolvedPath"), resolvedFramePath);
            resolvedFrames.append(frameObject);
        } else {
            resolvedFrames.append(resolvedFramePath);
        }
    }
    if (!resolvedFrames.isEmpty()) {
        overlay->insert(QStringLiteral("resolvedFrames"), resolvedFrames);
    }

    if (overlay->value(QStringLiteral("resolvedImagePath")).toString().isEmpty()
        && overlay->value(QStringLiteral("resolvedFrames")).toArray().isEmpty()
        && overlay->value(QStringLiteral("text")).toString().trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("Pet overlay requires image, frames, or text.");
        }
        return false;
    }

    return true;
}

}  // namespace

ExtensionManager::ExtensionManager(QObject* parent)
    : QObject(parent)
{
    refreshDebounce_.setSingleShot(true);
    refreshDebounce_.setInterval(500);
    connect(&refreshDebounce_, &QTimer::timeout, this, &ExtensionManager::refreshExtensions);
}

ExtensionManager::~ExtensionManager()
{
    shutdown();
}

void ExtensionManager::setCallbacks(ExtensionHostCallbacks callbacks)
{
    callbacks_ = std::move(callbacks);
}

void ExtensionManager::initialize(QMenuBar* menuBar, QMenu* toolsMenu, QMenu* helpMenu)
{
    menuBar_ = menuBar;
    toolsMenu_ = toolsMenu;
    helpMenu_ = helpMenu;
    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() {
        scheduleRefresh();
    });
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this]() {
        scheduleRefresh();
    });
    QDir().mkpath(userExtensionsDirectory());
}

void ExtensionManager::restartRuntime()
{
    if (callbacks_.mainWindowRequest) {
        callbacks_.mainWindowRequest(QStringLiteral("extensions/clearRuntimeContributions"), QJsonObject{});
    }
    shutdown();
    const QJsonArray runtimeManifests = manifestsForRuntime();
    if (runtimeManifests.isEmpty()) {
        appendExtensionLog(QStringLiteral("info"), QStringLiteral("extension runtime skipped"), QJsonObject{
            {QStringLiteral("reason"), QStringLiteral("no JavaScript extensions")},
        });
        return;
    }
    appendExtensionLog(QStringLiteral("info"), QStringLiteral("extension runtime starting"), QJsonObject{
        {QStringLiteral("extensionCount"), runtimeManifests.size()},
    });
    runtime_ = std::make_unique<EmbeddedExtensionRuntime>(this);
    runtime_->setHostRequestHandler([this](const QString& method, const QJsonObject& params) {
        return handleHostRequest(method, params);
    });
    connect(runtime_.get(), &EmbeddedExtensionRuntime::runtimeLogMessage, this, [this](const QString& message) {
        appendExtensionLog(QStringLiteral("info"), QStringLiteral("runtime log"), QJsonObject{
            {QStringLiteral("message"), message},
        });
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });
    connect(runtime_.get(), &EmbeddedExtensionRuntime::runtimeErrorMessage, this, [this](const QString& message) {
        diagnostics_.append(message);
        appendExtensionLog(QStringLiteral("error"), QStringLiteral("runtime error"), QJsonObject{
            {QStringLiteral("message"), message},
        });
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });

    QString error;
    if (!runtime_->start(runtimeManifests, &error)) {
        diagnostics_.append(error);
        appendExtensionLog(QStringLiteral("error"), QStringLiteral("runtime start failed"), QJsonObject{
            {QStringLiteral("error"), error},
        });
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(error));
        }
    } else {
        appendExtensionLog(QStringLiteral("info"), QStringLiteral("extension runtime started"), QJsonObject{
            {QStringLiteral("extensionCount"), runtimeManifests.size()},
        });
    }
}

void ExtensionManager::shutdown()
{
    if (runtime_) {
        runtime_->stop();
        runtime_.reset();
    }
}

bool isPathInside(const QString& path, const QString& root)
{
    if (path.trimmed().isEmpty() || root.trimmed().isEmpty()) {
        return false;
    }
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString cleanRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    return cleanPath == cleanRoot || cleanPath.startsWith(cleanRoot + QDir::separator(), Qt::CaseInsensitive)
        || cleanPath.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool copyDirectoryRecursively(const QString& sourcePath, const QString& targetPath, QString* error)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isDir()) {
        if (error != nullptr) {
            *error = QStringLiteral("Source is not a directory: %1").arg(sourcePath);
        }
        return false;
    }
    if (!QDir().mkpath(targetPath)) {
        if (error != nullptr) {
            *error = QStringLiteral("Cannot create target directory: %1").arg(targetPath);
        }
        return false;
    }
    QDirIterator iterator(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo entry = iterator.fileInfo();
        const QString relative = QDir(sourcePath).relativeFilePath(entry.absoluteFilePath());
        const QString target = QDir(targetPath).filePath(relative);
        if (entry.isDir()) {
            if (!QDir().mkpath(target)) {
                if (error != nullptr) {
                    *error = QStringLiteral("Cannot create directory: %1").arg(target);
                }
                return false;
            }
        } else {
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(entry.absoluteFilePath(), target)) {
                if (QFileInfo::exists(target)) {
                    QFile::remove(target);
                }
                if (!QFile::copy(entry.absoluteFilePath(), target)) {
                    if (error != nullptr) {
                        *error = QStringLiteral("Cannot copy %1 to %2").arg(entry.absoluteFilePath(), target);
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

QJsonObject okValue(const QJsonValue& value)
{
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("value"), value}};
}

QJsonObject errorObject(const QString& error)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

bool isPermanentlyBlockedApiMethod(const QString& method)
{
    Q_UNUSED(method);
    return false;
}

bool isBlockedPermission(const QString& permission)
{
    static const QSet<QString> blocked{};
    return blocked.contains(permission);
}

QStringList jsonStringList(const QJsonValue& value)
{
    QStringList result;
    if (!value.isArray()) {
        return result;
    }
    const QJsonArray array = value.toArray();
    for (const QJsonValue& item : array) {
        if (item.isString()) {
            result.append(item.toString());
        }
    }
    return result;
}

QProcessEnvironment processEnvironmentFromJson(const QJsonObject& object)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QJsonObject envObject = object.value(QStringLiteral("env")).toObject(
        object.value(QStringLiteral("environment")).toObject());
    for (auto it = envObject.constBegin(); it != envObject.constEnd(); ++it) {
        environment.insert(it.key(), it.value().toString());
    }
    return environment;
}

QJsonObject startDetachedProcess(
    const QString& target,
    const QString& program,
    const QStringList& arguments,
    const QString& workingDirectory,
    const QProcessEnvironment& environment,
    const QJsonObject& params,
    const QString& startedAlias)
{
    if (program.trimmed().isEmpty()) {
        return errorObject(QStringLiteral("%1 denied: missing program or command.").arg(target));
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    if (!workingDirectory.trimmed().isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.setProcessEnvironment(environment);

    qint64 pid = 0;
    const bool started = process.startDetached(&pid);
    QJsonObject value{
        {QStringLiteral("target"), target},
        {QStringLiteral("experimentalRaw"), true},
        {QStringLiteral("rawAccess"), true},
        {QStringLiteral("started"), started},
        {QStringLiteral("pid"), static_cast<double>(pid)},
        {QStringLiteral("program"), program},
        {QStringLiteral("args"), QJsonArray::fromStringList(arguments)},
        {QStringLiteral("workingDirectory"), workingDirectory},
        {QStringLiteral("params"), params},
    };
    if (!startedAlias.isEmpty()) {
        value.insert(startedAlias, started);
    }
    return okValue(value);
}

QJsonObject experimentalRawAccepted(
    const QString& target,
    const QString& hostMethod,
    const QString& member,
    const QJsonObject& params)
{
    return okValue(QJsonObject{
        {QStringLiteral("target"), target},
        {QStringLiteral("hostMethod"), hostMethod},
        {QStringLiteral("member"), member},
        {QStringLiteral("experimentalRaw"), true},
        {QStringLiteral("rawAccess"), true},
        {QStringLiteral("accepted"), true},
        {QStringLiteral("params"), params},
    });
}

QJsonObject apiDescriptor(
    const QString& id,
    const QString& method,
    const QString& permission,
    const QString& risk,
    const QString& status,
    const QString& description)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("permission"), permission},
        {QStringLiteral("risk"), risk},
        {QStringLiteral("status"), status},
        {QStringLiteral("description"), description},
    };
}

QVector<QJsonObject> extensionApiRegistry()
{
    static const QVector<QJsonObject> registry{
        apiDescriptor(QStringLiteral("app.getInfo"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get MiaCode version, platform, install roots, locale, and runtime information.")),
        apiDescriptor(QStringLiteral("app.openPreferences"), QStringLiteral("app/openPreferences"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open Preferences.")),
        apiDescriptor(QStringLiteral("app.openAboutDialog"), QStringLiteral("app/openAboutDialog"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open About dialog.")),
        apiDescriptor(QStringLiteral("app.reloadExtensions"), QStringLiteral("app/reloadExtensions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Reload local extensions.")),

        apiDescriptor(QStringLiteral("capabilities.list"), QStringLiteral("api/list"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List public v1 extension capabilities.")),
        apiDescriptor(QStringLiteral("capabilities.has"), QStringLiteral("api/has"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Check whether a public capability exists.")),
        apiDescriptor(QStringLiteral("capabilities.describe"), QStringLiteral("api/describe"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Describe a public capability.")),
        apiDescriptor(QStringLiteral("capabilities.describeNamespace"), QStringLiteral("api/describeNamespace"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Describe capabilities in a namespace.")),
        apiDescriptor(QStringLiteral("capabilities.call"), QStringLiteral("api/call"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Call an implemented public capability by id.")),
        apiDescriptor(QStringLiteral("capabilities.invokePublicMethod"), QStringLiteral("api/invoke"), QString(), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Invoke an implemented public host method.")),
        apiDescriptor(QStringLiteral("capabilities.request"), QStringLiteral("api/request"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Record a structured request for a missing extension capability.")),
        apiDescriptor(QStringLiteral("devtools.snapshot"), QStringLiteral("devtools/snapshot"), QStringLiteral("open.inspect"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Inspect extension host state, diagnostics, registered callbacks, and recent calls.")),
        apiDescriptor(QStringLiteral("devtools.diagnose"), QStringLiteral("devtools/diagnose"), QStringLiteral("open.inspect"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Diagnose one API id or public host method for route, permission, and block status.")),
        apiDescriptor(QStringLiteral("devtools.recentCalls"), QStringLiteral("devtools/recentCalls"), QStringLiteral("open.inspect"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read recent extension host API calls.")),

        apiDescriptor(QStringLiteral("open.list"), QStringLiteral("open/list"), QStringLiteral("open.inspect"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("List Open Bridge facade objects.")),
        apiDescriptor(QStringLiteral("open.describe"), QStringLiteral("open/describe"), QStringLiteral("open.inspect"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Describe one Open Bridge facade object.")),
        apiDescriptor(QStringLiteral("open.call"), QStringLiteral("open/call"), QStringLiteral("open.call"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Call an implemented Open Bridge facade method.")),
        apiDescriptor(QStringLiteral("open.forbiddenTargets"), QStringLiteral("open/forbiddenTargets"), QStringLiteral("open.inspect"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Legacy name: list experimental raw Open Bridge targets.")),
        apiDescriptor(QStringLiteral("open.describeForbiddenTarget"), QStringLiteral("open/describeForbiddenTarget"), QStringLiteral("open.inspect"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Legacy name: describe one experimental raw Open Bridge target.")),

        apiDescriptor(QStringLiteral("extensions.list"), QStringLiteral("extensions/all"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("List discovered extensions.")),
        apiDescriptor(QStringLiteral("extensions.get"), QStringLiteral("extensions/get"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Get extension information.")),
        apiDescriptor(QStringLiteral("extensions.reload"), QStringLiteral("app/reloadExtensions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Reload extensions.")),
        apiDescriptor(QStringLiteral("extensions.enable"), QStringLiteral("extensions/enable"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Enable an extension.")),
        apiDescriptor(QStringLiteral("extensions.disable"), QStringLiteral("extensions/disable"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Disable an extension.")),
        apiDescriptor(QStringLiteral("extensions.installFromFolder"), QStringLiteral("extensions/installFromFolder"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Install an extension from a local folder.")),
        apiDescriptor(QStringLiteral("extensions.remove"), QStringLiteral("extensions/remove"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Remove a user-installed extension.")),

        apiDescriptor(QStringLiteral("commands.list"), QStringLiteral("commands/getCommands"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List extension commands.")),
        apiDescriptor(QStringLiteral("commands.describe"), QStringLiteral("commands/describe"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Describe an extension command.")),
        apiDescriptor(QStringLiteral("commands.register"), QStringLiteral("commands/register"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Register a command callback from the owning extension.")),
        apiDescriptor(QStringLiteral("commands.execute"), QStringLiteral("commands/execute"), QStringLiteral("commands.execute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Execute an extension command.")),
        apiDescriptor(QStringLiteral("commands.setChecked"), QStringLiteral("commands/setChecked"), QStringLiteral("commands.execute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set the checked state of an extension's own menu command.")),
        apiDescriptor(QStringLiteral("commands.executeInternal"), QStringLiteral("commands/executeInternal"), QStringLiteral("commands.execute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Execute an allowlisted MiaCode internal command.")),
        apiDescriptor(QStringLiteral("commands.getInternalCommands"), QStringLiteral("commands/getInternalCommands"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List allowlisted MiaCode internal commands.")),

        apiDescriptor(QStringLiteral("window.showInformationMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show an information message.")),
        apiDescriptor(QStringLiteral("window.showWarningMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show a warning message.")),
        apiDescriptor(QStringLiteral("window.showErrorMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show an error message.")),
        apiDescriptor(QStringLiteral("window.showInputBox"), QStringLiteral("window/showInputBox"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show a text input dialog.")),
        apiDescriptor(QStringLiteral("window.showQuickPick"), QStringLiteral("window/showQuickPick"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show a simple selection dialog.")),
        apiDescriptor(QStringLiteral("window.createStatusBarItem"), QStringLiteral("window/createStatusBarItem"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show a temporary status bar message.")),
        apiDescriptor(QStringLiteral("window.focusEditor"), QStringLiteral("window/focusEditor"), QStringLiteral("ui.prompt"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Focus the active chart editor.")),

        apiDescriptor(QStringLiteral("workspace.getActiveDocument"), QStringLiteral("workspace/getActiveDocument"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get the active document snapshot.")),
        apiDescriptor(QStringLiteral("workspace.applyDocumentEdit"), QStringLiteral("workspace/applyDocumentEdit"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace active document text.")),
        apiDescriptor(QStringLiteral("workspace.getCurrentFilePath"), QStringLiteral("workspace/getCurrentFilePath"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current file path.")),
        apiDescriptor(QStringLiteral("workspace.getChartFolder"), QStringLiteral("workspace/getChartFolder"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get the current chart folder.")),
        apiDescriptor(QStringLiteral("workspace.getChartMetadata"), QStringLiteral("workspace/getChartMetadata"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get chart metadata.")),
        apiDescriptor(QStringLiteral("workspace.updateChartMetadata"), QStringLiteral("workspace/updateChartMetadata"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Update chart metadata.")),
        apiDescriptor(QStringLiteral("workspace.getMediaFiles"), QStringLiteral("workspace/getMediaFiles"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List media files in the chart folder.")),
        apiDescriptor(QStringLiteral("workspace.getDirtyState"), QStringLiteral("workspace/getDirtyState"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get dirty state.")),
        apiDescriptor(QStringLiteral("workspace.save"), QStringLiteral("workspace/save"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Save the current chart.")),
        apiDescriptor(QStringLiteral("workspace.saveAs"), QStringLiteral("workspace/saveAs"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Save the current chart to a path.")),

        apiDescriptor(QStringLiteral("document.query"), QStringLiteral("document/query"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Query active chart data with a selector.")),
        apiDescriptor(QStringLiteral("document.edit"), QStringLiteral("document/edit"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Apply chart edit operations.")),
        apiDescriptor(QStringLiteral("document.getText"), QStringLiteral("workspace/getActiveDocument"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get active chart text.")),
        apiDescriptor(QStringLiteral("document.setText"), QStringLiteral("workspace/applyDocumentEdit"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace active chart text.")),
        apiDescriptor(QStringLiteral("document.getDifficulties"), QStringLiteral("document/getDifficulties"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List difficulties.")),
        apiDescriptor(QStringLiteral("document.getActiveDifficulty"), QStringLiteral("document/getActiveDifficulty"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get active difficulty.")),
        apiDescriptor(QStringLiteral("document.setActiveDifficulty"), QStringLiteral("document/setActiveDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set active difficulty.")),
        apiDescriptor(QStringLiteral("document.replaceActiveDifficultyText"), QStringLiteral("document/replaceActiveDifficultyText"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace active difficulty text.")),
        apiDescriptor(QStringLiteral("document.getParsedNotes"), QStringLiteral("document/getParsedNoteMarkers"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get parsed note markers.")),
        apiDescriptor(QStringLiteral("document.getTimingMetadata"), QStringLiteral("document/getTimingMetadata"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get timing metadata.")),
        apiDescriptor(QStringLiteral("document.applyTextEdits"), QStringLiteral("document/applyTextEdits"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Apply text edits.")),
        apiDescriptor(QStringLiteral("document.format"), QStringLiteral("document/format"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Normalize the active chart.")),
        apiDescriptor(QStringLiteral("document.createDifficulty"), QStringLiteral("document/createDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Create a difficulty.")),
        apiDescriptor(QStringLiteral("document.deleteDifficulty"), QStringLiteral("document/deleteDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Delete a difficulty.")),
        apiDescriptor(QStringLiteral("document.renameDifficulty"), QStringLiteral("document/renameDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Rename a difficulty.")),

        apiDescriptor(QStringLiteral("editor.getSelection"), QStringLiteral("editor/getSelection"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get editor selection.")),
        apiDescriptor(QStringLiteral("editor.getCursor"), QStringLiteral("editor/getCursor"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get editor cursor.")),
        apiDescriptor(QStringLiteral("editor.getText"), QStringLiteral("editor/getText"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get active editor text.")),
        apiDescriptor(QStringLiteral("editor.getVisibleRange"), QStringLiteral("editor/getVisibleRange"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get visible editor text range.")),
        apiDescriptor(QStringLiteral("editor.revealRange"), QStringLiteral("editor/revealRange"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Reveal an editor range.")),
        apiDescriptor(QStringLiteral("editor.getParsedSnapshot"), QStringLiteral("editor/getParsedSnapshot"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get editor text and parsed-marker summary.")),
        apiDescriptor(QStringLiteral("editor.setSelection"), QStringLiteral("editor/setSelection"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set editor selection.")),
        apiDescriptor(QStringLiteral("editor.getLine"), QStringLiteral("editor/getLine"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get a line of text.")),
        apiDescriptor(QStringLiteral("editor.getCurrentLine"), QStringLiteral("editor/getCurrentLine"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current line.")),
        apiDescriptor(QStringLiteral("editor.getCurrentToken"), QStringLiteral("editor/getCurrentToken"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current token.")),
        apiDescriptor(QStringLiteral("editor.insertText"), QStringLiteral("editor/insertText"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Insert text.")),
        apiDescriptor(QStringLiteral("editor.replaceSelection"), QStringLiteral("editor/replaceSelection"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace selection.")),
        apiDescriptor(QStringLiteral("editor.replaceRange"), QStringLiteral("editor/replaceRange"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace a text range.")),
        apiDescriptor(QStringLiteral("editor.addDecoration"), QStringLiteral("editor/addDecoration"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add editor decoration.")),
        apiDescriptor(QStringLiteral("editor.clearDecorations"), QStringLiteral("editor/clearDecorations"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear editor decorations.")),
        apiDescriptor(QStringLiteral("editor.showCompletions"), QStringLiteral("editor/showCompletions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show registered completion providers in host UI.")),
        apiDescriptor(QStringLiteral("editor.showCodeActions"), QStringLiteral("editor/showCodeActions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show registered code actions in host UI.")),

        apiDescriptor(QStringLiteral("validation.run"), QStringLiteral("validation/run"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Run chart validation.")),
        apiDescriptor(QStringLiteral("validation.getLastResult"), QStringLiteral("validation/getLastResult"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Get last validation result.")),
        apiDescriptor(QStringLiteral("validation.addDiagnostics"), QStringLiteral("validation/addDiagnostics"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add extension diagnostics.")),
        apiDescriptor(QStringLiteral("validation.clearDiagnostics"), QStringLiteral("validation/clearDiagnostics"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear extension diagnostics.")),
        apiDescriptor(QStringLiteral("diagnostics.validateDocument"), QStringLiteral("diagnostics/validateDocument"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Validate the active document.")),

        apiDescriptor(QStringLiteral("timeline.getSnapshot"), QStringLiteral("timeline/getSnapshot"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get timeline snapshot.")),
        apiDescriptor(QStringLiteral("timeline.getCurrentSecond"), QStringLiteral("timeline/getCurrentSecond"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current preview/timeline second.")),
        apiDescriptor(QStringLiteral("timeline.getZoomState"), QStringLiteral("timeline/getZoomState"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get timeline zoom state.")),
        apiDescriptor(QStringLiteral("timeline.getVisibleRange"), QStringLiteral("timeline/getVisibleRange"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get approximate visible timeline seconds.")),
        apiDescriptor(QStringLiteral("timeline.getMarkersAtSecond"), QStringLiteral("timeline/getMarkersAtSecond"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get parsed note markers near a second.")),
        apiDescriptor(QStringLiteral("timeline.seek"), QStringLiteral("timeline/seek"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Seek timeline.")),
        apiDescriptor(QStringLiteral("timeline.zoomIn"), QStringLiteral("timeline/zoomIn"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Increase timeline zoom by one preset.")),
        apiDescriptor(QStringLiteral("timeline.zoomOut"), QStringLiteral("timeline/zoomOut"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Decrease timeline zoom by one preset.")),
        apiDescriptor(QStringLiteral("timeline.stepZoomPreset"), QStringLiteral("timeline/stepZoomPreset"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Move timeline zoom by a preset delta.")),
        apiDescriptor(QStringLiteral("timeline.setZoomScale"), QStringLiteral("timeline/setZoomScale"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set timeline zoom scale when the active host supports anchored scaling.")),
        apiDescriptor(QStringLiteral("timeline.scrollToSecond"), QStringLiteral("timeline/scrollToSecond"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Scroll or seek the timeline to a second.")),
        apiDescriptor(QStringLiteral("timeline.setFollowPreview"), QStringLiteral("timeline/setFollowPreview"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Enable or disable timeline follow-preview mode.")),
        apiDescriptor(QStringLiteral("timeline.setFollowProgress"), QStringLiteral("timeline/setFollowProgress"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Enable or disable timeline follow-progress mode.")),
        apiDescriptor(QStringLiteral("timeline.addMarker"), QStringLiteral("timeline/addMarker"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add extension timeline marker.")),
        apiDescriptor(QStringLiteral("timeline.clearMarkers"), QStringLiteral("timeline/clearMarkers"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear extension timeline markers.")),
        apiDescriptor(QStringLiteral("timeline.addBand"), QStringLiteral("timeline/addBand"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add extension timeline band.")),
        apiDescriptor(QStringLiteral("timeline.addVerticalLine"), QStringLiteral("timeline/addVerticalLine"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add extension timeline vertical line.")),
        apiDescriptor(QStringLiteral("timeline.clearVisuals"), QStringLiteral("timeline/clearVisuals"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear extension timeline visuals.")),

        apiDescriptor(QStringLiteral("preview.getState"), QStringLiteral("preview/getState"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get preview state.")),
        apiDescriptor(QStringLiteral("preview.getRenderState"), QStringLiteral("preview/getRenderState"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get preview canvas and render option state.")),
        apiDescriptor(QStringLiteral("preview.play"), QStringLiteral("preview/play"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Play preview.")),
        apiDescriptor(QStringLiteral("preview.pause"), QStringLiteral("preview/pause"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Pause preview.")),
        apiDescriptor(QStringLiteral("preview.stop"), QStringLiteral("preview/stop"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Stop preview.")),
        apiDescriptor(QStringLiteral("preview.seek"), QStringLiteral("preview/seek"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Seek preview.")),
        apiDescriptor(QStringLiteral("preview.setSpeed"), QStringLiteral("preview/setSpeed"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set preview speed.")),
        apiDescriptor(QStringLiteral("preview.setMineSkinEnabled"), QStringLiteral("preview/setMineSkinEnabled"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Choose dedicated or normal sprites for mine notes.")),
        apiDescriptor(QStringLiteral("preview.setMineSfxEnabled"), QStringLiteral("preview/setMineSfxEnabled"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Enable or mute type-based SFX for mine notes.")),
        apiDescriptor(QStringLiteral("preview.addOverlay"), QStringLiteral("preview/addOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add a rendered text overlay above the preview.")),
        apiDescriptor(QStringLiteral("preview.updateOverlay"), QStringLiteral("preview/updateOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Update a preview overlay.")),
        apiDescriptor(QStringLiteral("preview.removeOverlay"), QStringLiteral("preview/removeOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Remove a preview overlay.")),
        apiDescriptor(QStringLiteral("preview.clearOverlays"), QStringLiteral("preview/clearOverlays"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear preview overlays.")),
        apiDescriptor(QStringLiteral("preview.getOverlays"), QStringLiteral("preview/getOverlays"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List preview overlays.")),
        apiDescriptor(QStringLiteral("preview.renderOverlayLayer"), QStringLiteral("preview/renderOverlayLayer"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Refresh the preview overlay layer.")),
        apiDescriptor(QStringLiteral("preview.hitTestOverlay"), QStringLiteral("preview/hitTestOverlay"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Hit-test preview overlays.")),

        apiDescriptor(QStringLiteral("ui.registerBottomTabView"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register and render a bottom-tab extension view.")),
        apiDescriptor(QStringLiteral("ui.registerToolbarButton"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register and render an extension toolbox button.")),
        apiDescriptor(QStringLiteral("ui.registerFloatingPanel"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register and render a modeless floating extension panel.")),
        apiDescriptor(QStringLiteral("ui.registerPetOverlay"), QStringLiteral("ui/registerPetOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register a controlled preview pet overlay using extension-local resources.")),
        apiDescriptor(QStringLiteral("ui.registerSceneOverlay"), QStringLiteral("ui/registerSceneOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register a controlled preview scene/canvas overlay.")),
        apiDescriptor(QStringLiteral("ui.getContributions"), QStringLiteral("ui/getContributions"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Get registered UI contributions.")),
        apiDescriptor(QStringLiteral("ui.getViews"), QStringLiteral("ui/getViews"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("List rendered extension views.")),
        apiDescriptor(QStringLiteral("ui.unregisterView"), QStringLiteral("ui/unregisterView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Unregister rendered extension views.")),
        apiDescriptor(QStringLiteral("ui.refreshViews"), QStringLiteral("ui/refreshViews"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Refresh extension view hosts.")),
        apiDescriptor(QStringLiteral("ui.renderDeclarativeView"), QStringLiteral("ui/renderDeclarativeView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render a declarative extension view.")),
        apiDescriptor(QStringLiteral("ui.renderBottomTabView"), QStringLiteral("ui/renderBottomTabView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render a bottom-tab extension view.")),
        apiDescriptor(QStringLiteral("ui.renderFloatingPanel"), QStringLiteral("ui/renderFloatingPanel"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render a modeless floating extension panel.")),
        apiDescriptor(QStringLiteral("ui.renderToolbarButton"), QStringLiteral("ui/renderToolbarButton"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render an extension toolbox button.")),
        apiDescriptor(QStringLiteral("ui.renderSceneOverlay"), QStringLiteral("ui/renderSceneOverlay"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render a controlled scene overlay above the preview.")),
        apiDescriptor(QStringLiteral("ui.renderWebView"), QStringLiteral("ui/renderWebView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render an HTML-lite view in a controlled host.")),
        apiDescriptor(QStringLiteral("ui.renderCanvasView"), QStringLiteral("ui/renderCanvasView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Render a declarative canvas/scene view.")),

        apiDescriptor(QStringLiteral("logs.writeExtensionLog"), QStringLiteral("logs/append"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Append to an extension log.")),
        apiDescriptor(QStringLiteral("logs.openFolder"), QStringLiteral("logs/open"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Open extension logs.")),
        apiDescriptor(QStringLiteral("logs.getPath"), QStringLiteral("logs/getPath"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get extension log path.")),
        apiDescriptor(QStringLiteral("logs.readRecent"), QStringLiteral("logs/readRecent"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read recent extension log lines.")),

        apiDescriptor(QStringLiteral("input.registerWheelGesture"), QStringLiteral("input/registerWheelGesture"), QStringLiteral("input.listen"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register a controlled wheel gesture that invokes an extension command.")),
        apiDescriptor(QStringLiteral("input.registerKeyGesture"), QStringLiteral("input/registerKeyGesture"), QStringLiteral("input.listen"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register a controlled key gesture that invokes an extension command.")),
        apiDescriptor(QStringLiteral("input.registerMouseGesture"), QStringLiteral("input/registerMouseGesture"), QStringLiteral("input.listen"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register a controlled mouse gesture that invokes an extension command.")),
        apiDescriptor(QStringLiteral("input.getGestures"), QStringLiteral("input/getGestures"), QStringLiteral("input.listen"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List registered extension input gestures.")),
        apiDescriptor(QStringLiteral("events.subscribe"), QStringLiteral("events/register"), QStringLiteral("events.subscribe"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Subscribe to an exact event name or namespace wildcard with optional filters.")),
        apiDescriptor(QStringLiteral("providers.registerHoverProvider"), QStringLiteral("contributions/register"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register an editor hover provider descriptor.")),
        apiDescriptor(QStringLiteral("providers.registerCompletionProvider"), QStringLiteral("contributions/register"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register an editor completion provider descriptor.")),
        apiDescriptor(QStringLiteral("providers.registerCodeActionProvider"), QStringLiteral("contributions/register"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register an editor code-action provider descriptor.")),
        apiDescriptor(QStringLiteral("providers.getRegistered"), QStringLiteral("providers/getRegistered"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List registered provider descriptors.")),
        apiDescriptor(QStringLiteral("providers.collectHover"), QStringLiteral("providers/collectHover"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Collect hover providers for the current editor context.")),
        apiDescriptor(QStringLiteral("providers.collectCompletions"), QStringLiteral("providers/collectCompletions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Collect completion providers for a context.")),
        apiDescriptor(QStringLiteral("providers.collectCodeActions"), QStringLiteral("providers/collectCodeActions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Collect code-action providers for a context.")),
        apiDescriptor(QStringLiteral("providers.showHover"), QStringLiteral("providers/showHover"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show hover provider output in host UI.")),
        apiDescriptor(QStringLiteral("providers.showCompletions"), QStringLiteral("providers/showCompletions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show completion provider output in host UI.")),
        apiDescriptor(QStringLiteral("providers.showCodeActions"), QStringLiteral("providers/showCodeActions"), QStringLiteral("providers.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show code-action provider output in host UI.")),
        apiDescriptor(QStringLiteral("ui.registerSidebarView"), QStringLiteral("ui/renderSidebarView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register and render a sidebar-style extension view.")),
        apiDescriptor(QStringLiteral("ui.registerPreferencesPage"), QStringLiteral("ui/renderPreferencesPage"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register and render an extension preferences page.")),
        apiDescriptor(QStringLiteral("export.hooks"), QStringLiteral("contributions/register"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register export hook/template/provider descriptors.")),
        apiDescriptor(QStringLiteral("media"), QStringLiteral("media/*"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read media metadata and chart media paths.")),
        apiDescriptor(QStringLiteral("theme"), QStringLiteral("theme/*"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read theme state and theme colors.")),
        apiDescriptor(QStringLiteral("backup"), QStringLiteral("backup/*"), QStringLiteral("backup.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read and write extension backup snapshots.")),
        apiDescriptor(QStringLiteral("shortcuts"), QStringLiteral("shortcuts/*"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read and register extension shortcut descriptors.")),
        apiDescriptor(QStringLiteral("shortcuts.list"), QStringLiteral("shortcuts/list"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List extension shortcut bindings.")),
        apiDescriptor(QStringLiteral("shortcuts.getEditable"), QStringLiteral("shortcuts/getEditable"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List shortcuts visible in Preferences > Shortcuts.")),
        apiDescriptor(QStringLiteral("shortcuts.getKeybinding"), QStringLiteral("shortcuts/getKeybinding"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read one extension shortcut binding.")),
        apiDescriptor(QStringLiteral("shortcuts.register"), QStringLiteral("shortcuts/register"), QStringLiteral("settings.write"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register an extension command shortcut that can be edited in Preferences > Shortcuts.")),

        apiDescriptor(QStringLiteral("shell.execute"), QStringLiteral("shell/execute"), QStringLiteral("shell.execute"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw shell execution API; starts a detached shell command.")),
        apiDescriptor(QStringLiteral("process.spawn"), QStringLiteral("process/spawn"), QStringLiteral("process.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw process API; starts a detached process.")),
        apiDescriptor(QStringLiteral("native"), QStringLiteral("native/raw"), QStringLiteral("native.unsafe"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw native API namespace.")),
        apiDescriptor(QStringLiteral("internal.raw"), QStringLiteral("internal/raw"), QStringLiteral("internal.raw"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw internal API namespace.")),
        apiDescriptor(QStringLiteral("renderer.raw"), QStringLiteral("renderer/raw"), QStringLiteral("renderer.raw"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw renderer API namespace.")),
        apiDescriptor(QStringLiteral("export.raw"), QStringLiteral("export/raw"), QStringLiteral("export.raw"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw export API namespace.")),
        apiDescriptor(QStringLiteral("security"), QStringLiteral("security/raw"), QStringLiteral("security.override"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw security API namespace.")),
        apiDescriptor(QStringLiteral("updates"), QStringLiteral("updates/raw"), QStringLiteral("updates.modify"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Experimental raw updater API namespace.")),
    };
    return registry;
}

QJsonObject findApiDescriptor(const QString& id)
{
    for (const QJsonObject& descriptor : extensionApiRegistry()) {
        if (descriptor.value(QStringLiteral("id")).toString() == id) {
            return descriptor;
        }
    }
    return QJsonObject();
}

bool methodIsImplementedPublicApi(const QString& method)
{
    for (const QJsonObject& descriptor : extensionApiRegistry()) {
        const QString descriptorMethod = descriptor.value(QStringLiteral("method")).toString();
        const bool methodMatches = descriptorMethod == method
            || (descriptorMethod.endsWith(QStringLiteral("/*"))
                && method.startsWith(descriptorMethod.left(descriptorMethod.size() - 1)));
        if (!methodMatches) {
            continue;
        }
        if (descriptor.value(QStringLiteral("status")).toString() == QStringLiteral("implemented")) {
            return true;
        }
    }
    return false;
}

const QVector<ExtensionManifest>& ExtensionManager::manifests() const
{
    return manifests_;
}

const QVector<ExtensionRecord>& ExtensionManager::records() const
{
    return records_;
}

QStringList ExtensionManager::diagnostics() const
{
    return diagnostics_;
}

QString ExtensionManager::userExtensionsDirectory() const
{
    return userExtensionDirectoryPath();
}

QString ExtensionManager::extensionLogDirectory() const
{
    const QString logRoot = qEnvironmentVariable("MIACODE_LOG_DIR");
    if (!logRoot.trimmed().isEmpty()) {
        return QDir::cleanPath(logRoot);
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
}

void ExtensionManager::appendExtensionLog(const QString& severity, const QString& message, const QJsonObject& details) const
{
    const QString logRoot = extensionLogDirectory();
    QDir().mkpath(logRoot);

    QFile file(QDir(logRoot).filePath(QStringLiteral("extensions.log")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << " [" << severity.trimmed().toUpper() << "] "
           << message;
    if (!details.isEmpty()) {
        stream << " " << compactJsonObject(details);
    }
    stream << '\n';
}

QJsonObject ExtensionManager::devtoolsSnapshotForUi() const
{
    return devtoolsSnapshot(QString());
}

void ExtensionManager::publishEvent(const QString& name, const QJsonObject& payload, bool coalescible)
{
    if (runtime_ != nullptr && runtime_->isRunning()) {
        runtime_->dispatchEvent(name, payload, coalescible);
    }
}

void ExtensionManager::refreshExtensions()
{
    discoverExtensions();
    rebuildMenuContributions();
    UiText::reloadExtensionLanguagePacks();
    const bool resetLanguage = UiText::ensurePreferredLanguageAvailable();
    restartRuntime();
    rebuildFilesystemWatchers();
    if (callbacks_.logMessage) {
        callbacks_.logMessage(QStringLiteral("extensions refreshed: %1 enabled, %2 diagnostics")
                                  .arg(manifests_.size())
                                  .arg(diagnostics_.size()));
    }
    appendExtensionLog(QStringLiteral("info"), QStringLiteral("extensions refreshed"), QJsonObject{
        {QStringLiteral("enabledExtensions"), manifests_.size()},
        {QStringLiteral("records"), records_.size()},
        {QStringLiteral("diagnostics"), QJsonArray::fromStringList(diagnostics_)},
    });
    if (resetLanguage && callbacks_.showMessage) {
        callbacks_.showMessage(
            QStringLiteral("warning"),
            UiText::isChineseUi()
                ? QStringLiteral("当前语言包不可用，已切回跟随系统。")
                : QStringLiteral("The selected language pack is unavailable. MiaCode switched back to Follow System."));
    }
}

void ExtensionManager::setExtensionEnabled(const QString& qualifiedId, bool enabled)
{
    saveDisabledExtensionState(qualifiedId, !enabled);
    refreshExtensions();
}

void ExtensionManager::discoverExtensions()
{
    records_.clear();
    manifests_.clear();
    diagnostics_.clear();
    commandOwnerById_.clear();
    languageOwnerById_.clear();

    const auto tryAppendManifest = [this](const QFileInfo& entry) {
        const ExtensionManifestParseResult parsed = loadExtensionManifest(entry.absoluteFilePath());
        if (!parsed.ok) {
            const QString diagnostic = QStringLiteral("%1: %2").arg(entry.fileName(), parsed.error);
            diagnostics_.append(diagnostic);
            appendExtensionLog(QStringLiteral("error"), QStringLiteral("manifest parse failed"), QJsonObject{
                {QStringLiteral("sourcePath"), entry.absoluteFilePath()},
                {QStringLiteral("diagnostic"), diagnostic},
            });
            ExtensionRecord record;
            record.valid = false;
            record.enabled = false;
            record.sourcePath = entry.absoluteFilePath();
            record.diagnostic = parsed.error;
            records_.append(record);
            return;
        }
        ExtensionRecord record;
        record.manifest = parsed.manifest;
        record.valid = true;
        record.enabled = !isDisabledByUser(parsed.manifest.qualifiedId());
        record.sourcePath = entry.absoluteFilePath();
        bool duplicate = false;
        for (const ExtensionManifest& existing : manifests_) {
            if (existing.qualifiedId() == parsed.manifest.qualifiedId()) {
                record.valid = false;
                record.enabled = false;
                record.diagnostic = QStringLiteral("Duplicate extension id '%1'.").arg(parsed.manifest.qualifiedId());
                const QString diagnostic = QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic);
                diagnostics_.append(diagnostic);
                appendExtensionLog(QStringLiteral("error"), QStringLiteral("duplicate extension id"), QJsonObject{
                    {QStringLiteral("extensionId"), parsed.manifest.qualifiedId()},
                    {QStringLiteral("sourcePath"), entry.absoluteFilePath()},
                    {QStringLiteral("diagnostic"), diagnostic},
                });
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            records_.append(record);
            return;
        }
        if (!record.enabled) {
            records_.append(record);
            return;
        }
        for (const ExtensionCommandContribution& command : parsed.manifest.commands) {
            if (commandOwnerById_.contains(command.id)) {
                record.valid = false;
                record.enabled = false;
                record.diagnostic = QStringLiteral("Command '%1' is already contributed by %2.")
                                        .arg(command.id, commandOwnerById_.value(command.id));
                const QString diagnostic = QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic);
                diagnostics_.append(diagnostic);
                appendExtensionLog(QStringLiteral("error"), QStringLiteral("duplicate command contribution"), QJsonObject{
                    {QStringLiteral("extensionId"), parsed.manifest.qualifiedId()},
                    {QStringLiteral("command"), command.id},
                    {QStringLiteral("sourcePath"), entry.absoluteFilePath()},
                    {QStringLiteral("diagnostic"), diagnostic},
                });
                duplicate = true;
                break;
            }
            commandOwnerById_.insert(command.id, parsed.manifest.qualifiedId());
        }
        for (const ExtensionLanguageContribution& language : parsed.manifest.languages) {
            if (languageOwnerById_.contains(language.id)) {
                record.valid = false;
                record.enabled = false;
                record.diagnostic = QStringLiteral("Language '%1' is already contributed by %2.")
                                        .arg(language.id, languageOwnerById_.value(language.id));
                const QString diagnostic = QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic);
                diagnostics_.append(diagnostic);
                appendExtensionLog(QStringLiteral("error"), QStringLiteral("duplicate language contribution"), QJsonObject{
                    {QStringLiteral("extensionId"), parsed.manifest.qualifiedId()},
                    {QStringLiteral("language"), language.id},
                    {QStringLiteral("sourcePath"), entry.absoluteFilePath()},
                    {QStringLiteral("diagnostic"), diagnostic},
                });
                duplicate = true;
                break;
            }
            languageOwnerById_.insert(language.id, parsed.manifest.qualifiedId());
        }
        if (!duplicate && record.valid) {
            manifests_.append(parsed.manifest);
        }
        records_.append(record);
    };

    for (const QString& rootPath : defaultExtensionSearchPaths()) {
        QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }
        if (QFileInfo::exists(root.filePath(QStringLiteral("miacode-extension.json")))
            || QFileInfo::exists(root.filePath(QStringLiteral("package.json")))) {
            tryAppendManifest(QFileInfo(root.absolutePath()));
            continue;
        }
        const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            tryAppendManifest(entry);
        }
    }
}

void ExtensionManager::rebuildMenuContributions()
{
    if (extensionsMenu_ != nullptr) {
        QAction* menuAction = extensionsMenu_->menuAction();
        if (toolsMenu_ != nullptr) {
            toolsMenu_->removeAction(menuAction);
        }
        extensionsMenu_->deleteLater();
        extensionsMenu_ = nullptr;
    }
    for (const QPointer<QAction>& action : topLevelMenuActions_) {
        if (action.isNull()) {
            continue;
        }
        if (menuBar_ != nullptr) {
            menuBar_->removeAction(action);
        }
        action->deleteLater();
    }
    topLevelMenuActions_.clear();

    commandActions_.clear();
    bool addedToolsCommand = false;
    for (const ExtensionManifest& manifest : manifests_) {
        for (const ExtensionMenuContribution& menu : manifest.menus) {
            const ExtensionCommandContribution command = commandContribution(menu.command);
            if (command.id.isEmpty()) {
                continue;
            }
            QAction* action = nullptr;
            if (menu.location == QStringLiteral("tools/menu")) {
                if (toolsMenu_ == nullptr) {
                    continue;
                }
                if (extensionsMenu_ == nullptr) {
                    extensionsMenu_ = toolsMenu_->addMenu(
                        localizedText(QStringLiteral("extension.menu.extensions"), QStringLiteral("Extensions")));
                }
                action = extensionsMenu_->addAction(displayLabelForCommand(command));
                addedToolsCommand = true;
            } else if (menu.location == QStringLiteral("menubar/beforeHelp")) {
                if (menuBar_ == nullptr) {
                    continue;
                }
                action = new QAction(command.title, this);
                const QAction* beforeAction = helpMenu_ != nullptr ? helpMenu_->menuAction() : nullptr;
                if (beforeAction != nullptr) {
                    menuBar_->insertAction(const_cast<QAction*>(beforeAction), action);
                } else {
                    menuBar_->addAction(action);
                }
                topLevelMenuActions_.append(action);
            } else {
                continue;
            }
            if (action == nullptr) {
                continue;
            }
            action->setData(command.id);
            action->setToolTip(QStringLiteral("%1 (%2)").arg(command.title, manifest.qualifiedId()));
            connect(action, &QAction::triggered, this, [this, command]() {
                invokeCommand(command.id);
            });
            commandActions_.insert(command.id, action);
        }
    }
    if (extensionsMenu_ == nullptr && toolsMenu_ != nullptr) {
        extensionsMenu_ = toolsMenu_->addMenu(
            localizedText(QStringLiteral("extension.menu.extensions"), QStringLiteral("Extensions")));
    }
    if (extensionsMenu_ != nullptr && !addedToolsCommand) {
        QAction* emptyAction = extensionsMenu_->addAction(
            localizedText(QStringLiteral("extension.menu.empty"), QStringLiteral("No extension commands")));
        emptyAction->setEnabled(false);
    }
}

QJsonArray ExtensionManager::manifestsForRuntime() const
{
    QJsonArray array;
    for (const ExtensionManifest& manifest : manifests_) {
        if (!manifest.needsJavaScriptRuntime()) {
            continue;
        }
        QJsonObject object = manifest.toRuntimeJson();
        object.insert(QStringLiteral("activateOnStartup"), activationIncludesStartup(manifest));
        array.append(object);
    }
    return array;
}

void ExtensionManager::rebuildFilesystemWatchers()
{
    if (watcher_ == nullptr) {
        return;
    }
    const QStringList oldDirectories = watcher_->directories();
    if (!oldDirectories.isEmpty()) {
        watcher_->removePaths(oldDirectories);
    }
    QStringList paths;
    for (const QString& rootPath : defaultExtensionSearchPaths()) {
        QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }
        paths.append(root.absolutePath());
        const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            paths.append(entry.absoluteFilePath());
        }
    }
    paths.removeDuplicates();
    if (!paths.isEmpty()) {
        watcher_->addPaths(paths);
    }
}

void ExtensionManager::scheduleRefresh()
{
    refreshDebounce_.start();
}

QString ExtensionManager::permissionForMethod(const QString& method, const QJsonObject& params) const
{
    if (method == QStringLiteral("api/list")
        || method == QStringLiteral("api/has")
        || method == QStringLiteral("api/describe")
        || method == QStringLiteral("api/describeNamespace")
        || method == QStringLiteral("api/request")
        || method == QStringLiteral("api/invoke")
        || method == QStringLiteral("api/call")) {
        return QString();
    }
    if (method == QStringLiteral("log") || method == QStringLiteral("commands/register")) {
        return QString();
    }
    if (method.startsWith(QStringLiteral("devtools/"))) {
        return QStringLiteral("open.inspect");
    }
    if (method == QStringLiteral("open/list")
        || method == QStringLiteral("open/describe")
        || method == QStringLiteral("open/forbiddenTargets")
        || method == QStringLiteral("open/describeForbiddenTarget")
        || method == QStringLiteral("objects/list")
        || method == QStringLiteral("objects/describe")
        || method == QStringLiteral("objects/inspect")) {
        return QStringLiteral("open.inspect");
    }
    if (method == QStringLiteral("open/call") || method == QStringLiteral("objects/call")) {
        return QStringLiteral("open.call");
    }
    if (method == QStringLiteral("window/showMessage")) {
        return QStringLiteral("ui.message");
    }
    if (method == QStringLiteral("window/showInputBox")
        || method == QStringLiteral("window/showQuickPick")
        || method == QStringLiteral("window/showOpenDialog")
        || method == QStringLiteral("window/showSaveDialog")
        || method == QStringLiteral("window/showSelectFolderDialog")
        || method == QStringLiteral("window/openExternalUrl")
        || method.startsWith(QStringLiteral("window/focus"))) {
        return QStringLiteral("ui.prompt");
    }
    if (method == QStringLiteral("window/createStatusBarItem")
        || method == QStringLiteral("window/clearStatusBarMessage")
        || method == QStringLiteral("window/setProgress")
        || method == QStringLiteral("window/clearProgress")) {
        return QStringLiteral("ui.status");
    }
    if (method == QStringLiteral("app/getInfo")) {
        return QStringLiteral("app.read");
    }
    if (method == QStringLiteral("app/openPreferences")) {
        return QStringLiteral("ui.prompt");
    }
    if (method == QStringLiteral("app/reloadExtensions")) {
        return QStringLiteral("extensions.manage");
    }
    if (method == QStringLiteral("app/requestRestart") || method == QStringLiteral("app/quit")) {
        return QStringLiteral("app.lifecycle");
    }
    if (method.startsWith(QStringLiteral("app/"))) {
        return method.startsWith(QStringLiteral("app/open")) ? QStringLiteral("ui.prompt") : QStringLiteral("app.read");
    }
    if (method == QStringLiteral("contributions/register")) {
        const QString kind = params.value(QStringLiteral("kind")).toString();
        if (kind.startsWith(QStringLiteral("providers/"))) {
            return QStringLiteral("providers.register");
        }
        if (kind.startsWith(QStringLiteral("export/"))) {
            return QStringLiteral("export.write");
        }
        if (kind.startsWith(QStringLiteral("events/"))) {
            return QStringLiteral("events.subscribe");
        }
        return QStringLiteral("ui.contribute");
    }
    if (method == QStringLiteral("events/register")) {
        return QStringLiteral("events.subscribe");
    }
    if (method.startsWith(QStringLiteral("events/"))) {
        return QStringLiteral("events.subscribe");
    }
    if (method.startsWith(QStringLiteral("input/"))) {
        return QStringLiteral("input.listen");
    }
    if (method.startsWith(QStringLiteral("workspace/get")) || method == QStringLiteral("workspace/getActiveDocument")) {
        return QStringLiteral("workspace.read");
    }
    if (method == QStringLiteral("workspace/scanChartFolders")) {
        return QStringLiteral("filesystem.read");
    }
    if (method == QStringLiteral("workspace/setProjectData")) {
        return QStringLiteral("workspace.write");
    }
    if (method == QStringLiteral("workspace/applyDocumentEdit")
        || method == QStringLiteral("workspace/updateChartMetadata")
        || method == QStringLiteral("document/replaceActiveDifficultyText")
        || method == QStringLiteral("document/setActiveDifficulty")) {
        return QStringLiteral("document.edit");
    }
    if (method == QStringLiteral("workspace/save") || method == QStringLiteral("workspace/saveAs")) {
        return QStringLiteral("workspace.write");
    }
    if (method.startsWith(QStringLiteral("workspace/"))) {
        return method.startsWith(QStringLiteral("workspace/get"))
            || method == QStringLiteral("workspace/isDirty")
            ? QStringLiteral("workspace.read")
            : QStringLiteral("workspace.write");
    }
    if (method.startsWith(QStringLiteral("document/get")) || method == QStringLiteral("document/query")) {
        return QStringLiteral("workspace.read");
    }
    if (method == QStringLiteral("document/format")
        || method == QStringLiteral("document/applyTextEdits")
        || method == QStringLiteral("document/createDifficulty")
        || method == QStringLiteral("document/deleteDifficulty")
        || method == QStringLiteral("document/renameDifficulty")) {
        return QStringLiteral("document.edit");
    }
    if (method.startsWith(QStringLiteral("document/"))) {
        return QStringLiteral("document.edit");
    }
    if (method.startsWith(QStringLiteral("editor/get"))) {
        return QStringLiteral("editor.read");
    }
    if (method.startsWith(QStringLiteral("providers/"))) {
        return method.startsWith(QStringLiteral("providers/collect"))
            || method.startsWith(QStringLiteral("providers/show"))
            || method == QStringLiteral("providers/getRegistered")
            ? QStringLiteral("providers.read")
            : QStringLiteral("providers.register");
    }
    if (method == QStringLiteral("editor/showCompletions") || method == QStringLiteral("editor/showCodeActions")) {
        return QStringLiteral("providers.read");
    }
    if (method.startsWith(QStringLiteral("editor/"))) {
        return QStringLiteral("editor.edit");
    }
    if (method.startsWith(QStringLiteral("validation/")) || method.startsWith(QStringLiteral("diagnostics/"))) {
        return QStringLiteral("diagnostics.run");
    }
    if (method.startsWith(QStringLiteral("analysis/"))) {
        return QStringLiteral("analysis.run");
    }
    if (method.startsWith(QStringLiteral("timeline/get"))) {
        return QStringLiteral("timeline.read");
    }
    if (method.startsWith(QStringLiteral("timeline/"))) {
        return QStringLiteral("timeline.control");
    }
    if (method.startsWith(QStringLiteral("preview/get"))) {
        return QStringLiteral("preview.read");
    }
    if (method.startsWith(QStringLiteral("preview/"))) {
        return QStringLiteral("preview.control");
    }
    if (method.startsWith(QStringLiteral("resources/get"))) {
        return QStringLiteral("resources.read");
    }
    if (method.startsWith(QStringLiteral("resources/set"))) {
        return QStringLiteral("resources.write");
    }
    if (method.startsWith(QStringLiteral("export/raw"))) {
        return QStringLiteral("export.raw");
    }
    if (method.startsWith(QStringLiteral("export/get"))) {
        return QStringLiteral("export.read");
    }
    if (method.startsWith(QStringLiteral("export/"))) {
        return QStringLiteral("export.write");
    }
    if (method.startsWith(QStringLiteral("menus/"))) {
        return QStringLiteral("ui.contribute");
    }
    if (method.startsWith(QStringLiteral("ui/"))) {
        return QStringLiteral("ui.contribute");
    }
    if (method.startsWith(QStringLiteral("tasks/"))) {
        return QStringLiteral("tasks.run");
    }
    if (method == QStringLiteral("logs/getPath") || method == QStringLiteral("logs/open")) {
        return QStringLiteral("logs.read");
    }
    if (method == QStringLiteral("logs/append")) {
        return QStringLiteral("logs.write");
    }
    if (method == QStringLiteral("fs/readText") || method == QStringLiteral("fs/exists") || method == QStringLiteral("fs/listDir")) {
        return QStringLiteral("filesystem.read");
    }
    if (method == QStringLiteral("fs/writeText")) {
        return QStringLiteral("filesystem.write");
    }
    if (method.startsWith(QStringLiteral("fs/read"))) {
        return QStringLiteral("filesystem.read");
    }
    if (method.startsWith(QStringLiteral("fs/"))) {
        return method.contains(QStringLiteral("AnyPathWithoutPrompt"))
            ? QStringLiteral("filesystem.unsafe")
            : QStringLiteral("filesystem.write");
    }
    if (method.contains(QStringLiteral("WithoutPrompt")) && method.startsWith(QStringLiteral("net/"))) {
        return QStringLiteral("network.unsafe");
    }
    if (method.startsWith(QStringLiteral("net/"))) {
        return QStringLiteral("network.fetch");
    }
    if (method.startsWith(QStringLiteral("media/generate")) || method.startsWith(QStringLiteral("media/transcode"))) {
        return QStringLiteral("resources.write");
    }
    if (method.startsWith(QStringLiteral("media/"))) {
        return QStringLiteral("resources.read");
    }
    if (method == QStringLiteral("settings/get")) {
        return QStringLiteral("settings.read");
    }
    if (method == QStringLiteral("settings/set")) {
        return QStringLiteral("settings.write");
    }
    if (method.startsWith(QStringLiteral("settings/"))) {
        return method == QStringLiteral("settings/get")
            || method == QStringLiteral("settings/listKeys")
            || method == QStringLiteral("settings/export")
            ? QStringLiteral("settings.read")
            : QStringLiteral("settings.write");
    }
    if (method == QStringLiteral("commands/execute")
        || method == QStringLiteral("commands/executeInternal")
        || method == QStringLiteral("commands/setChecked")) {
        return QStringLiteral("commands.execute");
    }
    if (method == QStringLiteral("commands/getCommands") || method == QStringLiteral("commands/getInternalCommands")) {
        return QStringLiteral("commands.read");
    }
    if (method.startsWith(QStringLiteral("extensions/"))) {
        return QStringLiteral("extensions.manage");
    }
    if (method.startsWith(QStringLiteral("theme/"))) {
        return method == QStringLiteral("theme/getCurrent")
            || method == QStringLiteral("theme/listAvailable")
            || method == QStringLiteral("theme/getColor")
            ? QStringLiteral("settings.read")
            : QStringLiteral("settings.write");
    }
    if (method.startsWith(QStringLiteral("shortcuts/"))) {
        return method == QStringLiteral("shortcuts/list") || method == QStringLiteral("shortcuts/getKeybinding")
            || method == QStringLiteral("shortcuts/getEditable")
            ? QStringLiteral("settings.read")
            : QStringLiteral("settings.write");
    }
    if (method.startsWith(QStringLiteral("nativeDialogs/"))) {
        return QStringLiteral("ui.prompt");
    }
    if (method.startsWith(QStringLiteral("backup/"))) {
        return method.contains(QStringLiteral("list"), Qt::CaseInsensitive)
            || method.contains(QStringLiteral("read"), Qt::CaseInsensitive)
            || method.contains(QStringLiteral("get"), Qt::CaseInsensitive)
            ? QStringLiteral("backup.read")
            : QStringLiteral("backup.write");
    }
    if (method.startsWith(QStringLiteral("clipboard/read"))) {
        return QStringLiteral("clipboard.read");
    }
    if (method.startsWith(QStringLiteral("clipboard/write"))) {
        return QStringLiteral("clipboard.write");
    }
    if (method.startsWith(QStringLiteral("shell/"))) {
        return QStringLiteral("shell.execute");
    }
    if (method.startsWith(QStringLiteral("process/"))) {
        return QStringLiteral("process.manage");
    }
    if (method.startsWith(QStringLiteral("native/"))) {
        return QStringLiteral("native.unsafe");
    }
    if (method.startsWith(QStringLiteral("internal/get"))
        || method.startsWith(QStringLiteral("internal/eval"))) {
        return QStringLiteral("internal.raw");
    }
    if (method.startsWith(QStringLiteral("internal/")) || method.startsWith(QStringLiteral("objects/"))) {
        return QStringLiteral("internal.inspect");
    }
    if (method.startsWith(QStringLiteral("experimental/"))) {
        return QStringLiteral("experimental.invoke");
    }
    if (method.startsWith(QStringLiteral("renderer/"))) {
        return QStringLiteral("renderer.raw");
    }
    if (method.startsWith(QStringLiteral("security/"))) {
        return QStringLiteral("security.override");
    }
    if (method.startsWith(QStringLiteral("updates/"))) {
        return QStringLiteral("updates.modify");
    }
    return QStringLiteral("unknown");
}

bool ExtensionManager::manifestDeclaresPermission(const QString& extensionId, const QString& permission) const
{
    if (permission.isEmpty()) {
        return true;
    }
    for (const ExtensionManifest& manifest : manifests_) {
        if (manifest.qualifiedId() == extensionId) {
            return manifest.permissions.contains(permission);
        }
    }
    return false;
}

QString ExtensionManager::extensionRootPathForId(const QString& extensionId) const
{
    for (const ExtensionManifest& manifest : manifests_) {
        if (manifest.qualifiedId() == extensionId) {
            return manifest.rootPath;
        }
    }
    return QString();
}

bool ExtensionManager::ensurePermission(const QString& extensionId, const QString& method, const QJsonObject& params, QJsonObject* errorResponse)
{
    QString permission = permissionForMethod(method, params);
    if (method == QStringLiteral("contributions/register")) {
        const QString kind = params.value(QStringLiteral("kind")).toString();
        if (kind.startsWith(QStringLiteral("providers/"))) {
            permission = QStringLiteral("providers.register");
        } else if (kind.startsWith(QStringLiteral("export/"))) {
            permission = QStringLiteral("export.write");
        } else if (kind.startsWith(QStringLiteral("tasks/"))) {
            permission = QStringLiteral("tasks.run");
        } else if (kind.startsWith(QStringLiteral("ui/"))) {
            permission = QStringLiteral("ui.contribute");
        }
    }
    if (permission.isEmpty()) {
        return true;
    }
    if (isPermanentlyBlockedApiMethod(method)) {
        const QString error = QStringLiteral("%1 denied: API is blocked in the ordinary v1 extension host.")
                                  .arg(method);
        diagnostics_.append(error);
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("API method blocked"), QJsonObject{
            {QStringLiteral("method"), method},
            {QStringLiteral("extensionId"), extensionId},
            {QStringLiteral("permission"), permission},
            {QStringLiteral("paramsPreview"), devtoolsParamsPreview(params)},
            {QStringLiteral("error"), error},
        });
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    if (isBlockedPermission(permission)) {
        const QString error = QStringLiteral("%1 denied: permission '%2' is blocked in the ordinary v1 extension host.")
                                  .arg(method, permission);
        diagnostics_.append(error);
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("permission blocked"), QJsonObject{
            {QStringLiteral("method"), method},
            {QStringLiteral("extensionId"), extensionId},
            {QStringLiteral("permission"), permission},
            {QStringLiteral("paramsPreview"), devtoolsParamsPreview(params)},
            {QStringLiteral("error"), error},
        });
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    if (extensionId.isEmpty()) {
        const QString error = QStringLiteral("Privileged API '%1' requires an extension context.").arg(method);
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("permission missing extension context"), QJsonObject{
            {QStringLiteral("method"), method},
            {QStringLiteral("permission"), permission},
            {QStringLiteral("paramsPreview"), devtoolsParamsPreview(params)},
            {QStringLiteral("error"), error},
        });
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    if (!manifestDeclaresPermission(extensionId, permission)) {
        const QString error = QStringLiteral("%1 denied: extension '%2' did not declare permission '%3'.")
                                  .arg(method, extensionId, permission);
        diagnostics_.append(error);
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("permission denied"), QJsonObject{
            {QStringLiteral("method"), method},
            {QStringLiteral("extensionId"), extensionId},
            {QStringLiteral("permission"), permission},
            {QStringLiteral("paramsPreview"), devtoolsParamsPreview(params)},
            {QStringLiteral("error"), error},
        });
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    return true;
}

QJsonObject ExtensionManager::devtoolsSnapshot(const QString& extensionId) const
{
    QJsonArray api;
    for (const QJsonObject& descriptor : extensionApiRegistry()) {
        api.append(descriptor);
    }
    QJsonArray uiContributions;
    QJsonArray uiViews;
    QJsonArray runtimeRegistrations;
    if (callbacks_.mainWindowRequest) {
        const QJsonObject contributions = callbacks_.mainWindowRequest(QStringLiteral("ui/getContributions"), QJsonObject{});
        if (!contributions.isEmpty()) {
            uiContributions = contributions.value(QStringLiteral("value")).toArray();
        }
        const QJsonObject registrations = callbacks_.mainWindowRequest(QStringLiteral("extensions/getRuntimeRegistrations"), QJsonObject{});
        if (!registrations.isEmpty()) {
            runtimeRegistrations = registrations.value(QStringLiteral("value")).toArray();
        }
        const QJsonObject views = callbacks_.mainWindowRequest(QStringLiteral("ui/getViews"), QJsonObject{});
        if (!views.isEmpty()) {
            uiViews = views.value(QStringLiteral("value")).toArray();
        }
    }

    const QJsonArray eventCallbacks = runtime_ ? runtime_->registeredEventCallbacksForDevtools() : QJsonArray();
    QJsonObject value{
        {QStringLiteral("extensionId"), extensionId},
        {QStringLiteral("api"), api},
        {QStringLiteral("openBridgeObjects"), extensionOpenBridgeObjectsJson()},
        {QStringLiteral("experimentalRawTargets"), extensionForbiddenOpenTargetsJson()},
        {QStringLiteral("diagnostics"), QJsonArray::fromStringList(diagnostics_)},
        {QStringLiteral("recentCalls"), recentHostCalls_},
        {QStringLiteral("eventCallbackCount"), runtime_ ? runtime_->registeredEventCallbackCount() : 0},
        {QStringLiteral("eventCallbacks"), eventCallbacks},
        {QStringLiteral("uiContributions"), uiContributions},
        {QStringLiteral("uiViews"), uiViews},
        {QStringLiteral("runtimeRegistrations"), runtimeRegistrations},
        {QStringLiteral("logPath"), QDir(extensionLogDirectory()).filePath(QStringLiteral("extensions.log"))},
    };
    QJsonArray extensions;
    QJsonArray extensionDetails;
    for (const ExtensionRecord& record : records_) {
        const QString recordId = record.valid ? record.manifest.qualifiedId() : record.sourcePath;
        extensions.append(QJsonObject{
            {QStringLiteral("id"), recordId},
            {QStringLiteral("name"), record.manifest.name},
            {QStringLiteral("version"), record.manifest.version},
            {QStringLiteral("enabled"), record.enabled},
            {QStringLiteral("valid"), record.valid},
            {QStringLiteral("sourcePath"), record.sourcePath},
            {QStringLiteral("permissions"), QJsonArray::fromStringList(record.manifest.permissions)},
            {QStringLiteral("diagnostic"), record.diagnostic},
        });

        QJsonArray calls;
        QJsonArray errors;
        QJsonArray rawCalls;
        for (const QJsonValue& callValue : recentHostCalls_) {
            const QJsonObject call = callValue.toObject();
            if (call.value(QStringLiteral("extensionId")).toString() != recordId) {
                continue;
            }
            calls.append(call);
            if (!call.value(QStringLiteral("ok")).toBool(true)) {
                errors.append(call);
            }
            if (isRawOrExperimentalCall(call)) {
                rawCalls.append(call);
            }
        }

        QJsonArray detailDiagnostics;
        if (!record.diagnostic.trimmed().isEmpty()) {
            detailDiagnostics.append(record.diagnostic);
        }
        for (const QString& diagnostic : diagnostics_) {
            if (diagnostic.contains(recordId) || diagnostic.contains(record.sourcePath)) {
                detailDiagnostics.append(diagnostic);
            }
        }

        const QJsonArray contributions = filterObjectsByExtension(uiContributions, recordId);
        const QJsonArray registrations = filterObjectsByExtension(runtimeRegistrations, recordId);
        const QJsonArray views = filterObjectsByExtension(uiViews, recordId);
        const QJsonArray callbacks = filterObjectsByExtension(eventCallbacks, recordId);
        extensionDetails.append(QJsonObject{
            {QStringLiteral("id"), recordId},
            {QStringLiteral("name"), record.manifest.name},
            {QStringLiteral("version"), record.manifest.version},
            {QStringLiteral("enabled"), record.enabled},
            {QStringLiteral("valid"), record.valid},
            {QStringLiteral("sourcePath"), record.sourcePath},
            {QStringLiteral("permissions"), QJsonArray::fromStringList(record.manifest.permissions)},
            {QStringLiteral("diagnostics"), detailDiagnostics},
            {QStringLiteral("recentCalls"), calls},
            {QStringLiteral("recentErrors"), errors},
            {QStringLiteral("experimentalRawCalls"), rawCalls},
            {QStringLiteral("uiContributions"), contributions},
            {QStringLiteral("runtimeRegistrations"), registrations},
            {QStringLiteral("uiViews"), views},
            {QStringLiteral("eventCallbacks"), callbacks},
            {QStringLiteral("providers"), filterContributionsByKindPrefix(registrations, QStringLiteral("providers/"))},
            {QStringLiteral("exportHooks"), filterContributionsByKindPrefix(registrations, QStringLiteral("export/"))},
        });
    }
    value.insert(QStringLiteral("extensions"), extensions);
    value.insert(QStringLiteral("extensionDetails"), extensionDetails);
    return value;
}

QJsonObject ExtensionManager::devtoolsDiagnose(const QString& extensionId, const QJsonObject& params) const
{
    const QString requestedId = params.value(QStringLiteral("id")).toString();
    QString method = params.value(QStringLiteral("method")).toString();
    QJsonObject descriptor;
    if (!requestedId.trimmed().isEmpty()) {
        descriptor = findApiDescriptor(requestedId);
        method = descriptor.value(QStringLiteral("method")).toString(method);
    }
    if (descriptor.isEmpty() && !method.trimmed().isEmpty()) {
        for (const QJsonObject& item : extensionApiRegistry()) {
            const QString descriptorMethod = item.value(QStringLiteral("method")).toString();
            const bool matches = descriptorMethod == method
                || (descriptorMethod.endsWith(QStringLiteral("/*"))
                    && method.startsWith(descriptorMethod.left(descriptorMethod.size() - 1)));
            if (matches) {
                descriptor = item;
                break;
            }
        }
    }
    const QString permission = permissionForMethod(method);
    return QJsonObject{
        {QStringLiteral("id"), requestedId},
        {QStringLiteral("method"), method},
        {QStringLiteral("descriptor"), descriptor},
        {QStringLiteral("implemented"), !method.trimmed().isEmpty() && methodIsImplementedPublicApi(method)},
        {QStringLiteral("requiredPermission"), permission},
        {QStringLiteral("extensionId"), extensionId},
        {QStringLiteral("manifestDeclaresPermission"), manifestDeclaresPermission(extensionId, permission)},
        {QStringLiteral("blockedByMethodHook"), isPermanentlyBlockedApiMethod(method)},
        {QStringLiteral("blockedByPermissionHook"), isBlockedPermission(permission)},
    };
}

void ExtensionManager::appendDevtoolsCall(const QString& method, const QJsonObject& params, const QJsonObject& result, qint64 elapsedMs)
{
    if (method == QStringLiteral("devtools/recentCalls")) {
        return;
    }
    QJsonObject entry{
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("method"), method},
        {QStringLiteral("extensionId"), params.value(QStringLiteral("extensionId")).toString()},
        {QStringLiteral("permission"), permissionForMethod(method)},
        {QStringLiteral("ok"), result.value(QStringLiteral("ok")).toBool(!result.contains(QStringLiteral("error")))},
        {QStringLiteral("error"), result.value(QStringLiteral("error")).toString()},
        {QStringLiteral("elapsedMs"), static_cast<double>(elapsedMs)},
        {QStringLiteral("paramsPreview"), devtoolsParamsPreview(params)},
    };
    recentHostCalls_.append(entry);
    while (recentHostCalls_.size() > 200) {
        recentHostCalls_.removeAt(0);
    }
    if (!entry.value(QStringLiteral("ok")).toBool(true)) {
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("host API call failed"), entry);
    } else if (isRawOrExperimentalCall(entry)
               || method == QStringLiteral("process/spawn")
               || method == QStringLiteral("shell/execute")) {
        appendExtensionLog(QStringLiteral("info"), QStringLiteral("host API call accepted"), entry);
    }
}

void ExtensionManager::refreshMenuSelectionIcons()
{
    for (QAction* action : std::as_const(commandActions_)) {
        if (action != nullptr && action->isCheckable()) {
            action->setIcon(UiTheme::menuSelectionCheckIcon(action->isChecked()));
        }
    }
}

void ExtensionManager::dispatchRuntimeEventForHostResult(const QString& method, const QJsonObject& params, const QJsonObject& result)
{
    if (runtime_ == nullptr || !runtime_->isRunning()) {
        return;
    }
    const bool ok = result.value(QStringLiteral("ok")).toBool(!result.contains(QStringLiteral("error")));
    if (!ok) {
        return;
    }
    QJsonObject event{
        {QStringLiteral("sourceMethod"), method},
        {QStringLiteral("extensionId"), params.value(QStringLiteral("extensionId")).toString()},
    };
    if (method == QStringLiteral("workspace/applyDocumentEdit")
        || method == QStringLiteral("document/edit")
        || method == QStringLiteral("document/replaceActiveDifficultyText")
        || method == QStringLiteral("document/applyTextEdits")
        || method == QStringLiteral("document/format")
        || method == QStringLiteral("document/createDifficulty")
        || method == QStringLiteral("document/deleteDifficulty")
        || method == QStringLiteral("document/renameDifficulty")
        || method == QStringLiteral("workspace/updateChartMetadata")) {
        if (callbacks_.activeDocument) {
            const ExtensionDocumentSnapshot snapshot = callbacks_.activeDocument();
            event.insert(QStringLiteral("uri"), snapshot.uri);
            event.insert(QStringLiteral("languageId"), snapshot.languageId);
            event.insert(QStringLiteral("activeDifficultyId"), snapshot.activeDifficultyId);
            event.insert(QStringLiteral("dirty"), snapshot.dirty);
            event.insert(QStringLiteral("textLength"), snapshot.text.size());
        }
        runtime_->dispatchEvent(QStringLiteral("events/document.onDidChangeText"), event);
        return;
    }
    if (method == QStringLiteral("workspace/save") || method == QStringLiteral("workspace/saveAs")) {
        if (callbacks_.activeDocument) {
            const ExtensionDocumentSnapshot snapshot = callbacks_.activeDocument();
            event.insert(QStringLiteral("uri"), snapshot.uri);
            event.insert(QStringLiteral("dirty"), snapshot.dirty);
        }
        runtime_->dispatchEvent(QStringLiteral("events/workspace.onDidSaveDocument"), event);
        return;
    }
    if (method == QStringLiteral("timeline/seek") || method == QStringLiteral("preview/seek")) {
        event.insert(QStringLiteral("second"), params.value(QStringLiteral("second")).toDouble(
            params.value(QStringLiteral("time")).toDouble(params.value(QStringLiteral("seconds")).toDouble())));
        runtime_->dispatchEvent(QStringLiteral("events/timeline.onDidSeek"), event);
        return;
    }
    if (method == QStringLiteral("preview/play")
        || method == QStringLiteral("preview/pause")
        || method == QStringLiteral("preview/stop")
        || method == QStringLiteral("preview/setSpeed")) {
        event.insert(QStringLiteral("state"), result.value(QStringLiteral("value")));
        if (callbacks_.mainWindowRequest) {
            const QJsonObject state = callbacks_.mainWindowRequest(QStringLiteral("preview/getState"), QJsonObject{});
            if (!state.isEmpty()) {
                event.insert(QStringLiteral("state"), state.value(QStringLiteral("value")));
            }
        }
        runtime_->dispatchEvent(QStringLiteral("events/preview.onDidChangeState"), event);
    }
}

QJsonObject ExtensionManager::handleHostRequest(const QString& method, const QJsonObject& params)
{
    QElapsedTimer timer;
    timer.start();
    QJsonObject result = handleHostRequestCore(method, params);
    appendDevtoolsCall(method, params, result, timer.elapsed());
    dispatchRuntimeEventForHostResult(method, params, result);
    return result;
}

QJsonObject ExtensionManager::handleHostRequestCore(const QString& method, const QJsonObject& params)
{
    QJsonObject permissionError;
    const QString extensionId = params.value(QStringLiteral("extensionId")).toString();
    if (!ensurePermission(extensionId, method, params, &permissionError)) {
        return permissionError;
    }
    if (method == QStringLiteral("log")) {
        appendExtensionLog(QStringLiteral("info"), QStringLiteral("extension log"), QJsonObject{
            {QStringLiteral("extensionId"), extensionId},
            {QStringLiteral("message"), params.value(QStringLiteral("message")).toString()},
        });
        if (callbacks_.logMessage) {
            callbacks_.logMessage(params.value(QStringLiteral("message")).toString());
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("devtools/snapshot")) {
        return okValue(devtoolsSnapshot(extensionId));
    }
    if (method == QStringLiteral("devtools/diagnose")) {
        return okValue(devtoolsDiagnose(extensionId, params));
    }
    if (method == QStringLiteral("devtools/recentCalls")) {
        return okValue(recentHostCalls_);
    }
    if (method == QStringLiteral("experimental/raw/inspect")) {
        const QString targetId = params.value(QStringLiteral("target")).toString(
            params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString()));
        const QJsonObject descriptor = extensionDescribeForbiddenOpenTarget(targetId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown experimental raw target: %1").arg(targetId));
        }
        return okValue(descriptor);
    }
    if (method == QStringLiteral("experimental/raw/call")) {
        const QString targetId = params.value(QStringLiteral("target")).toString(
            params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString()));
        const QJsonObject descriptor = extensionDescribeForbiddenOpenTarget(targetId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown experimental raw target: %1").arg(targetId));
        }
        return okValue(QJsonObject{
            {QStringLiteral("target"), targetId},
            {QStringLiteral("experimentalRaw"), true},
            {QStringLiteral("rawAccess"), true},
            {QStringLiteral("accepted"), true},
            {QStringLiteral("descriptor"), descriptor},
            {QStringLiteral("params"), params},
        });
    }
    if (method == QStringLiteral("ui/registerPetOverlay")) {
        const QString extensionRootPath = extensionRootPathForId(extensionId);
        if (extensionRootPath.isEmpty()) {
            return errorObject(QStringLiteral("ui.registerPetOverlay denied: unknown extension '%1'.").arg(extensionId));
        }
        QJsonObject overlay = params;
        QString error;
        if (!preparePetOverlay(&overlay, extensionRootPath, &error)) {
            return errorObject(error);
        }
        overlay.insert(QStringLiteral("extensionId"), extensionId);
        if (!callbacks_.mainWindowRequest) {
            return errorObject(QStringLiteral("UI host is not available for pet overlays."));
        }
        return callbacks_.mainWindowRequest(QStringLiteral("ui/registerPetOverlay"), overlay);
    }
    if (method == QStringLiteral("api/list")) {
        QJsonArray array;
        for (const QJsonObject& descriptor : extensionApiRegistry()) {
            array.append(descriptor);
        }
        return okValue(array);
    }
    if (method == QStringLiteral("api/has")) {
        return okValue(!findApiDescriptor(params.value(QStringLiteral("id")).toString()).isEmpty());
    }
    if (method == QStringLiteral("api/describe")) {
        const QJsonObject descriptor = findApiDescriptor(params.value(QStringLiteral("id")).toString());
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown API: %1").arg(params.value(QStringLiteral("id")).toString()));
        }
        return okValue(descriptor);
    }
    if (method == QStringLiteral("api/describeNamespace")) {
        const QString namespaceId = params.value(QStringLiteral("namespace")).toString(
            params.value(QStringLiteral("id")).toString()).trimmed();
        if (namespaceId.isEmpty()) {
            return errorObject(QStringLiteral("Missing namespace."));
        }
        QJsonArray array;
        const QString prefix = namespaceId.endsWith(QLatin1Char('.')) ? namespaceId : namespaceId + QLatin1Char('.');
        for (const QJsonObject& descriptor : extensionApiRegistry()) {
            if (descriptor.value(QStringLiteral("id")).toString().startsWith(prefix)) {
                array.append(descriptor);
            }
        }
        return okValue(array);
    }
    if (method == QStringLiteral("api/request")) {
        const QString requestedId = params.value(QStringLiteral("id")).toString();
        const QString reason = params.value(QStringLiteral("reason")).toString();
        const QString error = QStringLiteral("API requested by %1: %2%3")
                                  .arg(extensionId,
                                       requestedId.isEmpty() ? QStringLiteral("<missing id>") : requestedId,
                                       reason.isEmpty() ? QString() : QStringLiteral(" - %1").arg(reason));
        diagnostics_.append(error);
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(error));
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("experimental/invoke")) {
        const QString id = params.value(QStringLiteral("id")).toString(
            params.value(QStringLiteral("target")).toString(params.value(QStringLiteral("method")).toString()));
        QJsonObject forwarded = params.value(QStringLiteral("params")).toObject();
        if (forwarded.isEmpty()) {
            forwarded = params;
            forwarded.remove(QStringLiteral("id"));
            forwarded.remove(QStringLiteral("target"));
            forwarded.remove(QStringLiteral("method"));
            forwarded.remove(QStringLiteral("params"));
        }
        forwarded.insert(QStringLiteral("extensionId"), extensionId);
        if (id == QStringLiteral("shell.execute") || id == QStringLiteral("shell/execute")) {
            return handleHostRequest(QStringLiteral("shell/execute"), forwarded);
        }
        if (id == QStringLiteral("process.spawn") || id == QStringLiteral("process/spawn")) {
            return handleHostRequest(QStringLiteral("process/spawn"), forwarded);
        }
        if (id == QStringLiteral("native") || id == QStringLiteral("native.raw") || id == QStringLiteral("native/raw")) {
            return handleHostRequest(QStringLiteral("native/raw"), forwarded);
        }
        if (id == QStringLiteral("internal.raw") || id == QStringLiteral("internal/raw")) {
            return handleHostRequest(QStringLiteral("internal/raw"), forwarded);
        }
        if (id == QStringLiteral("renderer.raw") || id == QStringLiteral("renderer/raw")) {
            return handleHostRequest(QStringLiteral("renderer/raw"), forwarded);
        }
        if (id == QStringLiteral("export.raw") || id == QStringLiteral("export/raw")) {
            return handleHostRequest(QStringLiteral("export/raw"), forwarded);
        }
        if (id == QStringLiteral("security") || id == QStringLiteral("security.raw") || id == QStringLiteral("security/raw")) {
            return handleHostRequest(QStringLiteral("security/raw"), forwarded);
        }
        if (id == QStringLiteral("updates") || id == QStringLiteral("updates.raw") || id == QStringLiteral("updates/raw")) {
            return handleHostRequest(QStringLiteral("updates/raw"), forwarded);
        }
        return experimentalRawAccepted(id.isEmpty() ? QStringLiteral("experimental") : id,
                                       QStringLiteral("experimental/invoke"),
                                       forwarded.value(QStringLiteral("member")).toString(),
                                       forwarded);
    }
    if (method == QStringLiteral("api/call")) {
        const QString apiId = params.value(QStringLiteral("id")).toString();
        const QJsonObject descriptor = findApiDescriptor(apiId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown API: %1").arg(apiId));
        }
        const QString status = descriptor.value(QStringLiteral("status")).toString();
        if (status != QStringLiteral("implemented")) {
            return errorObject(QStringLiteral("API '%1' is %2 and cannot be called in the ordinary v1 extension host.")
                                   .arg(apiId, status));
        }
        QString targetMethod = descriptor.value(QStringLiteral("method")).toString();
        if (targetMethod.endsWith(QStringLiteral("/*"))) {
            targetMethod.chop(1);
            QString member = params.value(QStringLiteral("member")).toString(
                params.value(QStringLiteral("method")).toString(params.value(QStringLiteral("op")).toString()));
            if (member.trimmed().isEmpty()) {
                if (apiId == QStringLiteral("media")) {
                    member = QStringLiteral("getInfo");
                } else if (apiId == QStringLiteral("theme")) {
                    member = QStringLiteral("getCurrent");
                } else if (apiId == QStringLiteral("backup")) {
                    member = QStringLiteral("list");
                } else if (apiId == QStringLiteral("shortcuts")) {
                    member = QStringLiteral("list");
                }
            }
            targetMethod += member;
        }
        if (isPermanentlyBlockedApiMethod(targetMethod)) {
            return errorObject(QStringLiteral("API method '%1' is permanently blocked.").arg(targetMethod));
        }
        QJsonObject forwarded = params;
        forwarded.remove(QStringLiteral("id"));
        if (apiId == QStringLiteral("window.showInformationMessage")) {
            forwarded.insert(QStringLiteral("severity"), QStringLiteral("info"));
        } else if (apiId == QStringLiteral("window.showWarningMessage")) {
            forwarded.insert(QStringLiteral("severity"), QStringLiteral("warning"));
        } else if (apiId == QStringLiteral("window.showErrorMessage")) {
            forwarded.insert(QStringLiteral("severity"), QStringLiteral("error"));
        } else if (apiId == QStringLiteral("ui.registerSidebarView")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/sidebarView"));
        } else if (apiId == QStringLiteral("ui.registerBottomTabView")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/bottomTabView"));
        } else if (apiId == QStringLiteral("ui.registerPreferencesPage")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/preferencesPage"));
        } else if (apiId == QStringLiteral("ui.registerFloatingPanel")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/floatingPanel"));
        } else if (apiId == QStringLiteral("ui.registerToolbarButton")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/toolbarButton"));
        } else if (apiId == QStringLiteral("providers.registerHoverProvider")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("providers/hover"));
        } else if (apiId == QStringLiteral("providers.registerCompletionProvider")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("providers/completion"));
        } else if (apiId == QStringLiteral("providers.registerCodeActionProvider")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("providers/codeAction"));
        } else if (apiId == QStringLiteral("export.hooks")) {
            forwarded.insert(QStringLiteral("kind"), forwarded.value(QStringLiteral("kind")).toString(QStringLiteral("export/hook")));
        }
        QJsonObject forwardedPermissionError;
        if (!ensurePermission(extensionId, targetMethod, forwarded, &forwardedPermissionError)) {
            return forwardedPermissionError;
        }
        return handleHostRequest(targetMethod, forwarded);
    }
    if (method == QStringLiteral("api/invoke")) {
        const QString targetMethod = params.value(QStringLiteral("method")).toString().trimmed();
        if (targetMethod.isEmpty()) {
            return errorObject(QStringLiteral("Missing public host method."));
        }
        if (targetMethod.startsWith(QStringLiteral("api/"))) {
            return errorObject(QStringLiteral("api.invoke cannot invoke api/* methods."));
        }
        if (!methodIsImplementedPublicApi(targetMethod)) {
            return errorObject(QStringLiteral("API method '%1' is not an implemented public v1 method. Use api.request to record the need.").arg(targetMethod));
        }
        if (isPermanentlyBlockedApiMethod(targetMethod)) {
            return errorObject(QStringLiteral("API method '%1' is permanently blocked.").arg(targetMethod));
        }
        QJsonObject forwarded = params.value(QStringLiteral("params")).toObject();
        if (forwarded.isEmpty()) {
            forwarded = params;
            forwarded.remove(QStringLiteral("method"));
            forwarded.remove(QStringLiteral("params"));
        }
        QJsonObject forwardedPermissionError;
        if (!ensurePermission(extensionId, targetMethod, forwarded, &forwardedPermissionError)) {
            return forwardedPermissionError;
        }
        return handleHostRequest(targetMethod, forwarded);
    }
    if (method == QStringLiteral("open/list")) {
        return okValue(extensionOpenBridgeObjectsJson());
    }
    if (method == QStringLiteral("open/forbiddenTargets")) {
        return okValue(extensionForbiddenOpenTargetsJson());
    }
    if (method == QStringLiteral("open/describeForbiddenTarget")) {
        const QString targetId = params.value(QStringLiteral("target")).toString(params.value(QStringLiteral("id")).toString());
        const QJsonObject descriptor = extensionDescribeForbiddenOpenTarget(targetId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Target is not marked forbidden: %1").arg(targetId));
        }
        return okValue(descriptor);
    }
    if (method == QStringLiteral("open/describe")) {
        const QString objectId = params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString());
        const QJsonObject forbidden = extensionDescribeForbiddenOpenTarget(objectId);
        if (!forbidden.isEmpty()) {
            return okValue(forbidden);
        }
        const QJsonObject descriptor = extensionOpenBridgeDescribeObject(objectId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown Open Bridge object: %1").arg(objectId));
        }
        return okValue(descriptor);
    }
    if (method == QStringLiteral("open/call") || method == QStringLiteral("objects/call")) {
        const QString objectId = params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString());
        const QString member = params.value(QStringLiteral("member")).toString(params.value(QStringLiteral("method")).toString());
        if (extensionIsForbiddenOpenTarget(objectId)) {
            const QJsonObject forbidden = extensionDescribeForbiddenOpenTarget(objectId);
            return errorObject(QStringLiteral("Open target '%1' is forbidden: %2")
                                   .arg(objectId, forbidden.value(QStringLiteral("reason")).toString()));
        }
        const QJsonObject objectDescriptor = extensionOpenBridgeDescribeObject(objectId);
        if (objectDescriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown Open Bridge object: %1").arg(objectId));
        }
        const QString objectPermission = objectDescriptor.value(QStringLiteral("permission")).toString();
        if (!objectPermission.isEmpty() && !manifestDeclaresPermission(extensionId, objectPermission)) {
            return errorObject(QStringLiteral("open.call denied: extension '%1' did not declare permission '%2'.")
                                   .arg(extensionId, objectPermission));
        }
        const QJsonObject methodDescriptor = extensionOpenBridgeDescribeMethod(objectId, member);
        if (methodDescriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown Open Bridge method: %1.%2").arg(objectId, member));
        }
        const QString status = methodDescriptor.value(QStringLiteral("status")).toString();
        if (status != QStringLiteral("implemented")) {
            return errorObject(QStringLiteral("Open Bridge method '%1.%2' is %3 and cannot be called.")
                                   .arg(objectId, member, status));
        }
        const QString methodPermission = methodDescriptor.value(QStringLiteral("permission")).toString();
        if (!methodPermission.isEmpty() && !manifestDeclaresPermission(extensionId, methodPermission)) {
            return errorObject(QStringLiteral("open.call denied: extension '%1' did not declare permission '%2'.")
                                   .arg(extensionId, methodPermission));
        }
        QJsonObject forwarded = params.value(QStringLiteral("args")).toObject(params.value(QStringLiteral("params")).toObject());
        if (forwarded.isEmpty()) {
            forwarded = params;
            forwarded.remove(QStringLiteral("object"));
            forwarded.remove(QStringLiteral("id"));
            forwarded.remove(QStringLiteral("member"));
            forwarded.remove(QStringLiteral("method"));
            forwarded.remove(QStringLiteral("args"));
            forwarded.remove(QStringLiteral("params"));
        }
        forwarded.insert(QStringLiteral("object"), objectId);
        forwarded.insert(QStringLiteral("target"), objectId);
        forwarded.insert(QStringLiteral("member"), member);
        forwarded.insert(QStringLiteral("extensionId"), extensionId);
        const QString targetMethod = methodDescriptor.value(QStringLiteral("hostMethod")).toString();
        if (!targetMethod.isEmpty()) {
            QJsonObject forwardedPermissionError;
            if (!ensurePermission(extensionId, targetMethod, forwarded, &forwardedPermissionError)) {
                return forwardedPermissionError;
            }
            return handleHostRequest(targetMethod, forwarded);
        }
        const QString command = methodDescriptor.value(QStringLiteral("command")).toString();
        if (!command.isEmpty() && callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(QStringLiteral("commands/executeInternal"), QJsonObject{
                {QStringLiteral("extensionId"), extensionId},
                {QStringLiteral("command"), command},
                {QStringLiteral("args"), forwarded},
            });
            return response.isEmpty()
                ? errorObject(QStringLiteral("Open Bridge command route returned no response: %1.%2").arg(objectId, member))
                : response;
        }
        return errorObject(QStringLiteral("Open Bridge method has no callable route: %1.%2").arg(objectId, member));
    }
    if (method == QStringLiteral("window/showMessage")) {
        if (callbacks_.showMessage) {
            callbacks_.showMessage(
                params.value(QStringLiteral("severity")).toString(QStringLiteral("info")),
                params.value(QStringLiteral("message")).toString());
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("workspace/getActiveDocument")) {
        const ExtensionDocumentSnapshot snapshot = callbacks_.activeDocument ? callbacks_.activeDocument() : ExtensionDocumentSnapshot();
        return QJsonObject{
            {QStringLiteral("uri"), snapshot.uri},
            {QStringLiteral("languageId"), snapshot.languageId},
            {QStringLiteral("text"), snapshot.text},
            {QStringLiteral("activeDifficultyId"), snapshot.activeDifficultyId},
            {QStringLiteral("dirty"), snapshot.dirty},
        };
    }
    if (method == QStringLiteral("workspace/applyDocumentEdit")) {
        QString error;
        const QString text = params.value(QStringLiteral("text")).toString();
        const bool ok = callbacks_.replaceActiveDocumentText
            ? callbacks_.replaceActiveDocumentText(text, &error)
            : false;
        return QJsonObject{{QStringLiteral("ok"), ok}, {QStringLiteral("error"), error}};
    }
    if (method == QStringLiteral("diagnostics/validateDocument")) {
        const bool ok = callbacks_.validateActiveDocument ? callbacks_.validateActiveDocument() : false;
        return QJsonObject{{QStringLiteral("ok"), ok}};
    }
    if (method == QStringLiteral("commands/register")) {
        const QString command = params.value(QStringLiteral("command")).toString();
        return QJsonObject{{QStringLiteral("ok"), commandOwnerById_.contains(command)}};
    }
    if (method == QStringLiteral("commands/getCommands")) {
        QJsonArray commands;
        for (const ExtensionManifest& manifest : manifests_) {
            for (const ExtensionCommandContribution& command : manifest.commands) {
                commands.append(QJsonObject{
                    {QStringLiteral("command"), command.id},
                    {QStringLiteral("title"), command.title},
                    {QStringLiteral("category"), command.category},
                    {QStringLiteral("extensionId"), manifest.qualifiedId()},
                });
            }
        }
        return okValue(commands);
    }
    if (method == QStringLiteral("commands/setChecked")) {
        const QString command = params.value(QStringLiteral("command")).toString();
        const QString extensionId = params.value(QStringLiteral("extensionId")).toString();
        if (commandOwnerById_.value(command) != extensionId) {
            return errorObject(QStringLiteral("Extensions may only change their own command state."));
        }
        QAction* action = commandActions_.value(command);
        if (action == nullptr) {
            return errorObject(QStringLiteral("Command menu action is unavailable: %1").arg(command));
        }
        action->setCheckable(true);
        action->setChecked(params.value(QStringLiteral("checked")).toBool());
        action->setIcon(UiTheme::menuSelectionCheckIcon(action->isChecked()));
        return okValue(QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("checked"), action->isChecked()},
        });
    }
    if (method == QStringLiteral("commands/getInternalCommands")) {
        if (!callbacks_.mainWindowRequest) {
            return errorObject(QStringLiteral("MainWindow command registry is not available."));
        }
        const QJsonObject response = callbacks_.mainWindowRequest(method, params);
        return response.isEmpty()
            ? errorObject(QStringLiteral("MainWindow command registry returned no response."))
            : response;
    }
    if (method == QStringLiteral("commands/describe")) {
        const QString commandId = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString());
        const ExtensionCommandContribution contribution = commandContribution(commandId);
        if (contribution.id.isEmpty()) {
            return errorObject(QStringLiteral("Unknown command: %1").arg(commandId));
        }
        return okValue(QJsonObject{
            {QStringLiteral("command"), contribution.id},
            {QStringLiteral("title"), contribution.title},
            {QStringLiteral("category"), contribution.category},
            {QStringLiteral("extensionId"), commandOwnerById_.value(commandId)},
            {QStringLiteral("available"), commandOwnerById_.contains(commandId)},
        });
    }
    if (method == QStringLiteral("commands/execute")) {
        const QString command = params.value(QStringLiteral("command")).toString();
        if (!commandOwnerById_.contains(command)) {
            return errorObject(QStringLiteral("Unknown or unavailable command: %1").arg(command));
        }
        invokeCommand(command);
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("app/getInfo")) {
        const qreal dpiScale = QCoreApplication::instance() != nullptr
            ? QCoreApplication::instance()->property("devicePixelRatio").toReal()
            : 0.0;
        return okValue(QJsonObject{
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("applicationName"), QCoreApplication::applicationName()},
            {QStringLiteral("processId"), static_cast<double>(QCoreApplication::applicationPid())},
            {QStringLiteral("platform"), QSysInfo::productType()},
            {QStringLiteral("platformVersion"), QSysInfo::productVersion()},
            {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("commitHash"), QString()},
            {QStringLiteral("executablePath"), QCoreApplication::applicationFilePath()},
            {QStringLiteral("installRoot"), QDir(QCoreApplication::applicationDirPath()).dirName().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0
                ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."))
                : QCoreApplication::applicationDirPath()},
            {QStringLiteral("extensionsRoot"), userExtensionsDirectory()},
            {QStringLiteral("logsRoot"), extensionLogDirectory()},
            {QStringLiteral("configRoot"), QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)},
            {QStringLiteral("tempRoot"), QDir::tempPath()},
            {QStringLiteral("locale"), QLocale::system().name()},
            {QStringLiteral("theme"), UiText::themeTokenFromPreferencesObject(UiText::loadPreferencesObject())},
            {QStringLiteral("dpiScale"), dpiScale > 0.0 ? dpiScale : 1.0},
            {QStringLiteral("portableMode"), false},
            {QStringLiteral("restartRequired"), false},
        });
    }
    if (method == QStringLiteral("app/restartRequired")) {
        return okValue(false);
    }
    if (method == QStringLiteral("app/requestRestart")) {
        return errorObject(QStringLiteral("Restart is not available from the embedded extension runtime yet."));
    }
    if (method == QStringLiteral("app/quit")) {
        if (QCoreApplication::instance() != nullptr) {
            QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("app/reloadExtensions")) {
        refreshExtensions();
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method.startsWith(QStringLiteral("fs/"))) {
        const QString path = params.value(QStringLiteral("path")).toString();
        const QFileInfo info(path);
        if (method == QStringLiteral("fs/stat")) {
            return okValue(QJsonObject{
                {QStringLiteral("path"), info.absoluteFilePath()},
                {QStringLiteral("exists"), info.exists()},
                {QStringLiteral("directory"), info.isDir()},
                {QStringLiteral("file"), info.isFile()},
                {QStringLiteral("size"), static_cast<double>(info.exists() ? info.size() : 0)},
                {QStringLiteral("lastModified"), info.exists() ? info.lastModified().toString(Qt::ISODateWithMs) : QString()},
            });
        }
        if (method == QStringLiteral("fs/exists")) {
            return okValue(info.exists());
        }
        if (method == QStringLiteral("fs/listDir")) {
            QDir dir(path);
            if (!dir.exists()) {
                return errorObject(QStringLiteral("Directory does not exist: %1").arg(path));
            }
            QJsonArray entries;
            for (const QFileInfo& entry : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
                entries.append(QJsonObject{
                    {QStringLiteral("name"), entry.fileName()},
                    {QStringLiteral("path"), entry.absoluteFilePath()},
                    {QStringLiteral("directory"), entry.isDir()},
                    {QStringLiteral("size"), static_cast<double>(entry.size())},
                });
            }
            return okValue(entries);
        }
        if (method == QStringLiteral("fs/readText")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return errorObject(QStringLiteral("Cannot read file: %1").arg(file.errorString()));
            }
            return okValue(QString::fromUtf8(file.readAll()));
        }
        if (method == QStringLiteral("fs/readBytes")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return errorObject(QStringLiteral("Cannot read file: %1").arg(file.errorString()));
            }
            return okValue(QString::fromLatin1(file.readAll().toBase64()));
        }
        if (method == QStringLiteral("fs/writeText")) {
            QFile file(path);
            if (!QDir().mkpath(info.absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return errorObject(QStringLiteral("Cannot write file: %1").arg(file.errorString()));
            }
            file.write(params.value(QStringLiteral("text")).toString().toUtf8());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("fs/writeBytes")) {
            const QByteArray bytes = QByteArray::fromBase64(params.value(QStringLiteral("base64")).toString().toLatin1());
            QFile file(path);
            if (!QDir().mkpath(info.absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return errorObject(QStringLiteral("Cannot write file: %1").arg(file.errorString()));
            }
            file.write(bytes);
            return okValue(QJsonObject{{QStringLiteral("bytes"), bytes.size()}});
        }
        if (method == QStringLiteral("fs/createDir")) {
            return QJsonObject{{QStringLiteral("ok"), QDir().mkpath(path)}};
        }
        if (method == QStringLiteral("fs/delete")) {
            const bool recursive = params.value(QStringLiteral("recursive")).toBool(false);
            bool ok = false;
            if (info.isDir()) {
                ok = recursive ? QDir(path).removeRecursively() : QDir().rmdir(path);
            } else {
                ok = QFile::remove(path);
            }
            return QJsonObject{{QStringLiteral("ok"), ok}};
        }
        if (method == QStringLiteral("fs/copy")) {
            const QString targetPath = params.value(QStringLiteral("targetPath")).toString(params.value(QStringLiteral("to")).toString());
            if (targetPath.isEmpty()) {
                return errorObject(QStringLiteral("Missing copy targetPath."));
            }
            QString error;
            const bool ok = info.isDir()
                ? copyDirectoryRecursively(path, targetPath, &error)
                : (QDir().mkpath(QFileInfo(targetPath).absolutePath()) && QFile::copy(path, targetPath));
            return ok ? okValue(targetPath) : errorObject(error.isEmpty() ? QStringLiteral("Copy failed.") : error);
        }
        if (method == QStringLiteral("fs/move") || method == QStringLiteral("fs/rename")) {
            const QString targetPath = params.value(QStringLiteral("targetPath")).toString(params.value(QStringLiteral("to")).toString());
            if (targetPath.isEmpty()) {
                return errorObject(QStringLiteral("Missing targetPath."));
            }
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            return QJsonObject{{QStringLiteral("ok"), QFile::rename(path, targetPath)}};
        }
        if (method == QStringLiteral("fs/openInExplorer")) {
            const QString target = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
            return QJsonObject{{QStringLiteral("ok"), QDesktopServices::openUrl(QUrl::fromLocalFile(target))}};
        }
        if (method == QStringLiteral("fs/readAnyPathWithoutPrompt")) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return errorObject(QStringLiteral("Cannot read file: %1").arg(file.errorString()));
            }
            return okValue(QString::fromUtf8(file.readAll()));
        }
        if (method == QStringLiteral("fs/writeAnyPathWithoutPrompt")) {
            QFile file(path);
            if (!QDir().mkpath(info.absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return errorObject(QStringLiteral("Cannot write file: %1").arg(file.errorString()));
            }
            file.write(params.value(QStringLiteral("text")).toString().toUtf8());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("fs/deleteAnyPathWithoutPrompt")) {
            const bool recursive = params.value(QStringLiteral("recursive")).toBool(false);
            const bool ok = info.isDir()
                ? (recursive ? QDir(path).removeRecursively() : QDir().rmdir(path))
                : QFile::remove(path);
            return QJsonObject{{QStringLiteral("ok"), ok}};
        }
    }
    if (method == QStringLiteral("net/fetch") || method == QStringLiteral("net/download")
        || method == QStringLiteral("net/fetchLocalhostWithoutPrompt")
        || method == QStringLiteral("net/fetchPrivateNetworkWithoutPrompt")) {
        const QUrl url(params.value(QStringLiteral("url")).toString());
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            return errorObject(QStringLiteral("Only http/https URLs are supported."));
        }
        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = manager.get(request);
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(qBound(1000, params.value(QStringLiteral("timeoutMs")).toInt(15000), 60000));
        loop.exec();
        if (!timer.isActive()) {
            reply->abort();
            reply->deleteLater();
            return errorObject(QStringLiteral("Network request timed out."));
        }
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        if (!error.isEmpty()) {
            return errorObject(error);
        }
        if (method == QStringLiteral("net/download")) {
            const QString targetPath = params.value(QStringLiteral("targetPath")).toString();
            QFile file(targetPath);
            if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return errorObject(QStringLiteral("Cannot write download target: %1").arg(file.errorString()));
            }
            file.write(body);
            return okValue(QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("path"), targetPath}});
        }
        return okValue(QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("text"), QString::fromUtf8(body)}});
    }
    if (method == QStringLiteral("net/getProxySettings")) {
        const QNetworkProxy proxy = QNetworkProxy::applicationProxy();
        return okValue(QJsonObject{
            {QStringLiteral("type"), static_cast<int>(proxy.type())},
            {QStringLiteral("host"), proxy.hostName()},
            {QStringLiteral("port"), proxy.port()},
            {QStringLiteral("user"), proxy.user()},
        });
    }
    if (method == QStringLiteral("net/setProxySettings")) {
        QNetworkProxy proxy;
        proxy.setType(static_cast<QNetworkProxy::ProxyType>(params.value(QStringLiteral("type")).toInt(QNetworkProxy::NoProxy)));
        proxy.setHostName(params.value(QStringLiteral("host")).toString());
        proxy.setPort(static_cast<quint16>(params.value(QStringLiteral("port")).toInt()));
        proxy.setUser(params.value(QStringLiteral("user")).toString());
        proxy.setPassword(params.value(QStringLiteral("password")).toString());
        QNetworkProxy::setApplicationProxy(proxy);
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("settings/get")) {
        const QJsonObject root = UiText::loadPreferencesObject();
        return okValue(root.value(params.value(QStringLiteral("key")).toString()));
    }
    if (method == QStringLiteral("settings/set")) {
        QJsonObject root = UiText::loadPreferencesObject();
        root.insert(params.value(QStringLiteral("key")).toString(), params.value(QStringLiteral("value")));
        return QJsonObject{{QStringLiteral("ok"), UiText::savePreferencesObject(root)}};
    }
    if (method == QStringLiteral("settings/listKeys")) {
        const QJsonObject root = UiText::loadPreferencesObject();
        QJsonArray keys;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            keys.append(it.key());
        }
        return okValue(keys);
    }
    if (method == QStringLiteral("settings/export")) {
        return okValue(UiText::loadPreferencesObject());
    }
    if (method == QStringLiteral("settings/reset")) {
        QJsonObject root = UiText::loadPreferencesObject();
        root.remove(params.value(QStringLiteral("key")).toString());
        return QJsonObject{{QStringLiteral("ok"), UiText::savePreferencesObject(root)}};
    }
    if (method == QStringLiteral("settings/import")) {
        const QJsonObject object = params.value(QStringLiteral("settings")).toObject(params.value(QStringLiteral("value")).toObject());
        if (object.isEmpty()) {
            return errorObject(QStringLiteral("settings.import requires a settings object."));
        }
        return QJsonObject{{QStringLiteral("ok"), UiText::savePreferencesObject(object)}};
    }
    if (method == QStringLiteral("settings/setSecuritySensitive")) {
        return errorObject(QStringLiteral("Security-sensitive settings cannot be changed through this API."));
    }
    if (method == QStringLiteral("theme/getCurrent")) {
        const QJsonObject root = UiText::loadPreferencesObject();
        return okValue(UiText::themeTokenFromPreferencesObject(root));
    }
    if (method == QStringLiteral("theme/listAvailable")) {
        return okValue(QJsonArray{QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")});
    }
    if (method == QStringLiteral("theme/getColor")) {
        const QString name = params.value(QStringLiteral("name")).toString(params.value(QStringLiteral("role")).toString());
        const QJsonObject colors{
            {QStringLiteral("window"), QStringLiteral("#202124")},
            {QStringLiteral("text"), QStringLiteral("#f1f3f4")},
            {QStringLiteral("accent"), QStringLiteral("#5b9dff")},
            {QStringLiteral("warning"), QStringLiteral("#fbbc04")},
            {QStringLiteral("error"), QStringLiteral("#ff6b6b")},
            {QStringLiteral("success"), QStringLiteral("#57c785")},
        };
        if (name.trimmed().isEmpty()) {
            return okValue(colors);
        }
        return okValue(colors.value(name));
    }
    if (method == QStringLiteral("theme/setCurrent")) {
        const QString theme = params.value(QStringLiteral("theme")).toString(params.value(QStringLiteral("value")).toString()).trimmed();
        if (theme.isEmpty()) {
            return errorObject(QStringLiteral("theme.setCurrent requires a theme value."));
        }
        QJsonObject root = UiText::loadPreferencesObject();
        UiText::setThemeTokenInPreferencesObject(&root, theme);
        return QJsonObject{{QStringLiteral("ok"), UiText::savePreferencesObject(root)}};
    }
    if (method == QStringLiteral("backup/list")) {
        const QDir backupDir(QDir(extensionLogDirectory()).filePath(QStringLiteral("backups")));
        QJsonArray items;
        if (backupDir.exists()) {
            for (const QFileInfo& info : backupDir.entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Time)) {
                items.append(QJsonObject{
                    {QStringLiteral("id"), info.completeBaseName()},
                    {QStringLiteral("path"), info.absoluteFilePath()},
                    {QStringLiteral("size"), static_cast<double>(info.size())},
                    {QStringLiteral("lastModified"), info.lastModified().toString(Qt::ISODate)},
                });
            }
        }
        return okValue(items);
    }
    if (method == QStringLiteral("backup/create")) {
        const QString requestedId = params.value(QStringLiteral("id")).toString().trimmed();
        const QString id = requestedId.isEmpty()
            ? QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzz"))
            : requestedId;
        QDir backupDir(QDir(extensionLogDirectory()).filePath(QStringLiteral("backups")));
        if (!backupDir.exists() && !backupDir.mkpath(QStringLiteral("."))) {
            return errorObject(QStringLiteral("Cannot create backup directory: %1").arg(backupDir.absolutePath()));
        }
        const QString path = backupDir.filePath(id + QStringLiteral(".json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return errorObject(QStringLiteral("Cannot write backup: %1").arg(file.errorString()));
        }
        const QJsonObject snapshot{
            {QStringLiteral("id"), id},
            {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {QStringLiteral("extensionId"), extensionId},
            {QStringLiteral("data"), params.value(QStringLiteral("data")).toObject(params.value(QStringLiteral("value")).toObject())},
            {QStringLiteral("settings"), params.value(QStringLiteral("includeSettings")).toBool(false) ? UiText::loadPreferencesObject() : QJsonObject{}},
        };
        file.write(QJsonDocument(snapshot).toJson(QJsonDocument::Indented));
        return okValue(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("path"), path}});
    }
    if (method == QStringLiteral("backup/read")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            return errorObject(QStringLiteral("backup.read requires an id."));
        }
        QFile file(QDir(QDir(extensionLogDirectory()).filePath(QStringLiteral("backups"))).filePath(id + QStringLiteral(".json")));
        if (!file.open(QIODevice::ReadOnly)) {
            return errorObject(QStringLiteral("Cannot read backup: %1").arg(file.errorString()));
        }
        return okValue(QJsonDocument::fromJson(file.readAll()).object());
    }
    if (method == QStringLiteral("backup/remove")) {
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            return errorObject(QStringLiteral("backup.remove requires an id."));
        }
        const QString path = QDir(QDir(extensionLogDirectory()).filePath(QStringLiteral("backups"))).filePath(id + QStringLiteral(".json"));
        return QJsonObject{{QStringLiteral("ok"), QFile::remove(path)}};
    }
    if (method.startsWith(QStringLiteral("providers/"))) {
        if (callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(method, params);
            if (!response.isEmpty()) {
                return response;
            }
        }
        return errorObject(QStringLiteral("Provider host is not available."));
    }
    if (method == QStringLiteral("shortcuts/getEditable") || method == QStringLiteral("shortcuts/reloadRegistered")) {
        if (callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(method, params);
            if (!response.isEmpty()) {
                return response;
            }
        }
        return errorObject(QStringLiteral("Shortcut host is not available."));
    }
    if (method == QStringLiteral("shortcuts/list")) {
        if (callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(method, params);
            if (!response.isEmpty()) {
                return response;
            }
        }
        return okValue(UiText::loadPreferencesObject().value(QStringLiteral("extensionShortcuts")).toObject());
    }
    if (method == QStringLiteral("shortcuts/getKeybinding")) {
        if (callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(method, params);
            if (!response.isEmpty()) {
                return response;
            }
        }
        const QString command = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString());
        return okValue(UiText::loadPreferencesObject()
                           .value(QStringLiteral("extensionShortcuts")).toObject()
                           .value(command));
    }
    if (method == QStringLiteral("shortcuts/register")) {
        if (callbacks_.mainWindowRequest) {
            const QJsonObject response = callbacks_.mainWindowRequest(method, params);
            if (!response.isEmpty()) {
                return response;
            }
        }
        const QString command = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString()).trimmed();
        if (command.isEmpty()) {
            return errorObject(QStringLiteral("shortcuts.register requires a command."));
        }
        QJsonObject root = UiText::loadPreferencesObject();
        QJsonObject shortcuts = root.value(QStringLiteral("extensionShortcuts")).toObject();
        shortcuts.insert(command, params.value(QStringLiteral("keybinding")).toString(params.value(QStringLiteral("keys")).toString()));
        root.insert(QStringLiteral("extensionShortcuts"), shortcuts);
        return QJsonObject{{QStringLiteral("ok"), UiText::savePreferencesObject(root)}};
    }
    if (method == QStringLiteral("extensions/all")) {
        QJsonArray array;
        for (const ExtensionRecord& record : records_) {
            array.append(QJsonObject{
                {QStringLiteral("id"), record.valid ? record.manifest.qualifiedId() : record.sourcePath},
                {QStringLiteral("name"), record.manifest.name},
                {QStringLiteral("version"), record.manifest.version},
                {QStringLiteral("enabled"), record.enabled},
                {QStringLiteral("valid"), record.valid},
                {QStringLiteral("permissions"), QJsonArray::fromStringList(record.manifest.permissions)},
                {QStringLiteral("diagnostic"), record.diagnostic},
            });
        }
        return okValue(array);
    }
    if (method == QStringLiteral("extensions/get")) {
        const QString id = params.value(QStringLiteral("id")).toString();
        for (const ExtensionRecord& record : records_) {
            if (record.valid && record.manifest.qualifiedId() == id) {
                return okValue(QJsonObject{
                    {QStringLiteral("id"), record.manifest.qualifiedId()},
                    {QStringLiteral("name"), record.manifest.name},
                    {QStringLiteral("version"), record.manifest.version},
                    {QStringLiteral("enabled"), record.enabled},
                    {QStringLiteral("permissions"), QJsonArray::fromStringList(record.manifest.permissions)},
                });
            }
        }
        return errorObject(QStringLiteral("Extension not found: %1").arg(id));
    }
    if (method == QStringLiteral("extensions/enable") || method == QStringLiteral("extensions/disable")) {
        setExtensionEnabled(params.value(QStringLiteral("id")).toString(), method == QStringLiteral("extensions/enable"));
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("extensions/installFromFolder")) {
        const QString sourcePath = params.value(QStringLiteral("path")).toString();
        const ExtensionManifestParseResult parsed = loadExtensionManifest(sourcePath);
        if (!parsed.ok) {
            return errorObject(QStringLiteral("Cannot install extension: %1").arg(parsed.error));
        }
        const QString extensionsRoot = userExtensionsDirectory();
        QDir().mkpath(extensionsRoot);
        const QString targetPath = QDir(extensionsRoot).filePath(parsed.manifest.qualifiedId());
        if (QFileInfo::exists(targetPath)) {
            return errorObject(QStringLiteral("Extension already exists: %1").arg(parsed.manifest.qualifiedId()));
        }
        QString error;
        if (!copyDirectoryRecursively(sourcePath, targetPath, &error)) {
            return errorObject(error);
        }
        refreshExtensions();
        return okValue(QJsonObject{{QStringLiteral("id"), parsed.manifest.qualifiedId()}, {QStringLiteral("path"), targetPath}});
    }
    if (method == QStringLiteral("extensions/getCurrent")) {
        return okValue(extensionId);
    }
    if (method == QStringLiteral("extensions/getDiagnostics")) {
        return okValue(QJsonArray::fromStringList(diagnostics_));
    }
    if (method == QStringLiteral("extensions/openFolder")) {
        return QJsonObject{{QStringLiteral("ok"), QDesktopServices::openUrl(QUrl::fromLocalFile(userExtensionsDirectory()))}};
    }
    if (method == QStringLiteral("extensions/validateManifest")) {
        const ExtensionManifestParseResult parsed = loadExtensionManifest(params.value(QStringLiteral("path")).toString());
        return parsed.ok
            ? okValue(QJsonObject{{QStringLiteral("id"), parsed.manifest.qualifiedId()}})
            : errorObject(parsed.error);
    }
    if (method == QStringLiteral("extensions/remove")) {
        const QString id = params.value(QStringLiteral("id")).toString();
        QString targetPath;
        for (const ExtensionRecord& record : records_) {
            if (record.valid && record.manifest.qualifiedId() == id) {
                targetPath = record.manifest.rootPath;
                break;
            }
        }
        if (targetPath.isEmpty()) {
            return errorObject(QStringLiteral("Extension not found: %1").arg(id));
        }
        const QString extensionsRoot = userExtensionsDirectory();
        if (!isPathInside(targetPath, extensionsRoot)) {
            return errorObject(QStringLiteral("Refusing to remove extension outside the user extensions directory: %1").arg(targetPath));
        }
        QDir dir(targetPath);
        if (!dir.removeRecursively()) {
            return errorObject(QStringLiteral("Cannot remove extension directory: %1").arg(targetPath));
        }
        refreshExtensions();
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("shell/execute")) {
        const QString command = params.value(QStringLiteral("command")).toString(
            params.value(QStringLiteral("cmd")).toString(params.value(QStringLiteral("script")).toString()));
        if (command.trimmed().isEmpty()) {
            return errorObject(QStringLiteral("shell.execute denied: missing command."));
        }
#if defined(Q_OS_WIN)
        const QString shell = qEnvironmentVariable("COMSPEC").trimmed().isEmpty()
            ? QStringLiteral("cmd.exe")
            : qEnvironmentVariable("COMSPEC");
        const QStringList arguments{QStringLiteral("/C"), command};
#else
        const QString shell = QStringLiteral("/bin/sh");
        const QStringList arguments{QStringLiteral("-c"), command};
#endif
        return startDetachedProcess(QStringLiteral("shell.execute"),
                                    shell,
                                    arguments,
                                    params.value(QStringLiteral("cwd")).toString(params.value(QStringLiteral("workingDirectory")).toString()),
                                    processEnvironmentFromJson(params),
                                    params,
                                    QStringLiteral("executed"));
    }
    if (method == QStringLiteral("process/spawn")) {
        const QString program = params.value(QStringLiteral("program")).toString(
            params.value(QStringLiteral("executable")).toString(params.value(QStringLiteral("file")).toString()));
        return startDetachedProcess(QStringLiteral("process.spawn"),
                                    program,
                                    jsonStringList(params.value(QStringLiteral("args"))),
                                    params.value(QStringLiteral("cwd")).toString(params.value(QStringLiteral("workingDirectory")).toString()),
                                    processEnvironmentFromJson(params),
                                    params,
                                    QStringLiteral("spawned"));
    }
    if (method == QStringLiteral("native/raw")
        || method == QStringLiteral("internal/raw")
        || method == QStringLiteral("renderer/raw")
        || method == QStringLiteral("export/raw")
        || method == QStringLiteral("security/raw")
        || method == QStringLiteral("updates/raw")) {
        const QString target = method;
        return experimentalRawAccepted(target.left(target.indexOf(QLatin1Char('/'))) + QStringLiteral(".raw"),
                                       method,
                                       params.value(QStringLiteral("member")).toString(params.value(QStringLiteral("op")).toString()),
                                       params);
    }
    if (method == QStringLiteral("process/openFileWithSystem")) {
        const QString path = params.value(QStringLiteral("path")).toString();
        return QJsonObject{{QStringLiteral("ok"), QDesktopServices::openUrl(QUrl::fromLocalFile(path))}};
    }
    if (method == QStringLiteral("process/revealInFileExplorer")) {
        const QString path = params.value(QStringLiteral("path")).toString();
        const QString absolutePath = QFileInfo(path).exists() ? QFileInfo(path).absoluteFilePath() : path;
#if defined(Q_OS_WIN)
        const bool ok = QProcess::startDetached(QStringLiteral("explorer.exe"), QStringList{QStringLiteral("/select,") + QDir::toNativeSeparators(absolutePath)});
#else
        const bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(absolutePath).absolutePath()));
#endif
        return QJsonObject{{QStringLiteral("ok"), ok}};
    }
    if (method == QStringLiteral("process/openUrlInBrowser")) {
        const QUrl url(params.value(QStringLiteral("url")).toString());
        return QJsonObject{{QStringLiteral("ok"), url.isValid() && QDesktopServices::openUrl(url)}};
    }
    if (callbacks_.mainWindowRequest) {
        const QJsonObject response = callbacks_.mainWindowRequest(method, params);
        if (!response.isEmpty()) {
            return response;
        }
    }
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unknown host method: %1").arg(method)}};
}

void ExtensionManager::invokeCommand(const QString& command)
{
    const ExtensionCommandContribution contribution = commandContribution(command);
    const QString commandLabel = contribution.title.isEmpty() ? command : displayLabelForCommand(contribution);
    if (!commandOwnerById_.contains(command)) {
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("extension command unavailable"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("label"), commandLabel},
        });
        if (callbacks_.showMessage) {
            callbacks_.showMessage(
                QStringLiteral("warning"),
                localizedText(
                    QStringLiteral("extension.command.unavailable"),
                    QStringLiteral("Extension command is unavailable: %1\n\nPossible reasons: the extension is disabled, its manifest is invalid, or this command no longer exists after refresh."))
                    .arg(commandLabel));
        }
        return;
    }
    if (runtime_ == nullptr || !runtime_->isRunning()) {
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("extension command runtime not running"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("label"), commandLabel},
            {QStringLiteral("ownerId"), commandOwnerById_.value(command)},
        });
        if (callbacks_.showMessage) {
            callbacks_.showMessage(
                QStringLiteral("warning"),
                localizedText(
                    QStringLiteral("extension.command.runtime_not_running"),
                    QStringLiteral("Extension runtime is not running, so MiaCode cannot run: %1\n\nPossible reasons: this extension has no loadable JS entry, activation failed, or extensions need to be refreshed."))
                    .arg(commandLabel));
        }
        return;
    }
    QString error;
    if (!runtime_->executeCommand(command, &error)) {
        appendExtensionLog(QStringLiteral("error"), QStringLiteral("extension command failed"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("label"), commandLabel},
            {QStringLiteral("ownerId"), commandOwnerById_.value(command)},
            {QStringLiteral("error"), error},
        });
        if (callbacks_.showMessage) {
            callbacks_.showMessage(
                QStringLiteral("warning"),
                localizedText(
                    QStringLiteral("extension.command.failed"),
                    QStringLiteral("Extension command did not respond normally: %1\n\nReason: %2"))
                    .arg(commandLabel, error));
        }
        return;
    }
    if (callbacks_.mainWindowRequest) {
        callbacks_.mainWindowRequest(QStringLiteral("window/createStatusBarItem"), QJsonObject{
            {QStringLiteral("text"),
             localizedText(
                 QStringLiteral("extension.command.ran"),
                 QStringLiteral("Extension command ran: %1"))
                 .arg(commandLabel)},
            {QStringLiteral("timeoutMs"), 3000},
        });
    }
}

bool ExtensionManager::executeExtensionCommand(const QString& command, QString* error)
{
    if (!commandOwnerById_.contains(command)) {
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("extension command unavailable"), QJsonObject{
            {QStringLiteral("command"), command},
        });
        if (error != nullptr) {
            *error = QStringLiteral("Unknown or unavailable command: %1").arg(command);
        }
        return false;
    }
    if (runtime_ == nullptr || !runtime_->isRunning()) {
        appendExtensionLog(QStringLiteral("warning"), QStringLiteral("extension command runtime not running"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("ownerId"), commandOwnerById_.value(command)},
        });
        if (error != nullptr) {
            *error = QStringLiteral("Extension runtime is not running.");
        }
        return false;
    }
    QString localError;
    if (!runtime_->executeCommand(command, &localError)) {
        appendExtensionLog(QStringLiteral("error"), QStringLiteral("extension command failed"), QJsonObject{
            {QStringLiteral("command"), command},
            {QStringLiteral("ownerId"), commandOwnerById_.value(command)},
            {QStringLiteral("error"), localError},
        });
        if (error != nullptr) {
            *error = localError;
        }
        return false;
    }
    if (callbacks_.mainWindowRequest) {
        callbacks_.mainWindowRequest(QStringLiteral("window/createStatusBarItem"), QJsonObject{
            {QStringLiteral("text"), localizedText(
                 QStringLiteral("extension.command.ran"),
                 QStringLiteral("Extension command ran: %1"))
                 .arg(command)},
            {QStringLiteral("timeoutMs"), 3000},
        });
    }
    return true;
}

ExtensionCommandContribution ExtensionManager::commandContribution(const QString& command) const
{
    for (const ExtensionManifest& manifest : manifests_) {
        for (const ExtensionCommandContribution& candidate : manifest.commands) {
            if (candidate.id == command) {
                return candidate;
            }
        }
    }
    return ExtensionCommandContribution();
}

}  // namespace miacode::extensions
