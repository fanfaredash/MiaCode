#include "extensions/ExtensionOpenBridge.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
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

bool hasMethodNamed(const QJsonArray& methods, const QString& name)
{
    for (const QJsonValue& value : methods) {
        if (value.toObject().value(QStringLiteral("name")).toString() == name) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    bool ok = true;
    const QVector<miacode::extensions::ExtensionOpenBridgeObject> objects = miacode::extensions::extensionOpenBridgeObjects();
    ok = expect(!objects.isEmpty(), QStringLiteral("Open Bridge object registry should not be empty")) && ok;

    QSet<QString> objectIds;
    int methodCount = 0;
    for (const miacode::extensions::ExtensionOpenBridgeObject& object : objects) {
        ok = expect(!object.id.trimmed().isEmpty(), QStringLiteral("Open Bridge object id is required")) && ok;
        ok = expect(!object.permission.trimmed().isEmpty(), QStringLiteral("Open Bridge object permission is required: %1").arg(object.id)) && ok;
        ok = expect(!objectIds.contains(object.id), QStringLiteral("Open Bridge object id is duplicated: %1").arg(object.id)) && ok;
        objectIds.insert(object.id);

        const QJsonObject describedObject = miacode::extensions::extensionOpenBridgeDescribeObject(object.id);
        ok = expect(describedObject.value(QStringLiteral("id")).toString() == object.id,
                    QStringLiteral("describeObject should round-trip object id: %1").arg(object.id)) &&
             ok;
        const bool experimentalRaw = object.stability == QStringLiteral("experimentalRaw");
        ok = expect(describedObject.value(QStringLiteral("experimentalRaw")).toBool(false) == experimentalRaw,
                    QStringLiteral("Open Bridge object should report experimental raw state: %1").arg(object.id)) &&
             ok;
        ok = expect(describedObject.value(QStringLiteral("rawCppObjectsExposed")).toBool(false) == experimentalRaw,
                    QStringLiteral("Open Bridge object raw exposure marker should match stability: %1").arg(object.id)) &&
             ok;

        QSet<QString> methodNames;
        const QJsonArray describedMethods = describedObject.value(QStringLiteral("methods")).toArray();
        for (const miacode::extensions::ExtensionOpenBridgeMethod& method : object.methods) {
            ++methodCount;
            ok = expect(!method.name.trimmed().isEmpty(), QStringLiteral("Open Bridge method name is required: %1").arg(object.id)) && ok;
            ok = expect(!method.permission.trimmed().isEmpty(),
                        QStringLiteral("Open Bridge method permission is required: %1.%2").arg(object.id, method.name)) &&
                 ok;
            ok = expect(method.status == QStringLiteral("implemented")
                            || method.status == QStringLiteral("planned")
                            || method.status == QStringLiteral("blocked"),
                        QStringLiteral("Open Bridge method has unsupported status: %1.%2=%3").arg(object.id, method.name, method.status)) &&
                 ok;
            ok = expect(!methodNames.contains(method.name),
                        QStringLiteral("Open Bridge method is duplicated: %1.%2").arg(object.id, method.name)) &&
                 ok;
            methodNames.insert(method.name);

            const bool hasRoute = !method.hostMethod.isEmpty() || !method.command.isEmpty();
            if (method.status == QStringLiteral("implemented")) {
                ok = expect(hasRoute, QStringLiteral("implemented Open Bridge method must have a route: %1.%2").arg(object.id, method.name)) && ok;
            }
            ok = expect(!(method.hostMethod.isEmpty() == false && method.command.isEmpty() == false),
                        QStringLiteral("Open Bridge method must not have both hostMethod and command: %1.%2").arg(object.id, method.name)) &&
                 ok;

            const QJsonObject describedMethod = miacode::extensions::extensionOpenBridgeDescribeMethod(object.id, method.name);
            ok = expect(describedMethod.value(QStringLiteral("name")).toString() == method.name,
                        QStringLiteral("describeMethod should round-trip method name: %1.%2").arg(object.id, method.name)) &&
                 ok;
            ok = expect(describedMethod.value(QStringLiteral("status")).toString() == method.status,
                        QStringLiteral("describeMethod should preserve method status: %1.%2").arg(object.id, method.name)) &&
                 ok;
            ok = expect(hasMethodNamed(describedMethods, method.name),
                        QStringLiteral("describeObject should include described method: %1.%2").arg(object.id, method.name)) &&
                 ok;
        }
    }

    ok = expect(methodCount >= 1, QStringLiteral("Open Bridge registry should contain methods")) && ok;
    ok = expect(objectIds.contains(QStringLiteral("ui")), QStringLiteral("Open Bridge should expose the ui facade object")) && ok;
    ok = expect(!miacode::extensions::extensionOpenBridgeDescribeMethod(QStringLiteral("ui"), QStringLiteral("registerPetOverlay")).isEmpty(),
                QStringLiteral("Open Bridge should describe ui.registerPetOverlay")) &&
         ok;

    const QVector<miacode::extensions::ExtensionForbiddenOpenTarget> rawTargets = miacode::extensions::extensionForbiddenOpenTargets();
    ok = expect(!rawTargets.isEmpty(), QStringLiteral("Open Bridge experimental raw target registry should not be empty")) && ok;
    for (const miacode::extensions::ExtensionForbiddenOpenTarget& target : rawTargets) {
        ok = expect(objectIds.contains(target.id),
                    QStringLiteral("experimental raw target must be exposed as an Open Bridge object id: %1").arg(target.id)) &&
             ok;
        const QJsonObject descriptor = miacode::extensions::extensionDescribeForbiddenOpenTarget(target.id);
        ok = expect(descriptor.value(QStringLiteral("forbidden")).toBool(true) == false,
                    QStringLiteral("experimental raw target should not be marked forbidden: %1").arg(target.id)) &&
             ok;
        ok = expect(descriptor.value(QStringLiteral("experimentalRaw")).toBool(false),
                    QStringLiteral("experimental raw target should be marked experimentalRaw: %1").arg(target.id)) &&
             ok;
        ok = expect(!miacode::extensions::extensionIsForbiddenOpenTarget(target.id),
                    QStringLiteral("experimental raw target should not be blocked by forbidden check: %1").arg(target.id)) &&
             ok;
    }

    return ok ? 0 : 1;
}
