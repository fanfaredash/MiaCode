#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "EmbeddedExtensionRuntime.h"
#include "ExtensionManifest.h"

class QAction;
class QFileSystemWatcher;
class QMenu;
class QMenuBar;

namespace miacode::extensions {

struct ExtensionDocumentSnapshot {
    QString uri;
    QString languageId = QStringLiteral("simai");
    QString text;
    int activeDifficultyId = 0;
    bool dirty = false;
};

struct ExtensionHostCallbacks {
    std::function<ExtensionDocumentSnapshot()> activeDocument;
    std::function<bool(const QString& text, QString* error)> replaceActiveDocumentText;
    std::function<bool()> validateActiveDocument;
    std::function<QJsonObject(const QString& method, const QJsonObject& params)> mainWindowRequest;
    std::function<void(const QString& severity, const QString& message)> showMessage;
    std::function<void(const QString& message)> logMessage;
};

struct ExtensionRecord {
    ExtensionManifest manifest;
    bool valid = false;
    bool enabled = true;
    QString sourcePath;
    QString diagnostic;
};

class ExtensionManager : public QObject {
    Q_OBJECT

public:
    explicit ExtensionManager(QObject* parent = nullptr);
    ~ExtensionManager() override;

    void setCallbacks(ExtensionHostCallbacks callbacks);
    void initialize(
        QMenuBar* menuBar,
        QMenu* fileMenu,
        QMenu* editMenu,
        QMenu* toolsMenu,
        QMenu* modifyMenu,
        QMenu* previewMenu,
        QMenu* helpMenu);
    void setToolboxMenu(QMenu* toolboxMenu, QAction* insertBeforeAction = nullptr);
    void shutdown();
    const QVector<ExtensionManifest>& manifests() const;
    const QVector<ExtensionRecord>& records() const;
    QStringList diagnostics() const;
    QString userExtensionsDirectory() const;
    QString extensionLogDirectory() const;
    QJsonObject devtoolsSnapshotForUi() const;
    void publishEvent(const QString& name, const QJsonObject& payload = {}, bool coalescible = false);
    // Cheap pre-check for publishers on a hot path: skip building the payload
    // at all when no running extension subscribes to `name`.
    bool hasEventSubscribers(const QString& name) const;
    void refreshExtensions();
    void refreshMenuSelectionIcons();
    void setExtensionEnabled(const QString& qualifiedId, bool enabled);
    bool executeExtensionCommand(const QString& command, QString* error = nullptr);

private:
    void discoverExtensions();
    void rebuildMenuContributions();
    void restartRuntime();
    void rebuildFilesystemWatchers();
    void scheduleRefresh();
    QJsonArray manifestsForRuntime() const;
    QJsonObject handleHostRequest(const QString& method, const QJsonObject& params);
    QJsonObject handleHostRequestCore(const QString& method, const QJsonObject& params);
    QJsonObject devtoolsSnapshot(const QString& extensionId) const;
    QJsonObject devtoolsDiagnose(const QString& extensionId, const QJsonObject& params) const;
    void appendExtensionLog(const QString& severity, const QString& message, const QJsonObject& details = {}) const;
    void appendDevtoolsCall(const QString& method, const QJsonObject& params, const QJsonObject& result, qint64 elapsedMs);
    void dispatchRuntimeEventForHostResult(const QString& method, const QJsonObject& params, const QJsonObject& result);
    bool ensurePermission(const QString& extensionId, const QString& method, const QJsonObject& params, QJsonObject* errorResponse);
    bool manifestDeclaresPermission(const QString& extensionId, const QString& permission) const;
    QString permissionForMethod(const QString& method, const QJsonObject& params = {}) const;
    QString extensionRootPathForId(const QString& extensionId) const;
    void invokeCommand(const QString& command);
    ExtensionCommandContribution commandContribution(const QString& command) const;

    QVector<ExtensionRecord> records_;
    QVector<ExtensionManifest> manifests_;
    QStringList diagnostics_;
    QHash<QString, QAction*> commandActions_;
    QHash<QString, QString> commandOwnerById_;
    QHash<QString, QString> languageOwnerById_;
    QPointer<QMenuBar> menuBar_;
    QPointer<QMenu> fileMenu_;
    QPointer<QMenu> editMenu_;
    QPointer<QMenu> toolsMenu_;
    QPointer<QMenu> modifyMenu_;
    QPointer<QMenu> previewMenu_;
    QPointer<QMenu> helpMenu_;
    QPointer<QMenu> toolboxMenu_;
    QPointer<QAction> toolboxInsertBeforeAction_;
    QPointer<QMenu> extensionsMenu_;
    QList<QPointer<QAction>> menuContributionActions_;
    QList<QPointer<QAction>> topLevelMenuActions_;
    QJsonArray recentHostCalls_;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer refreshDebounce_;
    std::unique_ptr<EmbeddedExtensionRuntime> runtime_;
    ExtensionHostCallbacks callbacks_;
};

}  // namespace miacode::extensions
