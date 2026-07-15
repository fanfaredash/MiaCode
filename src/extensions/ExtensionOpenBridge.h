#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace miacode::extensions {

struct ExtensionOpenBridgeMethod {
    QString name;
    QString hostMethod;
    QString command;
    QString permission;
    QString status;
    QString description;
};

struct ExtensionOpenBridgeObject {
    QString id;
    QString permission;
    QString stability;
    QString description;
    QVector<ExtensionOpenBridgeMethod> methods;
};

struct ExtensionForbiddenOpenTarget {
    QString id;
    QString category;
    QString reason;
};

QVector<ExtensionOpenBridgeObject> extensionOpenBridgeObjects();
QJsonArray extensionOpenBridgeObjectIds();
QJsonArray extensionOpenBridgeObjectsJson();
QJsonObject extensionOpenBridgeDescribeObject(const QString& objectId);
bool extensionOpenBridgeHasObject(const QString& objectId);
QJsonObject extensionOpenBridgeDescribeMethod(const QString& objectId, const QString& methodName);

QVector<ExtensionForbiddenOpenTarget> extensionForbiddenOpenTargets();
QJsonArray extensionForbiddenOpenTargetsJson();
QJsonObject extensionDescribeForbiddenOpenTarget(const QString& targetId);
bool extensionIsForbiddenOpenTarget(const QString& targetId);

}  // namespace miacode::extensions
