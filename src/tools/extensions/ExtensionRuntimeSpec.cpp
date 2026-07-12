#include "extensions/EmbeddedExtensionRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message)
{
    if (condition) {
        return true;
    }
    QTextStream(stderr) << "FAIL: " << message << "\n";
    return false;
}

bool writeText(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    QTemporaryDir dir;
    ok = expect(dir.isValid(), QStringLiteral("temp dir should be valid")) && ok;
    const QString mainPath = QDir(dir.path()).filePath(QStringLiteral("extension.js"));
    ok = expect(writeText(mainPath, QStringLiteral(R"(
function activate(context) {
  context.log("activate");
  miacode.devtools.snapshot();
  miacode.events.onDidChangeText(function(event) {
    context.log("event:" + event.kind + ":" + event.textLength);
    miacode.devtools.diagnose("preview.seek");
  });
}
module.exports = { activate: activate };
)")),
                QStringLiteral("extension.js should be writable")) &&
         ok;

    int snapshotCalls = 0;
    int diagnoseCalls = 0;
    QString diagnoseExtensionId;
    QStringList logMessages;
    QStringList runtimeErrors;
    QStringList eventRegistrations;

    miacode::extensions::EmbeddedExtensionRuntime runtime;
    QObject::connect(&runtime, &miacode::extensions::EmbeddedExtensionRuntime::runtimeErrorMessage, [&](const QString& message) {
        runtimeErrors.append(message);
    });
    runtime.setHostRequestHandler([&](const QString& method, const QJsonObject& params) {
        if (method == QStringLiteral("log")) {
            logMessages.append(params.value(QStringLiteral("message")).toString());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("events/register")) {
            eventRegistrations.append(params.value(QStringLiteral("kind")).toString());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("devtools/snapshot")) {
            ++snapshotCalls;
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("value"), QJsonObject{{QStringLiteral("seen"), true}}}};
        }
        if (method == QStringLiteral("devtools/diagnose")) {
            ++diagnoseCalls;
            diagnoseExtensionId = params.value(QStringLiteral("extensionId")).toString();
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("value"), QJsonObject{{QStringLiteral("seen"), true}}}};
        }
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unexpected method: %1").arg(method)}};
    });

    const QString extensionId = QStringLiteral("local.runtime-test");
    const QJsonObject extension{
        {QStringLiteral("qualifiedId"), extensionId},
        {QStringLiteral("main"), mainPath},
        {QStringLiteral("rootPath"), dir.path()},
        {QStringLiteral("activateOnStartup"), true},
    };

    QString error;
    ok = expect(runtime.start(QJsonArray{extension}, &error), QStringLiteral("runtime should start: %1").arg(error)) && ok;
    ok = expect(runtimeErrors.isEmpty(), QStringLiteral("runtime errors: %1").arg(runtimeErrors.join(QStringLiteral(" | ")))) && ok;
    ok = expect(snapshotCalls == 1, QStringLiteral("devtools.snapshot should be callable during activation")) && ok;
    ok = expect(runtime.registeredEventCallbackCount(QStringLiteral("events/document.onDidChangeText")) == 1,
                QStringLiteral("document change callback should be stored")) &&
         ok;
    ok = expect(eventRegistrations.contains(QStringLiteral("events/document.onDidChangeText")),
                QStringLiteral("events/register descriptor should still reach host")) &&
         ok;

    runtime.dispatchEvent(QStringLiteral("events/document.onDidChangeText"), QJsonObject{
        {QStringLiteral("textLength"), 42},
    });

    ok = expect(diagnoseCalls == 1, QStringLiteral("event callback should call devtools.diagnose")) && ok;
    ok = expect(diagnoseExtensionId == extensionId,
                QStringLiteral("event callback host calls should keep registering extension id")) &&
         ok;
    ok = expect(logMessages.join(QLatin1Char('\n')).contains(QStringLiteral("event:events/document.onDidChangeText:42")),
                QStringLiteral("event callback should receive payload")) &&
         ok;

    runtime.stop();
    ok = expect(runtime.registeredEventCallbackCount() == 0, QStringLiteral("stop should clear event callbacks")) && ok;
    return ok ? 0 : 1;
}
