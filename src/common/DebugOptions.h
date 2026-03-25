#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

namespace miacode::debug_options {

inline bool isTruthyValue(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("1")
        || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on");
}

inline QString envValue(const char* key, const char* legacyKey = nullptr)
{
    if (legacyKey == nullptr) {
        return qEnvironmentVariable(key).trimmed();
    }
    return qEnvironmentVariable(key, qEnvironmentVariable(legacyKey)).trimmed();
}

inline bool envFlagEnabled(const char* key, const char* legacyKey = nullptr)
{
    return isTruthyValue(envValue(key, legacyKey));
}

inline bool startupTimingEnabled()
{
    return envFlagEnabled("MIACODE_ENABLE_STARTUP_TIMING", "MAIMURI_ENABLE_STARTUP_TIMING");
}

inline bool runtimeDebugOutputEnabled()
{
    return envFlagEnabled("MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT");
}

inline bool hasRuntimeDebugArg(const QStringList& args)
{
    return args.contains(QStringLiteral("--miacode-debug"))
        || args.contains(QStringLiteral("--debug-runtime"))
        || args.contains(QStringLiteral("--enable-debug-output"));
}

inline QString startupTimingLogPath()
{
    return QDir::temp().filePath(QStringLiteral("miacode_startup_timing.log"));
}

inline QString runtimeDebugLogPath()
{
    return QDir::temp().filePath(QStringLiteral("miacode_runtime_debug.log"));
}

}  // namespace miacode::debug_options
