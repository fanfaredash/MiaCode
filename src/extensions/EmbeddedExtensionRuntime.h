#pragma once

#include <functional>

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSEngine>
#include <QStringList>
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
    void dispatchEvent(const QString& kind, const QJsonObject& payload);

signals:
    void runtimeLogMessage(const QString& message);
    void runtimeErrorMessage(const QString& message);
    void extensionCommandRegistered(const QString& command);

private:
    class BridgeObject;
    struct EventCallback {
        QString extensionId;
        QJSValue callback;
    };

    QJsonObject requestHost(const QString& method, const QJsonObject& params = {});
    QJsonObject scriptValueToJson(const QJSValue& value);
    QJSValue jsonToScriptValue(const QJsonObject& object);
    QJSValue disposableValue();
    void registerCommand(const QString& command, const QJSValue& callback);
    void registerEventCallback(const QString& kind, const QJSValue& callback);
    bool activateExtension(const QJsonObject& extension, QString* errorMessage);
    void deactivateExtensions();
    QString describeError(const QJSValue& value) const;

    QJSEngine engine_;
    HostRequestHandler hostRequestHandler_;
    QHash<QString, QJSValue> commandCallbacks_;
    QHash<QString, QJsonObject> extensionById_;
    QHash<QString, QString> commandOwnerById_;
    QHash<QString, QVector<EventCallback>> eventCallbacksByKind_;
    QHash<QString, QJSValue> loadedExports_;
    QStringList loadedExtensionIds_;
    QString currentExtensionId_;
    QString currentCallExtensionId_;
    bool running_ = false;
};

}  // namespace miacode::extensions
