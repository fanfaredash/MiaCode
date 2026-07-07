#include "ExtensionManager.h"

#include <QAction>
#include <QCoreApplication>
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
    if (method == QStringLiteral("log") || method == QStringLiteral("commands/register")) {
        return QString();
    }
    if (method.startsWith(QStringLiteral("window/show"))) {
        return QStringLiteral("ui.message");
    }
    if (method == QStringLiteral("window/showInputBox") || method == QStringLiteral("window/showQuickPick")) {
        return QStringLiteral("ui.prompt");
    }
    if (method == QStringLiteral("window/createStatusBarItem")) {
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
    if (method.startsWith(QStringLiteral("net/"))) {
        return QStringLiteral("network.fetch");
    }
    if (method == QStringLiteral("settings/get")) {
        return QStringLiteral("settings.read");
    }
    if (method == QStringLiteral("settings/set")) {
        return QStringLiteral("settings.write");
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
            {QStringLiteral("installRoot"), QDir(QCoreApplication::applicationDirPath()).dirName().compare(QStringLiteral("app"), Qt::CaseInsensitive) == 0
                ? QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."))
                : QCoreApplication::applicationDirPath()},
            {QStringLiteral("extensionsRoot"), userExtensionsDirectory()},
            {QStringLiteral("logsRoot"), extensionLogDirectory()},
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
