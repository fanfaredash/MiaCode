#include "ExtensionManager.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>

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
    restartHost();
    rebuildFilesystemWatchers();
}

void ExtensionManager::restartHost()
{
    shutdown();
    const QJsonArray hostManifests = manifestsForHost();
    if (hostManifests.isEmpty()) {
        return;
    }
    host_ = std::make_unique<ExtensionHostProcess>(this);
    host_->setHostRequestHandler([this](const QString& method, const QJsonObject& params) {
        return handleHostRequest(method, params);
    });
    connect(host_.get(), &ExtensionHostProcess::hostLogMessage, this, [this](const QString& message) {
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });
    connect(host_.get(), &ExtensionHostProcess::hostErrorMessage, this, [this](const QString& message) {
        diagnostics_.append(message);
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(message));
        }
    });

    QString error;
    if (!host_->start(extensionHostScriptPath(), hostManifests, &error)) {
        diagnostics_.append(error);
        if (callbacks_.logMessage) {
            callbacks_.logMessage(QStringLiteral("extensions: %1").arg(error));
        }
    }
}

void ExtensionManager::shutdown()
{
    if (host_) {
        host_->stop();
        host_.reset();
    }
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

void ExtensionManager::refreshExtensions()
{
    discoverExtensions();
    rebuildMenuContributions();
    UiText::reloadExtensionLanguagePacks();
    const bool resetLanguage = UiText::ensurePreferredLanguageAvailable();
    restartHost();
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
                    extensionsMenu_ = toolsMenu_->addMenu(QStringLiteral("Extensions"));
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
        extensionsMenu_ = toolsMenu_->addMenu(QStringLiteral("Extensions"));
    }
    if (extensionsMenu_ != nullptr && !addedToolsCommand) {
        QAction* emptyAction = extensionsMenu_->addAction(QStringLiteral("No extension commands"));
        emptyAction->setEnabled(false);
    }
}

QJsonArray ExtensionManager::manifestsForHost() const
{
    QJsonArray array;
    for (const ExtensionManifest& manifest : manifests_) {
        if (!manifest.needsExtensionHost()) {
            continue;
        }
        QJsonObject object = manifest.toHostJson();
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

QString ExtensionManager::extensionHostScriptPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString deployed = appDir.filePath(QStringLiteral("extensions/extensionHost.js"));
    if (QFileInfo::exists(deployed)) {
        return deployed;
    }
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../resources/extensions/extensionHost.js"));
}

QJsonObject ExtensionManager::handleHostRequest(const QString& method, const QJsonObject& params)
{
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
    if (method == QStringLiteral("window/openPreferences")) {
        if (callbacks_.openPreferences) {
            callbacks_.openPreferences();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Preferences are unavailable.")}};
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
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unknown host method: %1").arg(method)}};
}

void ExtensionManager::invokeCommand(const QString& command)
{
    if (host_ == nullptr || !host_->isRunning()) {
        if (callbacks_.showMessage) {
            callbacks_.showMessage(QStringLiteral("warning"), QStringLiteral("Extension host is not running."));
        }
        return;
    }
    host_->sendRequest(QStringLiteral("commands/execute"), QJsonObject{{QStringLiteral("command"), command}});
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
