#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "ExtensionHostProcess.h"
#include "ExtensionManifest.h"

class QAction;
class QFileSystemWatcher;
class QMenu;

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
    void initialize(QMenu* toolsMenu);
    void shutdown();
    const QVector<ExtensionManifest>& manifests() const;
    const QVector<ExtensionRecord>& records() const;
    QStringList diagnostics() const;
    QString userExtensionsDirectory() const;
    QString extensionLogDirectory() const;
    void refreshExtensions();
    void setExtensionEnabled(const QString& qualifiedId, bool enabled);

private:
    void discoverExtensions();
    void rebuildMenuContributions(QMenu* toolsMenu);
    void restartHost();
    void rebuildFilesystemWatchers();
    void scheduleRefresh();
    QJsonArray manifestsForHost() const;
    QString extensionHostScriptPath() const;
    QJsonObject handleHostRequest(const QString& method, const QJsonObject& params);
    void invokeCommand(const QString& command);
    ExtensionCommandContribution commandContribution(const QString& command) const;

    QVector<ExtensionRecord> records_;
    QVector<ExtensionManifest> manifests_;
    QStringList diagnostics_;
    QHash<QString, QAction*> commandActions_;
    QHash<QString, QString> commandOwnerById_;
    QHash<QString, QString> languageOwnerById_;
    QPointer<QMenu> toolsMenu_;
    QPointer<QMenu> extensionsMenu_;
    QFileSystemWatcher* watcher_ = nullptr;
    QTimer refreshDebounce_;
    std::unique_ptr<ExtensionHostProcess> host_;
    ExtensionHostCallbacks callbacks_;
};

}  // namespace miacode::extensions
