#include "ExtensionManager.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include "UiText.h"

namespace miacode::extensions {
namespace {

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
    discoverExtensions();
    rebuildMenuContributions();
    UiText::reloadExtensionLanguagePacks();
    UiText::ensurePreferredLanguageAvailable();
    restartRuntime();
    rebuildFilesystemWatchers();
}

void ExtensionManager::restartRuntime()
{
    shutdown();
    const QJsonArray runtimeManifests = manifestsForRuntime();
    if (runtimeManifests.isEmpty()) {
        return;
    }
    runtime_ = std::make_unique<EmbeddedExtensionRuntime>(this);
    runtime_->setHostRequestHandler([this](const QString& method, const QJsonObject& params) {
        return handleHostRequest(method, params);
    });
    connect(runtime_.get(), &EmbeddedExtensionRuntime::runtimeLogMessage, this, [this](const QString& message) {
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });
    connect(runtime_.get(), &EmbeddedExtensionRuntime::runtimeErrorMessage, this, [this](const QString& message) {
        diagnostics_.append(message);
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });

    QString error;
    if (!runtime_->start(runtimeManifests, &error)) {
        diagnostics_.append(error);
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(error));
        }
    }
}

void ExtensionManager::shutdown()
{
    if (runtime_) {
        runtime_->stop();
        runtime_.reset();
    }
}

QJsonObject permissionGrantsObject()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    return root.value(QStringLiteral("extensions")).toObject()
        .value(QStringLiteral("permissions")).toObject();
}

QStringList permissionLabels(const QStringList& permissions)
{
    QStringList labels;
    for (const QString& permission : permissions) {
        labels.append(permission);
    }
    labels.removeDuplicates();
    labels.sort(Qt::CaseInsensitive);
    return labels;
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

bool isPrivateOrLocalNetworkTarget(const QUrl& url)
{
    const QString host = url.host().trimmed().toLower();
    if (host.isEmpty() || host == QStringLiteral("localhost") || host.endsWith(QStringLiteral(".local"))) {
        return true;
    }
    QHostAddress address(host);
    if (address.isNull()) {
        return false;
    }
    return address.isLoopback()
        || address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
        || address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12)
        || address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)
        || address.isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16)
        || address.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7)
        || address.isInSubnet(QHostAddress(QStringLiteral("fe80::")), 10);
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
    static const QSet<QString> blocked{
        QStringLiteral("security/disablePermissionChecks"),
        QStringLiteral("security/grantAllPermissions"),
        QStringLiteral("security/modifyTrustedExtensions"),
        QStringLiteral("security/installUnsignedSilently"),
        QStringLiteral("updates/replaceExecutable"),
        QStringLiteral("updates/patchApplicationFiles"),
        QStringLiteral("updates/autoInstallWithoutPrompt"),
    };
    return blocked.contains(method);
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
    // Keep this registry broad: it is both the callable API index and the product
    // contract extensions use to discover/request MiaCode capabilities.
    static const QVector<QJsonObject> registry{
        apiDescriptor(QStringLiteral("app.getVersion"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get MiaCode version from app info.")),
        apiDescriptor(QStringLiteral("app.getBuildInfo"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get build information from app info.")),
        apiDescriptor(QStringLiteral("app.getCommitHash"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get source commit hash.")),
        apiDescriptor(QStringLiteral("app.getPlatform"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get platform information.")),
        apiDescriptor(QStringLiteral("app.getArchitecture"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get CPU architecture.")),
        apiDescriptor(QStringLiteral("app.getInstallRoot"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get install root.")),
        apiDescriptor(QStringLiteral("app.getExecutablePath"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get executable path.")),
        apiDescriptor(QStringLiteral("app.getExtensionsRoot"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get extensions root.")),
        apiDescriptor(QStringLiteral("app.getLogsRoot"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get logs root.")),
        apiDescriptor(QStringLiteral("app.getConfigRoot"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get config root.")),
        apiDescriptor(QStringLiteral("app.getTempRoot"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get temp root.")),
        apiDescriptor(QStringLiteral("app.getLocale"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current UI locale.")),
        apiDescriptor(QStringLiteral("app.getTheme"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get current theme.")),
        apiDescriptor(QStringLiteral("app.getDpiScale"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get DPI scale.")),
        apiDescriptor(QStringLiteral("app.isPortableMode"), QStringLiteral("app/getInfo"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Check portable mode.")),
        apiDescriptor(QStringLiteral("app.openPreferences"), QStringLiteral("app/openPreferences"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open Preferences.")),
        apiDescriptor(QStringLiteral("app.openAboutDialog"), QStringLiteral("app/openAboutDialog"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open About dialog.")),
        apiDescriptor(QStringLiteral("app.reloadExtensions"), QStringLiteral("app/reloadExtensions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Reload extensions.")),
        apiDescriptor(QStringLiteral("app.restartRequired"), QStringLiteral("app/restartRequired"), QStringLiteral("app.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Check whether restart is required.")),
        apiDescriptor(QStringLiteral("app.requestRestart"), QStringLiteral("app/requestRestart"), QStringLiteral("app.lifecycle"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Request app restart.")),
        apiDescriptor(QStringLiteral("app.quit"), QStringLiteral("app/quit"), QStringLiteral("app.lifecycle"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Request app quit.")),

        apiDescriptor(QStringLiteral("capabilities.list"), QStringLiteral("api/list"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List all registered API capabilities.")),
        apiDescriptor(QStringLiteral("capabilities.has"), QStringLiteral("api/has"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Check whether an API exists.")),
        apiDescriptor(QStringLiteral("capabilities.describe"), QStringLiteral("api/describe"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Describe an API.")),
        apiDescriptor(QStringLiteral("capabilities.describeNamespace"), QStringLiteral("api/describeNamespace"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Describe a namespace.")),
        apiDescriptor(QStringLiteral("capabilities.getPermissions"), QStringLiteral("api/describe"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read permission metadata for an API.")),
        apiDescriptor(QStringLiteral("capabilities.getRiskLevel"), QStringLiteral("api/describe"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read risk metadata for an API.")),
        apiDescriptor(QStringLiteral("capabilities.request"), QStringLiteral("api/request"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Submit a structured API request.")),
        apiDescriptor(QStringLiteral("capabilities.invokePublicMethod"), QStringLiteral("api/invoke"), QString(), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Invoke a public host bridge method by method id.")),
        apiDescriptor(QStringLiteral("capabilities.getMissing"), QStringLiteral("api/getMissing"), QString(), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("List missing APIs requested by current extension.")),
        apiDescriptor(QStringLiteral("capabilities.exportRequestReport"), QStringLiteral("api/exportRequestReport"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export API request report.")),

        apiDescriptor(QStringLiteral("extensions.list"), QStringLiteral("extensions/all"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("List extensions.")),
        apiDescriptor(QStringLiteral("extensions.get"), QStringLiteral("extensions/get"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Get extension information.")),
        apiDescriptor(QStringLiteral("extensions.getCurrent"), QStringLiteral("extensions/getCurrent"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Get current extension information.")),
        apiDescriptor(QStringLiteral("extensions.getDiagnostics"), QStringLiteral("extensions/getDiagnostics"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Get extension diagnostics.")),
        apiDescriptor(QStringLiteral("extensions.getPermissions"), QStringLiteral("extensions/get"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Get declared permissions.")),
        apiDescriptor(QStringLiteral("extensions.getGrantedPermissions"), QStringLiteral("extensions/getGrantedPermissions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Get granted permissions.")),
        apiDescriptor(QStringLiteral("extensions.openFolder"), QStringLiteral("extensions/openFolder"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Open extensions folder.")),
        apiDescriptor(QStringLiteral("extensions.openLogs"), QStringLiteral("logs/open"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Open extension logs.")),
        apiDescriptor(QStringLiteral("extensions.reload"), QStringLiteral("app/reloadExtensions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Reload extensions.")),
        apiDescriptor(QStringLiteral("extensions.enable"), QStringLiteral("extensions/enable"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Enable extension.")),
        apiDescriptor(QStringLiteral("extensions.disable"), QStringLiteral("extensions/disable"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Disable extension.")),
        apiDescriptor(QStringLiteral("extensions.revokePermissions"), QStringLiteral("extensions/revokePermissions"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Revoke extension permission grants.")),
        apiDescriptor(QStringLiteral("extensions.installFromFolder"), QStringLiteral("extensions/installFromFolder"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Install extension from folder.")),
        apiDescriptor(QStringLiteral("extensions.installFromZip"), QStringLiteral("extensions/installFromZip"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Install extension from zip.")),
        apiDescriptor(QStringLiteral("extensions.remove"), QStringLiteral("extensions/remove"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Remove extension.")),
        apiDescriptor(QStringLiteral("extensions.update"), QStringLiteral("extensions/update"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Update extension.")),
        apiDescriptor(QStringLiteral("extensions.validateManifest"), QStringLiteral("extensions/validateManifest"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Validate manifest.")),
        apiDescriptor(QStringLiteral("extensions.pack"), QStringLiteral("extensions/pack"), QStringLiteral("extensions.manage"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Pack extension.")),

        apiDescriptor(QStringLiteral("commands.list"), QStringLiteral("commands/getCommands"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List commands.")),
        apiDescriptor(QStringLiteral("commands.describe"), QStringLiteral("commands/describe"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Describe command.")),
        apiDescriptor(QStringLiteral("commands.register"), QStringLiteral("commands/register"), QString(), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Register command callback.")),
        apiDescriptor(QStringLiteral("commands.unregister"), QStringLiteral("commands/unregister"), QString(), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Unregister command.")),
        apiDescriptor(QStringLiteral("commands.execute"), QStringLiteral("commands/execute"), QStringLiteral("commands.execute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Execute command.")),
        apiDescriptor(QStringLiteral("commands.executeInternal"), QStringLiteral("commands/executeInternal"), QStringLiteral("commands.execute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Execute internal command.")),
        apiDescriptor(QStringLiteral("commands.getHistory"), QStringLiteral("commands/getHistory"), QStringLiteral("commands.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get command history.")),

        apiDescriptor(QStringLiteral("menus.listContributionPoints"), QStringLiteral("menus/listContributionPoints"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("List menu contribution points.")),
        apiDescriptor(QStringLiteral("menus.getItems"), QStringLiteral("menus/getItems"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Get menu items.")),
        apiDescriptor(QStringLiteral("menus.refresh"), QStringLiteral("menus/refresh"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Refresh menus.")),
        apiDescriptor(QStringLiteral("menus.registerItem"), QStringLiteral("menus/registerItem"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Register menu item.")),
        apiDescriptor(QStringLiteral("menus.unregisterItem"), QStringLiteral("menus/unregisterItem"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Unregister menu item.")),
        apiDescriptor(QStringLiteral("menus.setItemEnabled"), QStringLiteral("menus/setItemEnabled"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set menu item enabled.")),
        apiDescriptor(QStringLiteral("menus.setItemVisible"), QStringLiteral("menus/setItemVisible"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set menu item visible.")),

        apiDescriptor(QStringLiteral("ui.registerSidebarView"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register sidebar view contribution metadata; visible renderer host is planned.")),
        apiDescriptor(QStringLiteral("ui.registerBottomTabView"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register bottom-tab contribution metadata; visible renderer host is planned.")),
        apiDescriptor(QStringLiteral("ui.registerPreferencesPage"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register preferences-page contribution metadata; visible renderer host is planned.")),
        apiDescriptor(QStringLiteral("ui.registerToolbarButton"), QStringLiteral("contributions/register"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register toolbar-button contribution metadata; visible renderer host is planned.")),
        apiDescriptor(QStringLiteral("ui.getContributions"), QStringLiteral("ui/getContributions"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Get registered UI contribution metadata.")),
        apiDescriptor(QStringLiteral("ui.getViews"), QStringLiteral("ui/getViews"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("List rendered extension views.")),
        apiDescriptor(QStringLiteral("ui.unregisterView"), QStringLiteral("ui/unregisterView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Unregister extension view.")),
        apiDescriptor(QStringLiteral("ui.refreshViews"), QStringLiteral("ui/refreshViews"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Refresh extension view hosts.")),
        apiDescriptor(QStringLiteral("ui.renderDeclarativeView"), QStringLiteral("ui/renderDeclarativeView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render a declarative extension view schema.")),
        apiDescriptor(QStringLiteral("ui.renderSidebarView"), QStringLiteral("ui/renderSidebarView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render sidebar extension view.")),
        apiDescriptor(QStringLiteral("ui.renderBottomTabView"), QStringLiteral("ui/renderBottomTabView"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render bottom-tab extension view.")),
        apiDescriptor(QStringLiteral("ui.renderPreferencesPage"), QStringLiteral("ui/renderPreferencesPage"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render preferences extension page.")),
        apiDescriptor(QStringLiteral("ui.renderToolbarButton"), QStringLiteral("ui/renderToolbarButton"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render toolbar extension button.")),

        apiDescriptor(QStringLiteral("window.showInformationMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show information message.")),
        apiDescriptor(QStringLiteral("window.showWarningMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show warning message.")),
        apiDescriptor(QStringLiteral("window.showErrorMessage"), QStringLiteral("window/showMessage"), QStringLiteral("ui.message"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Show error message.")),
        apiDescriptor(QStringLiteral("window.showInputBox"), QStringLiteral("window/showInputBox"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show input box.")),
        apiDescriptor(QStringLiteral("window.showQuickPick"), QStringLiteral("window/showQuickPick"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show quick pick.")),
        apiDescriptor(QStringLiteral("window.showOpenDialog"), QStringLiteral("window/showOpenDialog"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show open dialog.")),
        apiDescriptor(QStringLiteral("window.showSaveDialog"), QStringLiteral("window/showSaveDialog"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show save dialog.")),
        apiDescriptor(QStringLiteral("window.showSelectFolderDialog"), QStringLiteral("window/showSelectFolderDialog"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show select folder dialog.")),
        apiDescriptor(QStringLiteral("window.createStatusBarItem"), QStringLiteral("window/createStatusBarItem"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Create status bar item.")),
        apiDescriptor(QStringLiteral("window.setStatusBarMessage"), QStringLiteral("window/createStatusBarItem"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Set status bar message.")),
        apiDescriptor(QStringLiteral("window.clearStatusBarMessage"), QStringLiteral("window/clearStatusBarMessage"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Clear status bar message.")),
        apiDescriptor(QStringLiteral("window.setProgress"), QStringLiteral("window/setProgress"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Set progress.")),
        apiDescriptor(QStringLiteral("window.clearProgress"), QStringLiteral("window/clearProgress"), QStringLiteral("ui.status"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Clear progress.")),
        apiDescriptor(QStringLiteral("window.openExternalUrl"), QStringLiteral("window/openExternalUrl"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open external URL.")),
        apiDescriptor(QStringLiteral("window.focusEditor"), QStringLiteral("window/focusEditor"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Focus editor.")),
        apiDescriptor(QStringLiteral("window.focusPreview"), QStringLiteral("window/focusPreview"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Focus preview.")),
        apiDescriptor(QStringLiteral("window.focusTimeline"), QStringLiteral("window/focusTimeline"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Focus timeline.")),
        apiDescriptor(QStringLiteral("window.focusValidationPanel"), QStringLiteral("window/focusValidationPanel"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Focus validation panel.")),
        apiDescriptor(QStringLiteral("window.focusMetadataPanel"), QStringLiteral("window/focusMetadataPanel"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Focus metadata panel.")),

        apiDescriptor(QStringLiteral("workspace.getActiveDocument"), QStringLiteral("workspace/getActiveDocument"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get active document snapshot.")),
        apiDescriptor(QStringLiteral("workspace.getCurrentFilePath"), QStringLiteral("workspace/getCurrentFilePath"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current file path.")),
        apiDescriptor(QStringLiteral("workspace.getChartFolder"), QStringLiteral("workspace/getChartFolder"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get chart folder.")),
        apiDescriptor(QStringLiteral("workspace.getChartMetadata"), QStringLiteral("workspace/getChartMetadata"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get chart metadata.")),
        apiDescriptor(QStringLiteral("workspace.updateChartMetadata"), QStringLiteral("workspace/updateChartMetadata"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Update chart metadata.")),
        apiDescriptor(QStringLiteral("workspace.getMediaFiles"), QStringLiteral("workspace/getMediaFiles"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get media files.")),
        apiDescriptor(QStringLiteral("workspace.getRecentFiles"), QStringLiteral("workspace/getRecentFiles"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get recent files.")),
        apiDescriptor(QStringLiteral("workspace.getDirtyState"), QStringLiteral("workspace/getDirtyState"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get dirty state.")),
        apiDescriptor(QStringLiteral("workspace.isDirty"), QStringLiteral("workspace/isDirty"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Check dirty state.")),
        apiDescriptor(QStringLiteral("workspace.save"), QStringLiteral("workspace/save"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Save current file.")),
        apiDescriptor(QStringLiteral("workspace.saveAs"), QStringLiteral("workspace/saveAs"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Save as path.")),
        apiDescriptor(QStringLiteral("workspace.reloadCurrentFile"), QStringLiteral("workspace/reloadCurrentFile"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Reload current file.")),
        apiDescriptor(QStringLiteral("workspace.closeCurrentFile"), QStringLiteral("workspace/closeCurrentFile"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Close current file.")),
        apiDescriptor(QStringLiteral("workspace.openFile"), QStringLiteral("workspace/openFile"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Open file.")),
        apiDescriptor(QStringLiteral("workspace.openFolder"), QStringLiteral("workspace/openFolder"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Open folder.")),
        apiDescriptor(QStringLiteral("workspace.createNewChart"), QStringLiteral("workspace/createNewChart"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Create new chart.")),
        apiDescriptor(QStringLiteral("workspace.importChartFolder"), QStringLiteral("workspace/importChartFolder"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Import chart folder.")),
        apiDescriptor(QStringLiteral("workspace.exportChartFolder"), QStringLiteral("workspace/exportChartFolder"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export chart folder.")),
        apiDescriptor(QStringLiteral("workspace.packProjectZip"), QStringLiteral("workspace/packProjectZip"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Pack project zip.")),
        apiDescriptor(QStringLiteral("workspace.unpackProjectZip"), QStringLiteral("workspace/unpackProjectZip"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Unpack project zip.")),
        apiDescriptor(QStringLiteral("workspace.getProjectData"), QStringLiteral("workspace/getProjectData"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get extension project data.")),
        apiDescriptor(QStringLiteral("workspace.setProjectData"), QStringLiteral("workspace/setProjectData"), QStringLiteral("workspace.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Set extension project data.")),
        apiDescriptor(QStringLiteral("workspace.scanChartFolders"), QStringLiteral("workspace/scanChartFolders"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Scan chart folders.")),

        apiDescriptor(QStringLiteral("document.getText"), QStringLiteral("workspace/getActiveDocument"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current chart text.")),
        apiDescriptor(QStringLiteral("document.setText"), QStringLiteral("workspace/applyDocumentEdit"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set chart text.")),
        apiDescriptor(QStringLiteral("document.replaceText"), QStringLiteral("workspace/applyDocumentEdit"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace chart text.")),
        apiDescriptor(QStringLiteral("document.getLine"), QStringLiteral("document/getLine"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get line.")),
        apiDescriptor(QStringLiteral("document.getLines"), QStringLiteral("document/getLines"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get lines.")),
        apiDescriptor(QStringLiteral("document.getLineCount"), QStringLiteral("document/getLineCount"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get line count.")),
        apiDescriptor(QStringLiteral("document.insertLine"), QStringLiteral("document/insertLine"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Insert line.")),
        apiDescriptor(QStringLiteral("document.replaceLine"), QStringLiteral("document/replaceLine"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Replace line.")),
        apiDescriptor(QStringLiteral("document.deleteLine"), QStringLiteral("document/deleteLine"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Delete line.")),
        apiDescriptor(QStringLiteral("document.getDifficulties"), QStringLiteral("document/getDifficulties"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get difficulties.")),
        apiDescriptor(QStringLiteral("document.getActiveDifficulty"), QStringLiteral("document/getActiveDifficulty"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get active difficulty.")),
        apiDescriptor(QStringLiteral("document.setActiveDifficulty"), QStringLiteral("document/setActiveDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set active difficulty.")),
        apiDescriptor(QStringLiteral("document.addDifficulty"), QStringLiteral("document/createDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add difficulty.")),
        apiDescriptor(QStringLiteral("document.renameDifficulty"), QStringLiteral("document/renameDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Rename difficulty.")),
        apiDescriptor(QStringLiteral("document.removeDifficulty"), QStringLiteral("document/deleteDifficulty"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Remove difficulty.")),
        apiDescriptor(QStringLiteral("document.getMetadata"), QStringLiteral("workspace/getChartMetadata"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get metadata.")),
        apiDescriptor(QStringLiteral("document.updateMetadata"), QStringLiteral("workspace/updateChartMetadata"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Update metadata.")),
        apiDescriptor(QStringLiteral("document.getParsedNotes"), QStringLiteral("document/getParsedNoteMarkers"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get parsed notes.")),
        apiDescriptor(QStringLiteral("document.getStatistics"), QStringLiteral("document/getStatistics"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get statistics.")),
        apiDescriptor(QStringLiteral("document.normalize"), QStringLiteral("document/format"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Normalize document.")),
        apiDescriptor(QStringLiteral("document.mirrorLeftRight"), QStringLiteral("document/mirrorLeftRight"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Mirror left/right.")),
        apiDescriptor(QStringLiteral("document.mirrorUpDown"), QStringLiteral("document/mirrorUpDown"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Mirror up/down.")),
        apiDescriptor(QStringLiteral("document.rotate180"), QStringLiteral("document/rotate180"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Rotate 180.")),
        apiDescriptor(QStringLiteral("document.rotateClockwise45"), QStringLiteral("document/rotateClockwise45"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Rotate clockwise 45.")),
        apiDescriptor(QStringLiteral("document.rotateCounterClockwise45"), QStringLiteral("document/rotateCounterClockwise45"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Rotate counterclockwise 45.")),
        apiDescriptor(QStringLiteral("document.replaceActiveDifficultyText"), QStringLiteral("document/replaceActiveDifficultyText"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace active difficulty text.")),
        apiDescriptor(QStringLiteral("document.getTimingMetadata"), QStringLiteral("document/getTimingMetadata"), QStringLiteral("workspace.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get timing metadata.")),
        apiDescriptor(QStringLiteral("document.applyTextEdits"), QStringLiteral("document/applyTextEdits"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Apply text edits.")),

        apiDescriptor(QStringLiteral("editor.getCursor"), QStringLiteral("editor/getCursor"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get cursor.")),
        apiDescriptor(QStringLiteral("editor.setCursor"), QStringLiteral("editor/setCursor"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set cursor.")),
        apiDescriptor(QStringLiteral("editor.getSelection"), QStringLiteral("editor/getSelection"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get selection.")),
        apiDescriptor(QStringLiteral("editor.setSelection"), QStringLiteral("editor/setSelection"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set selection.")),
        apiDescriptor(QStringLiteral("editor.getVisibleRange"), QStringLiteral("editor/getVisibleRange"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get visible range.")),
        apiDescriptor(QStringLiteral("editor.revealRange"), QStringLiteral("editor/revealRange"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Reveal range.")),
        apiDescriptor(QStringLiteral("editor.getLineCount"), QStringLiteral("editor/getLineCount"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get line count.")),
        apiDescriptor(QStringLiteral("editor.getLineText"), QStringLiteral("editor/getLine"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get line text.")),
        apiDescriptor(QStringLiteral("editor.getLine"), QStringLiteral("editor/getLine"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get line.")),
        apiDescriptor(QStringLiteral("editor.getCurrentLine"), QStringLiteral("editor/getCurrentLine"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current line.")),
        apiDescriptor(QStringLiteral("editor.getCurrentToken"), QStringLiteral("editor/getCurrentToken"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current token.")),
        apiDescriptor(QStringLiteral("editor.insertText"), QStringLiteral("editor/insertText"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Insert text.")),
        apiDescriptor(QStringLiteral("editor.replaceSelection"), QStringLiteral("editor/replaceSelection"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace selection.")),
        apiDescriptor(QStringLiteral("editor.replaceRange"), QStringLiteral("editor/replaceRange"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Replace range.")),
        apiDescriptor(QStringLiteral("editor.deleteRange"), QStringLiteral("editor/deleteRange"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Delete range.")),
        apiDescriptor(QStringLiteral("editor.formatDocument"), QStringLiteral("document/format"), QStringLiteral("document.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Format document.")),
        apiDescriptor(QStringLiteral("editor.undo"), QStringLiteral("editor/undo"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Undo.")),
        apiDescriptor(QStringLiteral("editor.redo"), QStringLiteral("editor/redo"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Redo.")),
        apiDescriptor(QStringLiteral("editor.cut"), QStringLiteral("editor/cut"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Cut.")),
        apiDescriptor(QStringLiteral("editor.copy"), QStringLiteral("editor/copy"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Copy.")),
        apiDescriptor(QStringLiteral("editor.paste"), QStringLiteral("editor/paste"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Paste.")),
        apiDescriptor(QStringLiteral("editor.selectAll"), QStringLiteral("editor/selectAll"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Select all.")),
        apiDescriptor(QStringLiteral("editor.find"), QStringLiteral("editor/find"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Find.")),
        apiDescriptor(QStringLiteral("editor.replace"), QStringLiteral("editor/replace"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Replace.")),
        apiDescriptor(QStringLiteral("editor.addDecoration"), QStringLiteral("editor/addDecoration"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add editor decoration.")),
        apiDescriptor(QStringLiteral("editor.updateDecoration"), QStringLiteral("editor/updateDecoration"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Update editor decoration.")),
        apiDescriptor(QStringLiteral("editor.clearDecorations"), QStringLiteral("editor/clearDecorations"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear editor decorations.")),
        apiDescriptor(QStringLiteral("editor.addInlineHint"), QStringLiteral("editor/addInlineHint"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Add inline hint.")),
        apiDescriptor(QStringLiteral("editor.clearInlineHints"), QStringLiteral("editor/clearInlineHints"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Clear inline hints.")),
        apiDescriptor(QStringLiteral("editor.addCodeLens"), QStringLiteral("editor/addCodeLens"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Add CodeLens.")),
        apiDescriptor(QStringLiteral("editor.clearCodeLens"), QStringLiteral("editor/clearCodeLens"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Clear CodeLens.")),
        apiDescriptor(QStringLiteral("editor.addBookmark"), QStringLiteral("editor/addBookmark"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Add bookmark.")),
        apiDescriptor(QStringLiteral("editor.renameBookmark"), QStringLiteral("editor/renameBookmark"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Rename bookmark.")),
        apiDescriptor(QStringLiteral("editor.deleteBookmark"), QStringLiteral("editor/deleteBookmark"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Delete bookmark.")),
        apiDescriptor(QStringLiteral("editor.listBookmarks"), QStringLiteral("editor/listBookmarks"), QStringLiteral("editor.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("List bookmarks.")),
        apiDescriptor(QStringLiteral("editor.jumpToBookmark"), QStringLiteral("editor/jumpToBookmark"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Jump to bookmark.")),
        apiDescriptor(QStringLiteral("editor.showHover"), QStringLiteral("editor/showHover"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Show hover.")),
        apiDescriptor(QStringLiteral("editor.addGutterIcon"), QStringLiteral("editor/addGutterIcon"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add gutter icon.")),
        apiDescriptor(QStringLiteral("editor.clearGutterIcons"), QStringLiteral("editor/clearGutterIcons"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear gutter icons.")),
        apiDescriptor(QStringLiteral("editor.fold"), QStringLiteral("editor/fold"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Fold range.")),
        apiDescriptor(QStringLiteral("editor.unfold"), QStringLiteral("editor/unfold"), QStringLiteral("editor.edit"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Unfold range.")),
        apiDescriptor(QStringLiteral("editor.registerHoverProvider"), QStringLiteral("providers/hover"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register hover provider.")),
        apiDescriptor(QStringLiteral("editor.registerCompletionProvider"), QStringLiteral("providers/completion"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register completion provider.")),
        apiDescriptor(QStringLiteral("editor.registerCodeActionProvider"), QStringLiteral("providers/codeAction"), QStringLiteral("providers.register"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Register code action provider.")),

        apiDescriptor(QStringLiteral("validation.run"), QStringLiteral("validation/run"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Run validation.")),
        apiDescriptor(QStringLiteral("validation.getLastResult"), QStringLiteral("validation/getLastResult"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Get last validation result.")),
        apiDescriptor(QStringLiteral("validation.getDiagnostics"), QStringLiteral("validation/getDiagnostics"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Get diagnostics.")),
        apiDescriptor(QStringLiteral("validation.addDiagnostics"), QStringLiteral("validation/addDiagnostics"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add diagnostics.")),
        apiDescriptor(QStringLiteral("validation.clearDiagnostics"), QStringLiteral("validation/clearDiagnostics"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear diagnostics.")),
        apiDescriptor(QStringLiteral("validation.jumpToDiagnostic"), QStringLiteral("validation/jumpToDiagnostic"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Jump to diagnostic.")),
        apiDescriptor(QStringLiteral("validation.exportReport"), QStringLiteral("validation/exportReport"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export validation report.")),
        apiDescriptor(QStringLiteral("diagnostics.validateDocument"), QStringLiteral("diagnostics/validateDocument"), QStringLiteral("diagnostics.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Validate document.")),

        apiDescriptor(QStringLiteral("analysis.runMuriAnalysis"), QStringLiteral("analysis/runMuriAnalysis"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Run Muri analysis.")),
        apiDescriptor(QStringLiteral("analysis.getLastMuriResult"), QStringLiteral("analysis/getLastMuriResult"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Get last Muri result.")),
        apiDescriptor(QStringLiteral("analysis.getChartStats"), QStringLiteral("analysis/getChartStats"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Get chart stats.")),
        apiDescriptor(QStringLiteral("analysis.runChartStatistics"), QStringLiteral("analysis/runChartStatistics"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Run chart statistics.")),
        apiDescriptor(QStringLiteral("analysis.addResultPanel"), QStringLiteral("analysis/addResultPanel"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Add analysis result panel.")),
        apiDescriptor(QStringLiteral("analysis.clearResultPanel"), QStringLiteral("analysis/clearResultPanel"), QStringLiteral("analysis.run"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Clear analysis result panel.")),
        apiDescriptor(QStringLiteral("analysis.exportResult"), QStringLiteral("analysis/exportResult"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export analysis result.")),

        apiDescriptor(QStringLiteral("timeline.getSnapshot"), QStringLiteral("timeline/getSnapshot"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get timeline snapshot.")),
        apiDescriptor(QStringLiteral("timeline.getCurrentSecond"), QStringLiteral("timeline/getCurrentSecond"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current second.")),
        apiDescriptor(QStringLiteral("timeline.seek"), QStringLiteral("timeline/seek"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Seek timeline.")),
        apiDescriptor(QStringLiteral("timeline.getVisibleRange"), QStringLiteral("timeline/getVisibleRange"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get visible range.")),
        apiDescriptor(QStringLiteral("timeline.setVisibleRange"), QStringLiteral("timeline/setVisibleRange"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set visible range.")),
        apiDescriptor(QStringLiteral("timeline.getMarkers"), QStringLiteral("timeline/getMarkers"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get markers.")),
        apiDescriptor(QStringLiteral("timeline.addMarker"), QStringLiteral("timeline/addMarker"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add marker.")),
        apiDescriptor(QStringLiteral("timeline.updateMarker"), QStringLiteral("timeline/updateMarker"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Update marker.")),
        apiDescriptor(QStringLiteral("timeline.clearMarkers"), QStringLiteral("timeline/clearMarkers"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear markers.")),
        apiDescriptor(QStringLiteral("timeline.getPlaybackState"), QStringLiteral("timeline/getPlaybackState"), QStringLiteral("timeline.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get playback state.")),
        apiDescriptor(QStringLiteral("timeline.setTemporaryOverlay"), QStringLiteral("timeline/setTemporaryOverlay"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set temporary overlay.")),
        apiDescriptor(QStringLiteral("timeline.clearTemporaryOverlay"), QStringLiteral("timeline/clearTemporaryOverlay"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Clear temporary overlay.")),
        apiDescriptor(QStringLiteral("timeline.refresh"), QStringLiteral("timeline/refresh"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Refresh timeline.")),
        apiDescriptor(QStringLiteral("timeline.zoomIn"), QStringLiteral("timeline/zoomIn"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Zoom in.")),
        apiDescriptor(QStringLiteral("timeline.zoomOut"), QStringLiteral("timeline/zoomOut"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Zoom out.")),
        apiDescriptor(QStringLiteral("timeline.resetZoom"), QStringLiteral("timeline/resetZoom"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Reset zoom.")),
        apiDescriptor(QStringLiteral("timeline.addBand"), QStringLiteral("timeline/addBand"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add band.")),
        apiDescriptor(QStringLiteral("timeline.addVerticalLine"), QStringLiteral("timeline/addVerticalLine"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Add vertical line.")),
        apiDescriptor(QStringLiteral("timeline.clearVisuals"), QStringLiteral("timeline/clearVisuals"), QStringLiteral("timeline.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Clear timeline visuals.")),

        apiDescriptor(QStringLiteral("preview.getState"), QStringLiteral("preview/getState"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get preview state.")),
        apiDescriptor(QStringLiteral("preview.play"), QStringLiteral("preview/play"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Play preview.")),
        apiDescriptor(QStringLiteral("preview.pause"), QStringLiteral("preview/pause"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Pause preview.")),
        apiDescriptor(QStringLiteral("preview.stop"), QStringLiteral("preview/stop"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Stop preview.")),
        apiDescriptor(QStringLiteral("preview.seek"), QStringLiteral("preview/seek"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Seek preview.")),
        apiDescriptor(QStringLiteral("preview.getCurrentSecond"), QStringLiteral("preview/getCurrentSecond"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get preview current second.")),
        apiDescriptor(QStringLiteral("preview.getSpeed"), QStringLiteral("preview/getSpeed"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get speed.")),
        apiDescriptor(QStringLiteral("preview.setSpeed"), QStringLiteral("preview/setSpeed"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Set speed.")),
        apiDescriptor(QStringLiteral("preview.getResolution"), QStringLiteral("preview/getResolution"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get resolution.")),
        apiDescriptor(QStringLiteral("preview.setBackgroundMedia"), QStringLiteral("preview/setBackgroundMedia"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set background media.")),
        apiDescriptor(QStringLiteral("preview.clearBackgroundMedia"), QStringLiteral("preview/clearBackgroundMedia"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Clear background media.")),
        apiDescriptor(QStringLiteral("preview.setSkin"), QStringLiteral("preview/setSkin"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Set skin.")),
        apiDescriptor(QStringLiteral("preview.getSkin"), QStringLiteral("preview/getSkin"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get skin.")),
        apiDescriptor(QStringLiteral("preview.reloadSkin"), QStringLiteral("preview/reloadSkin"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Reload skin.")),
        apiDescriptor(QStringLiteral("preview.captureFrame"), QStringLiteral("preview/captureFrame"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Capture frame.")),
        apiDescriptor(QStringLiteral("preview.addOverlay"), QStringLiteral("preview/addOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register preview overlay metadata; visible overlay renderer is planned.")),
        apiDescriptor(QStringLiteral("preview.setOverlay"), QStringLiteral("preview/addOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Register preview overlay metadata; visible overlay renderer is planned.")),
        apiDescriptor(QStringLiteral("preview.updateOverlay"), QStringLiteral("preview/updateOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Update preview overlay.")),
        apiDescriptor(QStringLiteral("preview.removeOverlay"), QStringLiteral("preview/removeOverlay"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Remove preview overlay.")),
        apiDescriptor(QStringLiteral("preview.clearOverlay"), QStringLiteral("preview/clearOverlays"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Clear registered preview overlay metadata; visible overlay renderer is planned.")),
        apiDescriptor(QStringLiteral("preview.clearOverlays"), QStringLiteral("preview/clearOverlays"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("partial"), QStringLiteral("Clear registered preview overlay metadata; visible overlay renderer is planned.")),
        apiDescriptor(QStringLiteral("preview.getOverlays"), QStringLiteral("preview/getOverlays"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get registered preview overlays.")),
        apiDescriptor(QStringLiteral("preview.renderOverlayLayer"), QStringLiteral("preview/renderOverlayLayer"), QStringLiteral("preview.control"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Render visible preview overlay layer.")),
        apiDescriptor(QStringLiteral("preview.hitTestOverlay"), QStringLiteral("preview/hitTestOverlay"), QStringLiteral("preview.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Hit-test preview overlay.")),

        apiDescriptor(QStringLiteral("export.getPresets"), QStringLiteral("export/getPresets"), QStringLiteral("export.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get export presets.")),
        apiDescriptor(QStringLiteral("export.registerPreset"), QStringLiteral("export/registerPreset"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register export preset.")),
        apiDescriptor(QStringLiteral("export.unregisterPreset"), QStringLiteral("export/unregisterPreset"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Unregister export preset.")),
        apiDescriptor(QStringLiteral("export.startVideoExport"), QStringLiteral("export/startVideoExport"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Start video export.")),
        apiDescriptor(QStringLiteral("export.startCoverExport"), QStringLiteral("export/startCoverExport"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Start cover export.")),
        apiDescriptor(QStringLiteral("export.startBatchExport"), QStringLiteral("export/startBatchExport"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Start batch export.")),
        apiDescriptor(QStringLiteral("export.cancelExport"), QStringLiteral("export/cancelExport"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Cancel export.")),
        apiDescriptor(QStringLiteral("export.getExportState"), QStringLiteral("export/getExportState"), QStringLiteral("export.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get export state.")),
        apiDescriptor(QStringLiteral("export.getLastExportResult"), QStringLiteral("export/getLastExportResult"), QStringLiteral("export.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get last export result.")),
        apiDescriptor(QStringLiteral("export.openOutputFolder"), QStringLiteral("export/openOutputFolder"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Open output folder.")),
        apiDescriptor(QStringLiteral("export.validateOptions"), QStringLiteral("export/validateOptions"), QStringLiteral("export.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Validate export options.")),
        apiDescriptor(QStringLiteral("export.registerBeforeExportHook"), QStringLiteral("export/beforeHook"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register before-export hook.")),
        apiDescriptor(QStringLiteral("export.registerAfterExportHook"), QStringLiteral("export/afterHook"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register after-export hook.")),
        apiDescriptor(QStringLiteral("export.registerCoverTemplate"), QStringLiteral("export/coverTemplate"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register cover template.")),
        apiDescriptor(QStringLiteral("export.registerBatchJobProvider"), QStringLiteral("export/batchJobProvider"), QStringLiteral("export.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Register batch job provider.")),

        apiDescriptor(QStringLiteral("media.getTrackInfo"), QStringLiteral("media/getTrackInfo"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get track info.")),
        apiDescriptor(QStringLiteral("media.readMetadata"), QStringLiteral("media/readMetadata"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Read media metadata.")),
        apiDescriptor(QStringLiteral("media.extractAudioInfo"), QStringLiteral("media/extractAudioInfo"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Extract audio info.")),
        apiDescriptor(QStringLiteral("media.generateWaveform"), QStringLiteral("media/generateWaveform"), QStringLiteral("resources.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Generate waveform.")),
        apiDescriptor(QStringLiteral("media.transcodePreview"), QStringLiteral("media/transcodePreview"), QStringLiteral("resources.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Transcode preview media.")),
        apiDescriptor(QStringLiteral("media.getDuration"), QStringLiteral("media/getDuration"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get media duration.")),
        apiDescriptor(QStringLiteral("media.checkCompatibility"), QStringLiteral("media/checkCompatibility"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Check media compatibility.")),
        apiDescriptor(QStringLiteral("media.resolveBackgroundMedia"), QStringLiteral("media/resolveBackgroundMedia"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Resolve background media.")),
        apiDescriptor(QStringLiteral("media.resolveTrackFile"), QStringLiteral("media/resolveTrackFile"), QStringLiteral("resources.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Resolve track file.")),

        apiDescriptor(QStringLiteral("filesystem.readText"), QStringLiteral("fs/readText"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Read text file.")),
        apiDescriptor(QStringLiteral("filesystem.writeText"), QStringLiteral("fs/writeText"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Write text file.")),
        apiDescriptor(QStringLiteral("filesystem.readBytes"), QStringLiteral("fs/readBytes"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Read bytes.")),
        apiDescriptor(QStringLiteral("filesystem.writeBytes"), QStringLiteral("fs/writeBytes"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Write bytes.")),
        apiDescriptor(QStringLiteral("filesystem.exists"), QStringLiteral("fs/exists"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Check path exists.")),
        apiDescriptor(QStringLiteral("filesystem.stat"), QStringLiteral("fs/stat"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Get file stat.")),
        apiDescriptor(QStringLiteral("filesystem.listDir"), QStringLiteral("fs/listDir"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("List directory.")),
        apiDescriptor(QStringLiteral("filesystem.createDir"), QStringLiteral("fs/createDir"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Create directory.")),
        apiDescriptor(QStringLiteral("filesystem.copy"), QStringLiteral("fs/copy"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Copy path.")),
        apiDescriptor(QStringLiteral("filesystem.move"), QStringLiteral("fs/move"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Move path.")),
        apiDescriptor(QStringLiteral("filesystem.rename"), QStringLiteral("fs/rename"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Rename path.")),
        apiDescriptor(QStringLiteral("filesystem.delete"), QStringLiteral("fs/delete"), QStringLiteral("filesystem.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Delete path.")),
        apiDescriptor(QStringLiteral("filesystem.watch"), QStringLiteral("fs/watch"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Watch path.")),
        apiDescriptor(QStringLiteral("filesystem.openInExplorer"), QStringLiteral("fs/openInExplorer"), QStringLiteral("filesystem.read"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Open in Explorer.")),
        apiDescriptor(QStringLiteral("filesystem.readAnyPathWithoutPrompt"), QStringLiteral("fs/readAnyPathWithoutPrompt"), QStringLiteral("filesystem.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Read any path without prompt.")),
        apiDescriptor(QStringLiteral("filesystem.writeAnyPathWithoutPrompt"), QStringLiteral("fs/writeAnyPathWithoutPrompt"), QStringLiteral("filesystem.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Write any path without prompt.")),
        apiDescriptor(QStringLiteral("filesystem.deleteAnyPathWithoutPrompt"), QStringLiteral("fs/deleteAnyPathWithoutPrompt"), QStringLiteral("filesystem.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Delete any path without prompt.")),

        apiDescriptor(QStringLiteral("network.fetch"), QStringLiteral("net/fetch"), QStringLiteral("network.fetch"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Fetch URL.")),
        apiDescriptor(QStringLiteral("network.download"), QStringLiteral("net/download"), QStringLiteral("network.fetch"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Download URL.")),
        apiDescriptor(QStringLiteral("network.upload"), QStringLiteral("net/upload"), QStringLiteral("network.fetch"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Upload file.")),
        apiDescriptor(QStringLiteral("network.websocketConnect"), QStringLiteral("net/websocketConnect"), QStringLiteral("network.fetch"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Connect WebSocket.")),
        apiDescriptor(QStringLiteral("network.getProxySettings"), QStringLiteral("net/getProxySettings"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get proxy settings.")),
        apiDescriptor(QStringLiteral("network.setProxySettings"), QStringLiteral("net/setProxySettings"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Set proxy settings.")),
        apiDescriptor(QStringLiteral("network.fetchLocalhostWithoutPrompt"), QStringLiteral("net/fetchLocalhostWithoutPrompt"), QStringLiteral("network.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Fetch localhost without prompt.")),
        apiDescriptor(QStringLiteral("network.fetchPrivateNetworkWithoutPrompt"), QStringLiteral("net/fetchPrivateNetworkWithoutPrompt"), QStringLiteral("network.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Fetch private network without prompt.")),
        apiDescriptor(QStringLiteral("network.uploadWithoutPrompt"), QStringLiteral("net/uploadWithoutPrompt"), QStringLiteral("network.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Upload without prompt.")),

        apiDescriptor(QStringLiteral("settings.get"), QStringLiteral("settings/get"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get setting.")),
        apiDescriptor(QStringLiteral("settings.set"), QStringLiteral("settings/set"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Set setting.")),
        apiDescriptor(QStringLiteral("settings.reset"), QStringLiteral("settings/reset"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Reset setting.")),
        apiDescriptor(QStringLiteral("settings.listKeys"), QStringLiteral("settings/listKeys"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("List setting keys.")),
        apiDescriptor(QStringLiteral("settings.onDidChange"), QStringLiteral("events/settings.onDidChange"), QStringLiteral("events.subscribe"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Listen setting changes.")),
        apiDescriptor(QStringLiteral("settings.import"), QStringLiteral("settings/import"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Import settings.")),
        apiDescriptor(QStringLiteral("settings.export"), QStringLiteral("settings/export"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Export settings.")),
        apiDescriptor(QStringLiteral("settings.setSecuritySensitive"), QStringLiteral("settings/setSecuritySensitive"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Set security-sensitive setting.")),

        apiDescriptor(QStringLiteral("theme.getCurrent"), QStringLiteral("theme/getCurrent"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Get current theme.")),
        apiDescriptor(QStringLiteral("theme.setCurrent"), QStringLiteral("theme/setCurrent"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Set current theme.")),
        apiDescriptor(QStringLiteral("theme.listAvailable"), QStringLiteral("theme/listAvailable"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("List themes.")),
        apiDescriptor(QStringLiteral("theme.registerTheme"), QStringLiteral("theme/registerTheme"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Register theme.")),
        apiDescriptor(QStringLiteral("theme.unregisterTheme"), QStringLiteral("theme/unregisterTheme"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Unregister theme.")),
        apiDescriptor(QStringLiteral("theme.getColor"), QStringLiteral("theme/getColor"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get theme color.")),
        apiDescriptor(QStringLiteral("theme.setColorOverride"), QStringLiteral("theme/setColorOverride"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Set color override.")),
        apiDescriptor(QStringLiteral("theme.clearColorOverride"), QStringLiteral("theme/clearColorOverride"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Clear color override.")),

        apiDescriptor(QStringLiteral("shortcuts.list"), QStringLiteral("shortcuts/list"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("List shortcuts.")),
        apiDescriptor(QStringLiteral("shortcuts.getKeybinding"), QStringLiteral("shortcuts/getKeybinding"), QStringLiteral("settings.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Get keybinding.")),
        apiDescriptor(QStringLiteral("shortcuts.setKeybinding"), QStringLiteral("shortcuts/setKeybinding"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Set keybinding.")),
        apiDescriptor(QStringLiteral("shortcuts.resetKeybinding"), QStringLiteral("shortcuts/resetKeybinding"), QStringLiteral("settings.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Reset keybinding.")),
        apiDescriptor(QStringLiteral("shortcuts.registerCommandBinding"), QStringLiteral("shortcuts/registerCommandBinding"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Register command binding.")),
        apiDescriptor(QStringLiteral("shortcuts.unregisterCommandBinding"), QStringLiteral("shortcuts/unregisterCommandBinding"), QStringLiteral("ui.contribute"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Unregister command binding.")),

        apiDescriptor(QStringLiteral("clipboard.readText"), QStringLiteral("clipboard/readText"), QStringLiteral("clipboard.read"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Read clipboard text.")),
        apiDescriptor(QStringLiteral("clipboard.writeText"), QStringLiteral("clipboard/writeText"), QStringLiteral("clipboard.write"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Write clipboard text.")),
        apiDescriptor(QStringLiteral("clipboard.readImage"), QStringLiteral("clipboard/readImage"), QStringLiteral("clipboard.read"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Read clipboard image.")),
        apiDescriptor(QStringLiteral("clipboard.writeImage"), QStringLiteral("clipboard/writeImage"), QStringLiteral("clipboard.write"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Write clipboard image.")),
        apiDescriptor(QStringLiteral("nativeDialogs.openFile"), QStringLiteral("nativeDialogs/openFile"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Open file dialog.")),
        apiDescriptor(QStringLiteral("nativeDialogs.saveFile"), QStringLiteral("nativeDialogs/saveFile"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Save file dialog.")),
        apiDescriptor(QStringLiteral("nativeDialogs.selectFolder"), QStringLiteral("nativeDialogs/selectFolder"), QStringLiteral("ui.prompt"), QStringLiteral("medium"), QStringLiteral("implemented"), QStringLiteral("Select folder dialog.")),

        apiDescriptor(QStringLiteral("logs.readRecent"), QStringLiteral("logs/readRecent"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Read recent logs.")),
        apiDescriptor(QStringLiteral("logs.writeExtensionLog"), QStringLiteral("logs/append"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Write extension log.")),
        apiDescriptor(QStringLiteral("logs.openFolder"), QStringLiteral("logs/open"), QStringLiteral("logs.read"), QStringLiteral("low"), QStringLiteral("implemented"), QStringLiteral("Open logs folder.")),
        apiDescriptor(QStringLiteral("logs.exportDiagnosticBundle"), QStringLiteral("logs/exportDiagnosticBundle"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export diagnostic bundle.")),
        apiDescriptor(QStringLiteral("logs.clearExtensionLogs"), QStringLiteral("logs/clearExtensionLogs"), QStringLiteral("logs.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Clear extension logs.")),

        apiDescriptor(QStringLiteral("backup.createSnapshot"), QStringLiteral("backup/createSnapshot"), QStringLiteral("backup.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Create backup snapshot.")),
        apiDescriptor(QStringLiteral("backup.listSnapshots"), QStringLiteral("backup/listSnapshots"), QStringLiteral("backup.read"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("List backup snapshots.")),
        apiDescriptor(QStringLiteral("backup.restoreSnapshot"), QStringLiteral("backup/restoreSnapshot"), QStringLiteral("backup.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Restore backup snapshot.")),
        apiDescriptor(QStringLiteral("backup.deleteSnapshot"), QStringLiteral("backup/deleteSnapshot"), QStringLiteral("backup.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Delete backup snapshot.")),
        apiDescriptor(QStringLiteral("backup.exportSnapshot"), QStringLiteral("backup/exportSnapshot"), QStringLiteral("backup.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Export backup snapshot.")),
        apiDescriptor(QStringLiteral("backup.importSnapshot"), QStringLiteral("backup/importSnapshot"), QStringLiteral("backup.write"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Import backup snapshot.")),

        apiDescriptor(QStringLiteral("objects.list"), QStringLiteral("objects/list"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("List object proxies.")),
        apiDescriptor(QStringLiteral("objects.describe"), QStringLiteral("objects/describe"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Describe object proxy.")),
        apiDescriptor(QStringLiteral("objects.call"), QStringLiteral("objects/call"), QStringLiteral("internal.call"), QStringLiteral("high"), QStringLiteral("planned"), QStringLiteral("Call object proxy.")),
        apiDescriptor(QStringLiteral("objects.on"), QStringLiteral("objects/on"), QStringLiteral("events.subscribe"), QStringLiteral("low"), QStringLiteral("planned"), QStringLiteral("Subscribe object event.")),
        apiDescriptor(QStringLiteral("objects.inspect"), QStringLiteral("objects/inspect"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect object state.")),
        apiDescriptor(QStringLiteral("internal.inspectMainWindow"), QStringLiteral("internal/inspectMainWindow"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect main window state.")),
        apiDescriptor(QStringLiteral("internal.inspectDocumentModel"), QStringLiteral("internal/inspectDocumentModel"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect document model.")),
        apiDescriptor(QStringLiteral("internal.inspectTimelineState"), QStringLiteral("internal/inspectTimelineState"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect timeline state.")),
        apiDescriptor(QStringLiteral("internal.inspectPreviewState"), QStringLiteral("internal/inspectPreviewState"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect preview state.")),
        apiDescriptor(QStringLiteral("internal.inspectExportState"), QStringLiteral("internal/inspectExportState"), QStringLiteral("internal.inspect"), QStringLiteral("medium"), QStringLiteral("planned"), QStringLiteral("Inspect export state.")),

        // Dangerous expert APIs: intentionally exposed without a separate key per product
        // decision. They remain tagged as extreme so UI, docs, logs, and future marketplace
        // policy can make the risk visible.
        apiDescriptor(QStringLiteral("shell.execute"), QStringLiteral("shell/execute"), QStringLiteral("shell.execute"), QStringLiteral("extreme"), QStringLiteral("implemented"), QStringLiteral("Execute shell command. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("process.spawn"), QStringLiteral("process/spawn"), QStringLiteral("process.manage"), QStringLiteral("extreme"), QStringLiteral("implemented"), QStringLiteral("Spawn external process. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("process.kill"), QStringLiteral("process/kill"), QStringLiteral("process.manage"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Kill process. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("process.openFileWithSystem"), QStringLiteral("process/openFileWithSystem"), QStringLiteral("process.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Open file with system handler.")),
        apiDescriptor(QStringLiteral("process.revealInFileExplorer"), QStringLiteral("process/revealInFileExplorer"), QStringLiteral("process.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Reveal file in explorer.")),
        apiDescriptor(QStringLiteral("process.openUrlInBrowser"), QStringLiteral("process/openUrlInBrowser"), QStringLiteral("process.manage"), QStringLiteral("high"), QStringLiteral("implemented"), QStringLiteral("Open URL in browser.")),
        apiDescriptor(QStringLiteral("process.inject"), QStringLiteral("process/inject"), QStringLiteral("process.manage"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Process injection. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("native.loadDll"), QStringLiteral("native/loadDll"), QStringLiteral("native.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Load DLL. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("native.callFunction"), QStringLiteral("native/callFunction"), QStringLiteral("native.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Call native function. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("native.loadQtPlugin"), QStringLiteral("native/loadQtPlugin"), QStringLiteral("native.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Load Qt plugin. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("native.loadQmlPlugin"), QStringLiteral("native/loadQmlPlugin"), QStringLiteral("native.unsafe"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Load QML plugin. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getMainWindowPointer"), QStringLiteral("internal/getMainWindowPointer"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw MainWindow pointer. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getQObjectPointer"), QStringLiteral("internal/getQObjectPointer"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw QObject pointer. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getTimelineViewPointer"), QStringLiteral("internal/getTimelineViewPointer"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw TimelineView pointer. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getPreviewRendererPointer"), QStringLiteral("internal/getPreviewRendererPointer"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw preview renderer pointer. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getDocumentRawPointer"), QStringLiteral("internal/getDocumentRawPointer"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw document pointer. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.getQmlEngine"), QStringLiteral("internal/getQmlEngine"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get QML engine. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.evalQml"), QStringLiteral("internal/evalQml"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Evaluate QML. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("internal.evalCppExpression"), QStringLiteral("internal/evalCppExpression"), QStringLiteral("internal.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Evaluate C++ expression. Extremely dangerous.")),
        apiDescriptor(QStringLiteral("renderer.getRawFrameBuffer"), QStringLiteral("renderer/getRawFrameBuffer"), QStringLiteral("renderer.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get raw frame buffer.")),
        apiDescriptor(QStringLiteral("renderer.writeRawFrameBuffer"), QStringLiteral("renderer/writeRawFrameBuffer"), QStringLiteral("renderer.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Write raw frame buffer.")),
        apiDescriptor(QStringLiteral("renderer.getGraphicsDevice"), QStringLiteral("renderer/getGraphicsDevice"), QStringLiteral("renderer.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get graphics device.")),
        apiDescriptor(QStringLiteral("renderer.getSwapchain"), QStringLiteral("renderer/getSwapchain"), QStringLiteral("renderer.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get swapchain.")),
        apiDescriptor(QStringLiteral("renderer.injectShader"), QStringLiteral("renderer/injectShader"), QStringLiteral("renderer.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Inject shader.")),
        apiDescriptor(QStringLiteral("export.getWorkerRawState"), QStringLiteral("export/getWorkerRawState"), QStringLiteral("export.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Get export worker raw state.")),
        apiDescriptor(QStringLiteral("export.modifyWorkerCommand"), QStringLiteral("export/modifyWorkerCommand"), QStringLiteral("export.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Modify export worker command.")),
        apiDescriptor(QStringLiteral("export.overrideFfmpegArgsRaw"), QStringLiteral("export/overrideFfmpegArgsRaw"), QStringLiteral("export.raw"), QStringLiteral("extreme"), QStringLiteral("planned"), QStringLiteral("Override raw ffmpeg args.")),
        // Permanently blocked APIs: queryable for transparency, never callable by extensions.
        apiDescriptor(QStringLiteral("security.disablePermissionChecks"), QStringLiteral("security/disablePermissionChecks"), QStringLiteral("security.override"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not disable permission checks.")),
        apiDescriptor(QStringLiteral("security.grantAllPermissions"), QStringLiteral("security/grantAllPermissions"), QStringLiteral("security.override"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not grant all permissions.")),
        apiDescriptor(QStringLiteral("security.modifyTrustedExtensions"), QStringLiteral("security/modifyTrustedExtensions"), QStringLiteral("security.override"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not modify trusted extensions.")),
        apiDescriptor(QStringLiteral("security.installUnsignedSilently"), QStringLiteral("security/installUnsignedSilently"), QStringLiteral("security.override"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not silently install unsigned extensions.")),
        apiDescriptor(QStringLiteral("updates.replaceExecutable"), QStringLiteral("updates/replaceExecutable"), QStringLiteral("updates.modify"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not replace MiaCode executable.")),
        apiDescriptor(QStringLiteral("updates.patchApplicationFiles"), QStringLiteral("updates/patchApplicationFiles"), QStringLiteral("updates.modify"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not patch MiaCode application files.")),
        apiDescriptor(QStringLiteral("updates.autoInstallWithoutPrompt"), QStringLiteral("updates/autoInstallWithoutPrompt"), QStringLiteral("updates.modify"), QStringLiteral("blocked"), QStringLiteral("blocked"), QStringLiteral("Blocked permanently: extensions may not auto-install updates without prompt.")),
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

QString ExtensionManager::permissionSummary(const ExtensionRecord& record) const
{
    if (!record.valid) {
        return QStringLiteral("-");
    }
    const QStringList permissions = permissionLabels(record.manifest.permissions);
    if (permissions.isEmpty()) {
        return UiText::isChineseUi()
            ? QStringLiteral("无特权 API 权限")
            : QStringLiteral("No privileged API permissions");
    }
    QStringList parts;
    for (const QString& permission : permissions) {
        const QString risk = permissionRisk(permission);
        if (risk == QStringLiteral("high")) {
            parts.append(UiText::isChineseUi()
                ? QStringLiteral("高风险：%1").arg(permission)
                : QStringLiteral("HIGH:%1").arg(permission));
        } else if (risk == QStringLiteral("medium")) {
            parts.append(UiText::isChineseUi()
                ? QStringLiteral("中风险：%1").arg(permission)
                : QStringLiteral("MED:%1").arg(permission));
        } else {
            parts.append(permission);
        }
    }
    return parts.join(QStringLiteral(", "));
}

void ExtensionManager::revokeExtensionPermissionGrants(const QString& qualifiedId)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject extensions = root.value(QStringLiteral("extensions")).toObject();
    QJsonObject permissions = extensions.value(QStringLiteral("permissions")).toObject();
    permissions.remove(qualifiedId);
    extensions.insert(QStringLiteral("permissions"), permissions);
    root.insert(QStringLiteral("extensions"), extensions);
    UiText::savePreferencesObject(root);
    diagnostics_.append(QStringLiteral("Revoked permission grants for %1.").arg(qualifiedId));
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
            diagnostics_.append(QStringLiteral("%1: %2").arg(entry.fileName(), parsed.error));
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
                diagnostics_.append(QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic));
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
                diagnostics_.append(QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic));
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
                diagnostics_.append(QStringLiteral("%1: %2").arg(entry.fileName(), record.diagnostic));
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

QString ExtensionManager::permissionForMethod(const QString& method) const
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
        return QStringLiteral("providers.register");
    }
    if (method == QStringLiteral("events/register")) {
        return QStringLiteral("events.subscribe");
    }
    if (method.startsWith(QStringLiteral("events/"))) {
        return QStringLiteral("events.subscribe");
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
    if (method.startsWith(QStringLiteral("document/get"))) {
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
    if (method == QStringLiteral("commands/execute")) {
        return QStringLiteral("commands.execute");
    }
    if (method == QStringLiteral("commands/getCommands")) {
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
            ? QStringLiteral("settings.read")
            : QStringLiteral("settings.write");
    }
    if (method.startsWith(QStringLiteral("nativeDialogs/"))) {
        return QStringLiteral("ui.prompt");
    }
    if (method.startsWith(QStringLiteral("backup/"))) {
        return method.contains(QStringLiteral("list"), Qt::CaseInsensitive)
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

QString ExtensionManager::permissionRisk(const QString& permission) const
{
    static const QSet<QString> high{
        QStringLiteral("filesystem.read"),
        QStringLiteral("filesystem.write"),
        QStringLiteral("network.fetch"),
        QStringLiteral("extensions.manage"),
        QStringLiteral("settings.write"),
        QStringLiteral("export.write"),
        QStringLiteral("workspace.write"),
        QStringLiteral("resources.write"),
        QStringLiteral("logs.write"),
        QStringLiteral("app.lifecycle"),
        QStringLiteral("backup.write"),
        QStringLiteral("filesystem.unsafe"),
        QStringLiteral("network.unsafe"),
        QStringLiteral("process.manage"),
        QStringLiteral("shell.execute"),
        QStringLiteral("native.unsafe"),
        QStringLiteral("internal.call"),
        QStringLiteral("internal.raw"),
        QStringLiteral("renderer.raw"),
        QStringLiteral("export.raw"),
        QStringLiteral("security.override"),
        QStringLiteral("updates.modify"),
    };
    static const QSet<QString> medium{
        QStringLiteral("document.edit"),
        QStringLiteral("editor.edit"),
        QStringLiteral("commands.execute"),
        QStringLiteral("diagnostics.run"),
        QStringLiteral("analysis.run"),
        QStringLiteral("timeline.control"),
        QStringLiteral("preview.control"),
        QStringLiteral("ui.prompt"),
        QStringLiteral("ui.contribute"),
        QStringLiteral("providers.register"),
        QStringLiteral("tasks.run"),
        QStringLiteral("clipboard.read"),
        QStringLiteral("clipboard.write"),
        QStringLiteral("internal.inspect"),
    };
    if (high.contains(permission)) {
        return QStringLiteral("high");
    }
    if (medium.contains(permission)) {
        return QStringLiteral("medium");
    }
    return QStringLiteral("low");
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

bool ExtensionManager::isPermissionGranted(const QString& extensionId, const QString& permission) const
{
    return permissionGrantsObject().value(extensionId).toObject().value(permission).toBool(false);
}

void ExtensionManager::savePermissionGrant(const QString& extensionId, const QString& permission, bool granted)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject extensions = root.value(QStringLiteral("extensions")).toObject();
    QJsonObject permissions = extensions.value(QStringLiteral("permissions")).toObject();
    QJsonObject extensionPermissions = permissions.value(extensionId).toObject();
    if (granted) {
        extensionPermissions.insert(permission, true);
    } else {
        extensionPermissions.remove(permission);
    }
    permissions.insert(extensionId, extensionPermissions);
    extensions.insert(QStringLiteral("permissions"), permissions);
    root.insert(QStringLiteral("extensions"), extensions);
    UiText::savePreferencesObject(root);
}

bool ExtensionManager::ensurePermission(const QString& extensionId, const QString& method, const QJsonObject& params, QJsonObject* errorResponse)
{
    const QString permission = permissionForMethod(method);
    if (permission.isEmpty()) {
        return true;
    }
    if (extensionId.isEmpty()) {
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(QStringLiteral("Privileged API '%1' requires an extension context.").arg(method));
        }
        return false;
    }
    if (!manifestDeclaresPermission(extensionId, permission)) {
        const QString error = QStringLiteral("%1 denied: extension '%2' did not declare permission '%3'.")
                                  .arg(method, extensionId, permission);
        diagnostics_.append(error);
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    if (permissionRisk(permission) != QStringLiteral("high") || isPermissionGranted(extensionId, permission)) {
        return true;
    }
    const QString detail = params.value(QStringLiteral("path")).toString(
        params.value(QStringLiteral("url")).toString(method));
    const bool approved = callbacks_.confirmHighRisk
        ? callbacks_.confirmHighRisk(extensionId, permission, detail)
        : false;
    if (!approved) {
        const QString error = QStringLiteral("%1 denied: user did not grant high-risk permission '%2'.").arg(method, permission);
        diagnostics_.append(error);
        if (errorResponse != nullptr) {
            *errorResponse = errorObject(error);
        }
        return false;
    }
    savePermissionGrant(extensionId, permission, true);
    return true;
}

QJsonObject ExtensionManager::handleHostRequest(const QString& method, const QJsonObject& params)
{
    QJsonObject permissionError;
    const QString extensionId = params.value(QStringLiteral("extensionId")).toString();
    if (!ensurePermission(extensionId, method, params, &permissionError)) {
        return permissionError;
    }
    if (method == QStringLiteral("log")) {
        if (callbacks_.logMessage) {
            callbacks_.logMessage(params.value(QStringLiteral("message")).toString());
        }
        return QJsonObject{{QStringLiteral("ok"), true}};
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
    if (method == QStringLiteral("api/call")) {
        const QString apiId = params.value(QStringLiteral("id")).toString();
        const QJsonObject descriptor = findApiDescriptor(apiId);
        if (descriptor.isEmpty()) {
            return errorObject(QStringLiteral("Unknown API: %1").arg(apiId));
        }
        if (descriptor.value(QStringLiteral("status")).toString() == QStringLiteral("blocked")) {
            return errorObject(QStringLiteral("API '%1' is blocked and may only be queried with api.describe/list.").arg(apiId));
        }
        const QString targetMethod = descriptor.value(QStringLiteral("method")).toString();
        if (isPermanentlyBlockedApiMethod(targetMethod)) {
            return errorObject(QStringLiteral("API method '%1' is permanently blocked.").arg(targetMethod));
        }
        QJsonObject forwarded = params;
        forwarded.remove(QStringLiteral("id"));
        QJsonObject forwardedPermissionError;
        if (!ensurePermission(extensionId, targetMethod, forwarded, &forwardedPermissionError)) {
            return forwardedPermissionError;
        }
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
        } else if (apiId == QStringLiteral("ui.registerToolbarButton")) {
            forwarded.insert(QStringLiteral("kind"), QStringLiteral("ui/toolbarButton"));
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
    if (method == QStringLiteral("commands/execute")) {
        const QString command = params.value(QStringLiteral("command")).toString();
        if (!commandOwnerById_.contains(command)) {
            return errorObject(QStringLiteral("Unknown or unavailable command: %1").arg(command));
        }
        invokeCommand(command);
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method == QStringLiteral("app/getInfo")) {
        return okValue(QJsonObject{
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("applicationName"), QCoreApplication::applicationName()},
            {QStringLiteral("platform"), QSysInfo::productType()},
            {QStringLiteral("platformVersion"), QSysInfo::productVersion()},
            {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("executablePath"), QCoreApplication::applicationFilePath()},
            {QStringLiteral("installRoot"), QDir(QCoreApplication::applicationDirPath()).dirName().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0
                ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."))
                : QCoreApplication::applicationDirPath()},
            {QStringLiteral("extensionsRoot"), userExtensionsDirectory()},
            {QStringLiteral("logsRoot"), extensionLogDirectory()},
            {QStringLiteral("configRoot"), QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)},
            {QStringLiteral("tempRoot"), QDir::tempPath()},
            {QStringLiteral("locale"), QLocale::system().name()},
        });
    }
    if (method == QStringLiteral("app/reloadExtensions")) {
        refreshExtensions();
        return QJsonObject{{QStringLiteral("ok"), true}};
    }
    if (method.startsWith(QStringLiteral("fs/"))) {
        const QString path = params.value(QStringLiteral("path")).toString();
        const QFileInfo info(path);
        QString extensionRoot;
        for (const ExtensionManifest& manifest : manifests_) {
            if (manifest.qualifiedId() == extensionId) {
                extensionRoot = manifest.rootPath;
                break;
            }
        }
        const QJsonObject chartFolderResponse = callbacks_.mainWindowRequest
            ? callbacks_.mainWindowRequest(QStringLiteral("workspace/getChartFolder"), params)
            : QJsonObject();
        const QString chartFolder = chartFolderResponse.value(QStringLiteral("value")).toString();
        const bool defaultAllowed = isPathInside(path, extensionRoot)
            || isPathInside(path, chartFolder)
            || (method != QStringLiteral("fs/writeText") && isPathInside(path, extensionLogDirectory()));
        if (!defaultAllowed && !isPermissionGranted(extensionId, permissionForMethod(method))) {
            return errorObject(QStringLiteral("File path is outside the default extension, chart, and log roots: %1").arg(path));
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
        if (method == QStringLiteral("fs/writeText")) {
            QFile file(path);
            if (!QDir().mkpath(info.absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return errorObject(QStringLiteral("Cannot write file: %1").arg(file.errorString()));
            }
            file.write(params.value(QStringLiteral("text")).toString().toUtf8());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
    }
    if (method == QStringLiteral("net/fetch") || method == QStringLiteral("net/download")) {
        const QUrl url(params.value(QStringLiteral("url")).toString());
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
            return errorObject(QStringLiteral("Only http/https URLs are supported."));
        }
        if (isPrivateOrLocalNetworkTarget(url) && !isPermissionGranted(extensionId, QStringLiteral("network.fetch.local"))) {
            const bool approved = callbacks_.confirmHighRisk
                ? callbacks_.confirmHighRisk(extensionId, QStringLiteral("network.fetch.local"), url.toString())
                : false;
            if (!approved) {
                return errorObject(QStringLiteral("Localhost and private-network targets are blocked by default: %1").arg(url.host()));
            }
            savePermissionGrant(extensionId, QStringLiteral("network.fetch.local"), true);
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
    if (method == QStringLiteral("theme/getCurrent")) {
        const QJsonObject root = UiText::loadPreferencesObject();
        return okValue(root.value(QStringLiteral("theme")));
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
        // Extreme-risk API: declared by manifest, confirmed by permission flow, then executed by MiaCode.
        const QString program = params.value(QStringLiteral("program")).toString(
            params.value(QStringLiteral("command")).toString()).trimmed();
        if (program.isEmpty()) {
            return errorObject(QStringLiteral("Missing shell command or program."));
        }
        QStringList arguments;
        for (const QJsonValue& value : params.value(QStringLiteral("args")).toArray()) {
            arguments.append(value.toString());
        }
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.setWorkingDirectory(params.value(QStringLiteral("cwd")).toString(QDir::currentPath()));
        process.start();
        if (!process.waitForStarted(params.value(QStringLiteral("startTimeoutMs")).toInt(5000))) {
            return errorObject(QStringLiteral("Failed to start process: %1").arg(process.errorString()));
        }
        const int timeoutMs = qBound(1000, params.value(QStringLiteral("timeoutMs")).toInt(30000), 300000);
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(3000);
            return errorObject(QStringLiteral("Process timed out and was killed."));
        }
        return okValue(QJsonObject{
            {QStringLiteral("exitCode"), process.exitCode()},
            {QStringLiteral("stdout"), QString::fromLocal8Bit(process.readAllStandardOutput())},
            {QStringLiteral("stderr"), QString::fromLocal8Bit(process.readAllStandardError())},
        });
    }
    if (method == QStringLiteral("process/spawn")) {
        // Extreme-risk API: starts an external process without waiting for completion.
        const QString program = params.value(QStringLiteral("program")).toString().trimmed();
        if (program.isEmpty()) {
            return errorObject(QStringLiteral("Missing program."));
        }
        QStringList arguments;
        for (const QJsonValue& value : params.value(QStringLiteral("args")).toArray()) {
            arguments.append(value.toString());
        }
        qint64 pid = 0;
        const bool ok = QProcess::startDetached(program, arguments, params.value(QStringLiteral("cwd")).toString(), &pid);
        return ok ? okValue(QJsonObject{{QStringLiteral("pid"), static_cast<double>(pid)}})
                  : errorObject(QStringLiteral("Failed to spawn process."));
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
