#pragma once

#include <functional>

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSEngine>
#include <QStringList>
#include <QTimer>
#include <QVector>

namespace miacode::extensions {

class EmbeddedExtensionRuntime : public QObject {
    Q_OBJECT

public:
    using HostRequestHandler = std::function<QJsonObject(const QString& method, const QJsonObject& params)>;

    explicit EmbeddedExtensionRuntime(QObject* parent = nullptr);
    ~EmbeddedExtensionRuntime() override;

    void setHostRequestHandler(HostRequestHandler handler);
    bool start(const QJsonArray& extensions, QString* errorMessage = nullptr);
    void stop();
    bool isRunning() const;
    bool executeCommand(const QString& command, QString* errorMessage = nullptr);
    int registeredEventCallbackCount(const QString& kind = {}) const;
    QJsonArray registeredEventCallbacksForDevtools() const;
    void dispatchEvent(const QString& kind, const QJsonObject& payload, bool coalescible = false);

signals:
    void runtimeLogMessage(const QString& message);
    void runtimeErrorMessage(const QString& message);
    void extensionCommandRegistered(const QString& command);

private:
    class BridgeObject;
    struct EventCallback {
        quint64 id = 0;
        QString extensionId;
        QString pattern;
        QJsonObject filter;
        QJSValue callback;
        quint64 received = 0;
        quint64 delivered = 0;
        quint64 coalesced = 0;
        quint64 dropped = 0;
        quint64 errors = 0;
        int consecutiveSlowCallbacks = 0;
        int peakQueueDepth = 0;
        qint64 lastDurationNs = 0;
        qint64 totalDurationNs = 0;
        bool suspended = false;
    };
    struct PendingEvent {
        quint64 subscriptionId = 0;
        QString kind;
        QJsonObject event;
        bool coalescible = false;
    };

    QJsonObject requestHost(const QString& method, const QJsonObject& params = {});
    QJsonObject scriptValueToJson(const QJSValue& value);
    QJSValue jsonToScriptValue(const QJsonObject& object);
    QJSValue disposableValue(quint64 subscriptionId = 0);
    void registerCommand(const QString& command, const QJSValue& callback);
    quint64 registerEventCallback(const QString& pattern, const QJsonObject& options, const QJSValue& callback);
    void unregisterEventCallback(quint64 id);
    void flushPendingEvents();
    bool activateExtension(const QJsonObject& extension, QString* errorMessage);
    void deactivateExtensions();
    QString describeError(const QJSValue& value) const;

    QJSEngine engine_;
    HostRequestHandler hostRequestHandler_;
    QHash<QString, QJSValue> commandCallbacks_;
    QHash<QString, QJsonObject> extensionById_;
    QHash<QString, QString> commandOwnerById_;
    QVector<EventCallback> eventCallbacks_;
    QVector<PendingEvent> pendingEvents_;
    QTimer eventFlushTimer_;
    quint64 nextEventSubscriptionId_ = 1;
    quint64 nextEventSequence_ = 1;
    QHash<QString, QJSValue> loadedExports_;
    QStringList loadedExtensionIds_;
    QString currentExtensionId_;
    QString currentCallExtensionId_;
    bool running_ = false;
};

}  // namespace miacode::extensions
