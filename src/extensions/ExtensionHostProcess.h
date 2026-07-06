#pragma once

#include <functional>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>

namespace miacode::extensions {

class ExtensionHostProcess : public QObject {
    Q_OBJECT

public:
    using HostRequestHandler = std::function<QJsonObject(const QString& method, const QJsonObject& params)>;

    explicit ExtensionHostProcess(QObject* parent = nullptr);
    ~ExtensionHostProcess() override;

    void setHostRequestHandler(HostRequestHandler handler);
    bool start(const QString& scriptPath, const QJsonArray& extensions, QString* errorMessage = nullptr);
    void stop();
    bool isRunning() const;
    void sendNotification(const QString& method, const QJsonObject& params = {});
    int sendRequest(const QString& method, const QJsonObject& params = {});

signals:
    void hostLogMessage(const QString& message);
    void hostErrorMessage(const QString& message);
    void extensionCommandRegistered(const QString& command);

private:
    void handleReadyReadStdout();
    void handleReadyReadStderr();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void handleMessage(const QJsonObject& message);
    void sendResponse(int id, const QJsonObject& result);
    void sendErrorResponse(int id, const QString& message);
    void writeMessage(const QJsonObject& message);

    QProcess process_;
    QByteArray stdoutBuffer_;
    int nextRequestId_ = 1;
    HostRequestHandler hostRequestHandler_;
};

}  // namespace miacode::extensions
