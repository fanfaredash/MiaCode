#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace miacode::extensions {

struct ExtensionCommandContribution {
    QString id;
    QString title;
    QString category;
};

struct ExtensionMenuContribution {
    QString location;
    QString command;
};

struct ExtensionLanguageContribution {
    QString id;
    QString label;
    QString translations;
};

struct ExtensionManifest {
    QString id;
    QString name;
    QString version;
    QString publisher;
    QString main;
    QString engineMiaCode;
    QStringList activationEvents;
    QStringList permissions;
    QVector<ExtensionCommandContribution> commands;
    QVector<ExtensionMenuContribution> menus;
    QVector<ExtensionLanguageContribution> languages;
    QString rootPath;
    QString manifestPath;

    QString qualifiedId() const;
    bool needsExtensionHost() const;
    QJsonObject toHostJson() const;
};

struct ExtensionManifestParseResult {
    bool ok = false;
    ExtensionManifest manifest;
    QString error;
};

ExtensionManifestParseResult loadExtensionManifest(const QString& extensionRootPath);
QStringList defaultExtensionSearchPaths();
QString userExtensionDirectoryPath();
bool isValidExtensionIdentifier(const QString& value);

}  // namespace miacode::extensions
