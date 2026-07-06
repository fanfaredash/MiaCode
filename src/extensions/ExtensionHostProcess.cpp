#include "ExtensionHostProcess.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace miacode::extensions {
namespace {

QString resolveNodeExecutable()
{
    const QString envNode = qEnvironmentVariable("MIACODE_NODE_PATH").trimmed();
    if (!envNode.isEmpty()) {
        return envNode;
    }
    const QString pathNode = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (!pathNode.isEmpty()) {
        return pathNode;
    }
#ifdef Q_OS_WIN
    return QStandardPaths::findExecutable(QStringLiteral("node.exe"));
#else
    return QStringLiteral("node");
#endif
}

QJsonObject makeErrorObject(const QString& message)
{
    return QJsonObject{
        {QStringLiteral("message"), message},
    };
}

}  // namespace

ExtensionHostProcess::ExtensionHostProcess(QObject* parent)
    : QObject(parent)
{
    connect(&process_, &QProcess::readyReadStandardOutput, this, &ExtensionHostProcess::handleReadyReadStdout);
    connect(&process_, &QProcess::readyReadStandardError, this, &ExtensionHostProcess::handleReadyReadStderr);
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &ExtensionHostProcess::handleProcessFinished);
}

ExtensionHostProcess::~ExtensionHostProcess()
{
    stop();
}

void ExtensionHostProcess::setHostRequestHandler(HostRequestHandler handler)
{
    hostRequestHandler_ = std::move(handler);
}

bool ExtensionHostProcess::start(const QString& scriptPath, const QJsonArray& extensions, QString* errorMessage)
{
    if (isRunning()) {
        return true;
    }
    const QString node = resolveNodeExecutable();
    if (node.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Node.js executable was not found. Set MIACODE_NODE_PATH or install node on PATH.");
        }
        return false;
    }
    if (!QFileInfo::exists(scriptPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Extension host script not found: %1").arg(scriptPath);
        }
        return false;
    }

    process_.setProgram(node);
    process_.setArguments({scriptPath});
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    process_.start();
    if (!process_.waitForStarted(3000)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to start extension host: %1").arg(process_.errorString());
        }
        return false;
    }

    sendRequest(QStringLiteral("miacode/initialize"), QJsonObject{
        {QStringLiteral("appVersion"), QCoreApplication::applicationVersion()},
        {QStringLiteral("extensions"), extensions},
    });
    return true;
}

void ExtensionHostProcess::stop()
{
    if (process_.state() == QProcess::NotRunning) {
        return;
    }
    sendNotification(QStringLiteral("miacode/shutdown"));
    process_.closeWriteChannel();
    if (!process_.waitForFinished(1500)) {
        process_.kill();
        process_.waitForFinished(1500);
    }
}

bool ExtensionHostProcess::isRunning() const
{
    return process_.state() != QProcess::NotRunning;
}

void ExtensionHostProcess::sendNotification(const QString& method, const QJsonObject& params)
{
    writeMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
}

int ExtensionHostProcess::sendRequest(const QString& method, const QJsonObject& params)
{
    const int id = nextRequestId_++;
    writeMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
    return id;
}

void ExtensionHostProcess::handleReadyReadStdout()
{
    stdoutBuffer_.append(process_.readAllStandardOutput());
    while (true) {
        const int newlineIndex = stdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray line = stdoutBuffer_.left(newlineIndex).trimmed();
        stdoutBuffer_.remove(0, newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit hostErrorMessage(QStringLiteral("Invalid extension host JSON: %1").arg(QString::fromUtf8(line)));
            continue;
        }
        handleMessage(document.object());
    }
}

void ExtensionHostProcess::handleReadyReadStderr()
{
    const QString text = QString::fromUtf8(process_.readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        emit hostLogMessage(text);
    }
}

void ExtensionHostProcess::handleProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    emit hostErrorMessage(QStringLiteral("Extension host exited: code=%1 status=%2")
                              .arg(exitCode)
                              .arg(status == QProcess::NormalExit ? QStringLiteral("normal") : QStringLiteral("crash")));
}

void ExtensionHostProcess::handleMessage(const QJsonObject& message)
{
    if (message.contains(QStringLiteral("method"))) {
        const int id = message.value(QStringLiteral("id")).toInt(-1);
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (method == QStringLiteral("commands/register")) {
            emit extensionCommandRegistered(params.value(QStringLiteral("command")).toString());
        }
        if (!hostRequestHandler_) {
            if (id >= 0) {
                sendErrorResponse(id, QStringLiteral("No host request handler is installed."));
            }
            return;
        }
        const QJsonObject result = hostRequestHandler_(method, params);
        if (id >= 0) {
            sendResponse(id, result);
        }
        return;
    }
    if (message.contains(QStringLiteral("error"))) {
        emit hostErrorMessage(message.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString());
    }
}

void ExtensionHostProcess::sendResponse(int id, const QJsonObject& result)
{
    writeMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("result"), result},
    });
}

void ExtensionHostProcess::sendErrorResponse(int id, const QString& message)
{
    writeMessage(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("error"), makeErrorObject(message)},
    });
}

void ExtensionHostProcess::writeMessage(const QJsonObject& message)
{
    if (process_.state() == QProcess::NotRunning) {
        return;
    }
    process_.write(QJsonDocument(message).toJson(QJsonDocument::Compact));
    process_.write("\n");
}

}  // namespace miacode::extensions
