#include "DebugLog.h"

#include "DebugOptions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <mach-o/dyld.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace miacode::debug_log {
namespace {

QMutex& logMutex()
{
    static QMutex mutex;
    return mutex;
}

QMutex& projectLogDirectoryMutex()
{
    static QMutex mutex;
    return mutex;
}

QString& sessionProjectLogDirectoryStorage()
{
    static QString path;
    return path;
}

bool channelEnabled(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return miacode::debug_options::runtimeDebugOutputEnabled();
    case Channel::Audio:
        return miacode::debug_options::audioDebugOutputEnabled();
    case Channel::Export:
        return miacode::debug_options::exportDebugOutputEnabled();
    case Channel::StartupTiming:
        return miacode::debug_options::startupTimingEnabled();
    case Channel::PreviewProfile:
        return miacode::debug_options::previewProfileOutputEnabled();
    case Channel::Fatal:
        return true;
    }
    return false;
}

QString channelLabel(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return QStringLiteral("runtime");
    case Channel::Audio:
        return QStringLiteral("audio");
    case Channel::Export:
        return QStringLiteral("export");
    case Channel::StartupTiming:
        return QStringLiteral("startup");
    case Channel::Fatal:
        return QStringLiteral("fatal");
    case Channel::PreviewProfile:
        return QStringLiteral("preview_profile");
    }
    return QStringLiteral("unknown");
}

QString channelFileName(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return QStringLiteral("miacode_runtime_debug.log");
    case Channel::Audio:
        return QStringLiteral("miacode_audio_debug.log");
    case Channel::Export:
        return QStringLiteral("miacode_video_export.log");
    case Channel::StartupTiming:
        return QStringLiteral("miacode_startup_timing.log");
    case Channel::Fatal:
        return QStringLiteral("miacode_fatal.log");
    case Channel::PreviewProfile:
        return QStringLiteral("miacode_preview_profile_summary.txt");
    }
    return QStringLiteral("miacode_debug.log");
}

QString channelPathOverride(Channel channel)
{
    switch (channel) {
    case Channel::Runtime:
        return qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH").trimmed();
    case Channel::Audio:
        return qEnvironmentVariable("MIACODE_AUDIO_LOG_PATH").trimmed();
    case Channel::Export:
        return qEnvironmentVariable("MIACODE_EXPORT_LOG_PATH").trimmed();
    case Channel::StartupTiming:
        return qEnvironmentVariable("MIACODE_STARTUP_LOG_PATH").trimmed();
    case Channel::Fatal:
        return qEnvironmentVariable("MIACODE_FATAL_LOG_PATH").trimmed();
    case Channel::PreviewProfile:
        return qEnvironmentVariable("MIACODE_PREVIEW_PROFILE_PATH").trimmed();
    }
    return QString();
}

QString resolvedOverridePath(Channel channel, const QString& overridePath)
{
    const QString trimmed = overridePath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const bool looksLikeDirectory = trimmed.endsWith(QLatin1Char('/')) || trimmed.endsWith(QLatin1Char('\\'));
    const QString cleanPath = QDir::cleanPath(trimmed);
    const QFileInfo info(cleanPath);
    const bool looksLikeDirectoryName = !info.exists()
        && info.suffix().isEmpty()
        && !info.fileName().contains(QLatin1Char('.'));
    if (looksLikeDirectory || (info.exists() && info.isDir()) || looksLikeDirectoryName) {
        return QDir(cleanPath).filePath(channelFileName(channel));
    }
    return cleanPath;
}

bool shouldWrite(Channel channel, bool force)
{
    return force || channelEnabled(channel);
}

void ensureParentDirectory(const QString& path)
{
    const QFileInfo info(path);
    const QString dirPath = info.absolutePath();
    if (!dirPath.isEmpty()) {
        QDir().mkpath(dirPath);
    }
}

QString executableDirectoryPath()
{
    if (QCoreApplication::instance() != nullptr) {
        const QString appDir = QCoreApplication::applicationDirPath().trimmed();
        if (!appDir.isEmpty()) {
            return QDir::cleanPath(appDir);
        }
    }

#ifdef Q_OS_WIN
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD pathLength = ::GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (pathLength > 0 && pathLength < MAX_PATH) {
        return QFileInfo(QString::fromWCharArray(modulePath, pathLength)).absolutePath();
    }
#elif defined(Q_OS_MAC)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) == -1 && size > 0) {
        QByteArray buffer(static_cast<int>(size), '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            return QFileInfo(QString::fromLocal8Bit(buffer.constData())).absolutePath();
        }
    }
#elif defined(Q_OS_UNIX)
    QByteArray buffer(4096, '\0');
    const ssize_t pathLength = ::readlink("/proc/self/exe", buffer.data(), static_cast<size_t>(buffer.size() - 1));
    if (pathLength > 0) {
        buffer[static_cast<int>(pathLength)] = '\0';
        return QFileInfo(QString::fromLocal8Bit(buffer.constData())).absolutePath();
    }
#endif

    return QString();
}

QString defaultDebugLogDirectory()
{
    const QString executableDir = executableDirectoryPath();
    if (executableDir.isEmpty()) {
        return QString();
    }
    return QDir(executableDir).filePath(QStringLiteral("logs"));
}

qint64 startupTrimMaxBytes()
{
    return 100 * 1024;
}

bool trimFileToMaxBytesLocked(const QString& path, qint64 maxBytes)
{
    if (maxBytes <= 0) {
        return true;
    }

    QFile file(path);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();
    if (data.size() <= maxBytes) {
        return true;
    }

    int start = qMax(0, data.size() - static_cast<int>(maxBytes));
    while (start < data.size() && start > 0 && data.at(start - 1) != '\n') {
        ++start;
    }
    if (start >= data.size()) {
        start = qMax(0, data.size() - static_cast<int>(maxBytes));
    }
    QByteArray trimmed = data.mid(start);
    if (trimmed.size() > maxBytes) {
        trimmed = trimmed.right(static_cast<int>(maxBytes));
    }

    ensureParentDirectory(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 written = file.write(trimmed);
    file.close();
    return written == trimmed.size();
}

void trimDebugLogsInCurrentDirectoryLocked()
{
    const qint64 maxBytes = startupTrimMaxBytes();
    for (const Channel channel : {
             Channel::Runtime,
             Channel::Audio,
             Channel::Export,
             Channel::StartupTiming,
             Channel::Fatal}) {
        (void)trimFileToMaxBytesLocked(logPath(channel), maxBytes);
    }
}

}  // namespace

QString timestampString()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

QString logDirectory()
{
    const QString envDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
    if (!envDir.isEmpty()) {
        return QDir::cleanPath(envDir);
    }

    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        const QString projectDir = sessionProjectLogDirectoryStorage().trimmed();
        if (!projectDir.isEmpty()) {
            return QDir::cleanPath(projectDir);
        }
    }

    if (miacode::debug_options::debugModeEnabled()) {
        const QString appDebugDir = defaultDebugLogDirectory();
        if (!appDebugDir.isEmpty()) {
            return QDir::cleanPath(appDebugDir);
        }
    }

    return QDir::tempPath();
}

void setSessionProjectLogDirectory(const QString& directoryPath)
{
    {
        QMutexLocker locker(&projectLogDirectoryMutex());
        sessionProjectLogDirectoryStorage() = directoryPath.trimmed().isEmpty()
            ? QString()
            : QDir::cleanPath(directoryPath);
    }
    if (miacode::debug_options::debugModeEnabled()) {
        trimDebugLogsInCurrentDirectoryLocked();
    }
}

QString logPath(Channel channel)
{
    const QString overridePath = resolvedOverridePath(channel, channelPathOverride(channel));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    return QDir(logDirectory()).filePath(channelFileName(channel));
}

QString runtimeLogPath()
{
    return logPath(Channel::Runtime);
}

QString audioLogPath()
{
    return logPath(Channel::Audio);
}

QString exportLogPath()
{
    return logPath(Channel::Export);
}

QString startupTimingLogPath()
{
    return logPath(Channel::StartupTiming);
}

QString fatalLogPath()
{
    return logPath(Channel::Fatal);
}

QString previewProfileSummaryPath()
{
    return logPath(Channel::PreviewProfile);
}

QString formatTitleLine(const QString& title)
{
    return QStringLiteral("[%1] %2").arg(timestampString(), title);
}

bool clearChannel(Channel channel)
{
    QMutexLocker locker(&logMutex());
    QFile::remove(logPath(channel));
    return true;
}

void clearDebugSessionLogs()
{
    clearChannel(Channel::Runtime);
    clearChannel(Channel::Audio);
    clearChannel(Channel::Export);
    clearChannel(Channel::PreviewProfile);
}

void trimDebugSessionLogsForStartup()
{
    if (!miacode::debug_options::debugModeEnabled()) {
        return;
    }
    QMutexLocker locker(&logMutex());
    trimDebugLogsInCurrentDirectoryLocked();
}

bool resetChannel(Channel channel, const QStringList& initialLines, bool force)
{
    if (!shouldWrite(channel, force)) {
        return false;
    }
    QMutexLocker locker(&logMutex());
    const QString path = logPath(channel);
    ensureParentDirectory(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    for (const QString& line : initialLines) {
        stream << line;
        if (!line.endsWith(QLatin1Char('\n'))) {
            stream << '\n';
        }
    }
    return true;
}

bool appendText(Channel channel, const QString& text, bool force)
{
    if (!shouldWrite(channel, force)) {
        return false;
    }
    QMutexLocker locker(&logMutex());
    const QString path = logPath(channel);
    ensureParentDirectory(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    if (!text.endsWith(QLatin1Char('\n'))) {
        stream << '\n';
    }
    stream.flush();
    file.close();
    if (miacode::debug_options::debugModeEnabled()) {
        trimFileToMaxBytesLocked(path, startupTrimMaxBytes());
    }
    return true;
}

bool appendLine(Channel channel, const QString& scope, const QString& payload, bool force)
{
    QString bracket = channelLabel(channel);
    if (!scope.trimmed().isEmpty()) {
        bracket += QLatin1Char('/') + scope.trimmed();
    }
    QString text = QStringLiteral("%1 [%2]").arg(timestampString(), bracket);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload;
    }
    return appendText(channel, text, force);
}

bool appendTimingLine(
    Channel channel,
    const QString& scope,
    const QString& step,
    qint64 elapsedMs,
    const QString& detail,
    bool force)
{
    QString payload = QStringLiteral("step=%1 elapsed_ms=%2")
                          .arg(step.trimmed().isEmpty() ? QStringLiteral("unknown") : step.trimmed())
                          .arg(elapsedMs);
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" ") + detail.trimmed();
    }
    return appendLine(channel, scope, payload, force);
}

bool initializeStartupTimingLogSession()
{
    if (!miacode::debug_options::startupTimingEnabled()) {
        return false;
    }
    return resetChannel(
        Channel::StartupTiming,
        {
            QStringLiteral("%1 [startup/session] pid=%2 log_path=%3")
                .arg(timestampString())
                .arg(QCoreApplication::applicationPid())
                .arg(startupTimingLogPath())
        }
    );
}

bool appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    if (!miacode::debug_options::startupTimingEnabled()) {
        return false;
    }
    return appendLine(
        Channel::StartupTiming,
        QStringLiteral("stage"),
        QStringLiteral("stage=%1 elapsed_ms=%2 delta_ms=%3").arg(stage).arg(elapsedMs).arg(deltaMs)
    );
}

bool appendFatalMessage(const QString& scope, const QString& payload)
{
    return appendLine(Channel::Fatal, scope, payload, true);
}

}  // namespace miacode::debug_log
