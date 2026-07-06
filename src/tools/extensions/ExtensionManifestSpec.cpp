#include "extensions/ExtensionManifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

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

QString validManifestJson(const QString& commandId = QStringLiteral("sample.hello"))
{
    return QStringLiteral(R"({
  "id": "sample-extension",
  "name": "Sample Extension",
  "version": "0.0.1",
  "publisher": "local",
  "main": "./extension.js",
  "engines": { "miacode": ">=1.0.0" },
  "activationEvents": ["onStartupFinished"],
  "contributes": {
    "commands": [
      { "command": "%1", "title": "Hello", "category": "Sample" }
    ],
    "menus": {
      "tools/menu": [
        { "command": "%1" }
      ]
    }
  }
})").arg(commandId);
}

bool expect(bool condition, const QString& message)
{
    if (condition) {
        return true;
    }
    QTextStream(stderr) << "FAIL: " << message << "\n";
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    {
        QTemporaryDir dir;
        ok = expect(dir.isValid(), QStringLiteral("temp dir")) && ok;
        writeText(QDir(dir.path()).filePath(QStringLiteral("extension.js")), QStringLiteral("module.exports = {};"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("miacode-extension.json")), validManifestJson());
        const auto result = miacode::extensions::loadExtensionManifest(dir.path());
        ok = expect(result.ok, QStringLiteral("valid manifest should parse: %1").arg(result.error)) && ok;
        ok = expect(result.manifest.commands.size() == 1, QStringLiteral("valid manifest command count")) && ok;
        ok = expect(result.manifest.menus.size() == 1, QStringLiteral("valid manifest menu count")) && ok;
    }

    {
        QTemporaryDir dir;
        ok = expect(dir.isValid(), QStringLiteral("temp dir for language pack")) && ok;
        QDir(dir.path()).mkpath(QStringLiteral("i18n"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("i18n/ja.json")), QStringLiteral(R"({"dialog.preferences.title":"設定"})"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("miacode-extension.json")), QStringLiteral(R"({
  "id": "sample-language",
  "name": "Sample Language",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "contributes": {
    "languages": [
      { "id": "ja", "label": "日本語", "translations": "./i18n/ja.json" }
    ]
  }
})"));
        const auto result = miacode::extensions::loadExtensionManifest(dir.path());
        ok = expect(result.ok, QStringLiteral("pure language pack should parse: %1").arg(result.error)) && ok;
        ok = expect(result.manifest.main.isEmpty(), QStringLiteral("pure language pack has no main")) && ok;
        ok = expect(result.manifest.languages.size() == 1, QStringLiteral("language contribution count")) && ok;
        ok = expect(!result.manifest.needsExtensionHost(), QStringLiteral("pure language pack does not need host")) && ok;
    }

    {
        QTemporaryDir dir;
        writeText(QDir(dir.path()).filePath(QStringLiteral("extension.js")), QStringLiteral("module.exports = {};"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("miacode-extension.json")), QStringLiteral("{}"));
        const auto result = miacode::extensions::loadExtensionManifest(dir.path());
        ok = expect(!result.ok && result.error.contains(QStringLiteral("id")), QStringLiteral("missing fields fail")) && ok;
    }

    {
        QTemporaryDir dir;
        writeText(QDir(dir.path()).filePath(QStringLiteral("extension.js")), QStringLiteral("module.exports = {};"));
        QString json = validManifestJson();
        json.replace(
            QStringLiteral(R"({ "command": "sample.hello", "title": "Hello", "category": "Sample" })"),
            QStringLiteral(R"({ "command": "sample.hello", "title": "Hello" }, { "command": "sample.hello", "title": "Again" })"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("miacode-extension.json")), json);
        const auto result = miacode::extensions::loadExtensionManifest(dir.path());
        ok = expect(!result.ok && result.error.contains(QStringLiteral("Duplicate command")), QStringLiteral("duplicate commands fail")) && ok;
    }

    {
        QTemporaryDir dir;
        writeText(QDir(dir.path()).filePath(QStringLiteral("extension.js")), QStringLiteral("module.exports = {};"));
        QString json = validManifestJson();
        json.replace(QStringLiteral(R"({ "command": "sample.hello" })"), QStringLiteral(R"({ "command": "sample.missing" })"));
        writeText(QDir(dir.path()).filePath(QStringLiteral("miacode-extension.json")), json);
        const auto result = miacode::extensions::loadExtensionManifest(dir.path());
        ok = expect(!result.ok && result.error.contains(QStringLiteral("unknown command")), QStringLiteral("unknown menu command fails")) && ok;
    }

    return ok ? 0 : 1;
}
