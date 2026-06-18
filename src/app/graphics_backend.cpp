#include "MainEntrypoints.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#include <QStringList>

namespace miacode::app::entry {

// ===== Graphics backend selector =====
//
// User-driven backend selection without driver probing (which would touch GPU vendor DLLs
// and trip Windows Defender heuristics). Strategy:
//   1. CLI flag `--rhi=<name>` overrides everything for this run AND persists the choice
//      so the same backend is used on the next launch.
//   2. With no flag, the persisted choice from a small JSON file beside the executable is
//      applied. If neither exists, we leave Qt on its platform default (D3D11 on Windows).
//   3. The persistence file is plain JSON, written only on explicit user choice — the
//      app never tries backends behind the user's back. If a chosen backend fails to
//      initialise the user can recover by relaunching with `--rhi=auto` (clears the file)
//      or `--rhi=d3d11` (forces the safe Windows default).
//
// Recognised values: "auto" / "default" (no override), "d3d11", "d3d12", "opengl",
// "vulkan", "metal", "software". Anything else is rejected and we fall through to auto.

QString persistedGraphicsBackendFilePath()
{
    // Beside the executable so the file is portable with the install. Hidden (leading dot)
    // to keep the directory uncluttered.
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(QStringLiteral(".miacode_graphics.json"));
}

QString readPersistedGraphicsBackend()
{
    const QString path = persistedGraphicsBackendFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("backend")).toString().trimmed().toLower();
}

bool writePersistedGraphicsBackend(const QString& backend)
{
    const QString path = persistedGraphicsBackendFilePath();
    if (backend.isEmpty()) {
        // "auto" / clear: just remove the file.
        QFile::remove(path);
        return true;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("backend"), backend);
    obj.insert(QStringLiteral("schema"), QStringLiteral("miacode_graphics_v1"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

QString canonicalRhiName(const QString& raw)
{
    const QString name = raw.trimmed().toLower();
    if (name == QStringLiteral("auto") || name == QStringLiteral("default")
        || name == QStringLiteral("platform") || name == QStringLiteral("none")) {
        return QString();
    }
    if (name == QStringLiteral("d3d11") || name == QStringLiteral("direct3d11")
        || name == QStringLiteral("dx11")) {
        return QStringLiteral("d3d11");
    }
    if (name == QStringLiteral("d3d12") || name == QStringLiteral("direct3d12")
        || name == QStringLiteral("dx12")) {
        return QStringLiteral("d3d12");
    }
    if (name == QStringLiteral("opengl") || name == QStringLiteral("gl")) {
        return QStringLiteral("opengl");
    }
    if (name == QStringLiteral("vulkan") || name == QStringLiteral("vk")) {
        return QStringLiteral("vulkan");
    }
    if (name == QStringLiteral("metal")) {
        return QStringLiteral("metal");
    }
    if (name == QStringLiteral("software") || name == QStringLiteral("sw")
        || name == QStringLiteral("null")) {
        return QStringLiteral("software");
    }
    return QString();  // unknown — caller treats as "no override".
}

// Pulls "--rhi=<value>" or "--rhi <value>" out of the raw argv. We do NOT use
// QCommandLineParser here because it requires a constructed QApplication, and we want to
// pick the backend before that.
QString parseRhiCommandLineArg(const QStringList& args)
{
    static const QString kFlag = QStringLiteral("--rhi");
    static const QString kFlagEq = QStringLiteral("--rhi=");
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a.startsWith(kFlagEq)) {
            return a.mid(kFlagEq.size());
        }
        if (a == kFlag && (i + 1) < args.size()) {
            return args.at(i + 1);
        }
    }
    return QString();
}

GraphicsBackendChoice resolveGraphicsBackendChoice(const QStringList& args)
{
    GraphicsBackendChoice choice{};
    const QString cliRaw = parseRhiCommandLineArg(args);
    if (!cliRaw.isEmpty()) {
        choice.fromCommandLine = true;
        const QString canonical = canonicalRhiName(cliRaw);
        choice.name = canonical;
        choice.clearedByCommand = canonical.isEmpty()
            && (cliRaw.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0);
        return choice;
    }
    choice.name = readPersistedGraphicsBackend();
    return choice;
}

// Apply the chosen backend to QQuickWindow. Must be called AFTER QApplication construction
// (because setGraphicsApi consults Qt's per-process state) but BEFORE any QQuickWindow
// is realised. Returns the canonical name actually applied (empty for "auto/default").
QString applyGraphicsBackendChoice(const QString& backend)
{
    if (backend == QStringLiteral("d3d11")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    } else if (backend == QStringLiteral("d3d12")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
    } else if (backend == QStringLiteral("opengl")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    } else if (backend == QStringLiteral("vulkan")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    } else if (backend == QStringLiteral("metal")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    } else if (backend == QStringLiteral("software")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    } else {
        return QString();
    }
    return backend;
}

}  // namespace miacode::app::entry
