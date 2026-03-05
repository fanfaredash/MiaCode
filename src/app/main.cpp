#include "AppVersion.h"
#include "MainWindow.h"
#include "UiText.h"

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDir>

namespace {

bool startupTimingEnabled()
{
    static const bool enabled = []() {
        const QString raw = qEnvironmentVariable(
            "MIACODE_ENABLE_STARTUP_TIMING",
            qEnvironmentVariable("MAIMURI_ENABLE_STARTUP_TIMING")
        ).trimmed();
        return raw == "1" || raw.compare("true", Qt::CaseInsensitive) == 0;
    }();
    return enabled;
}

}  // namespace

int main(int argc, char* argv[])
{
    QElapsedTimer startupTimer;
    startupTimer.start();
    qint64 lastStageMs = 0;
    const QString startupLogPath = QDir::toNativeSeparators(QDir::temp().filePath("miacode_startup_timing.log"));
    if (startupTimingEnabled()) {
        QFile logFile(startupLogPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
            out << "pid=" << QCoreApplication::applicationPid() << "\n";
            out << "log_path=" << startupLogPath << "\n";
        }
    }
    const auto logStartupStage = [&](const QString& stage) {
        if (!startupTimingEnabled()) {
            return;
        }
        const qint64 nowMs = startupTimer.elapsed();
        const qint64 deltaMs = nowMs - lastStageMs;
        lastStageMs = nowMs;
        QFile logFile(startupLogPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << "stage=" << stage << ", elapsed_ms=" << nowMs << ", delta_ms=" << deltaMs << "\n";
        }
    };
    logStartupStage("process_entry");

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    logStartupStage("surface_format_ready");

    QApplication app(argc, argv);
    logStartupStage("qapplication_constructed");
    app.setApplicationName("MiaCode");
    app.setApplicationVersion(MIACODE_VERSION_STRING);
    app.setStyle(QStyleFactory::create("Fusion"));
    logStartupStage("app_style_ready");

    if (UiText::isChineseUi()) {
        QFont zhUiFont;
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            zhUiFont.setFamily(family);
            if (zhUiFont.family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
        zhUiFont.setStyleStrategy(QFont::PreferAntialias);
        zhUiFont.setHintingPreference(QFont::PreferNoHinting);
        app.setFont(zhUiFont);
    }
    logStartupStage("ui_font_ready");

    MainWindow window;
    logStartupStage("mainwindow_constructed");
    window.showMaximized();
    logStartupStage("mainwindow_showmaximized_called");
    QTimer::singleShot(0, &app, [&logStartupStage]() {
        logStartupStage("event_loop_first_tick");
    });
    return app.exec();
}
