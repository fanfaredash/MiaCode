#pragma once

#include <QString>
#include <QStringList>

namespace miacode::debug_log {

enum class Channel {
    Runtime,
    Audio,
    Export,
    StartupTiming,
    Fatal,
    PreviewProfile,
};

QString timestampString();
QString logDirectory();
void setSessionProjectLogDirectory(const QString& directoryPath);
QString logPath(Channel channel);
QString runtimeLogPath();
QString audioLogPath();
QString exportLogPath();
QString startupTimingLogPath();
QString fatalLogPath();
QString previewProfileSummaryPath();

QString formatTitleLine(const QString& title);

bool clearChannel(Channel channel);
void clearDebugSessionLogs();
bool resetChannel(Channel channel, const QStringList& initialLines = {}, bool force = false);
bool appendText(Channel channel, const QString& text, bool force = false);
bool appendLine(Channel channel, const QString& scope, const QString& payload, bool force = false);
bool initializeStartupTimingLogSession();
bool appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs);
bool appendFatalMessage(const QString& scope, const QString& payload);

}  // namespace miacode::debug_log
