#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    const QString path = QDir(QStringLiteral(MIACODE_SOURCE_ROOT)).filePath(
        QStringLiteral("scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err << "FAIL: script missing or unreadable: " << path << Qt::endl;
        return 1;
    }
    const QString script = QString::fromUtf8(file.readAll());

    bool ok = true;
    const QStringList requiredTokens = {
        QStringLiteral("#requires -Version 5.1"),
        QStringLiteral("ValidateSet(\"GpuBound\", \"GpuOff\")"),
        QStringLiteral("MIACODE_GPU_BIND_HIGH_PERFORMANCE"),
        QStringLiteral("MIACODE_LOG_DIR"),
        QStringLiteral("Get-FileHash"),
        QStringLiteral("SHA256"),
        QStringLiteral("Win32_OperatingSystem"),
        QStringLiteral("Win32_Processor"),
        QStringLiteral("Win32_PhysicalMemory"),
        QStringLiteral("Win32_VideoController"),
        QStringLiteral("powercfg.exe"),
        QStringLiteral("Env:MIACODE_"),
        QStringLiteral("Start-Process"),
        QStringLiteral("--debug"),
    };
    for (const QString& token : requiredTokens) {
        ok &= require(script.contains(token),
                      QStringLiteral("script contains required token: %1").arg(token), err);
    }

    ok &= require(script.contains(QStringLiteral("$gpuBinding = \"1\""))
                      && script.contains(QStringLiteral("$gpuBinding = \"0\"")),
                  QStringLiteral("both profiles set an explicit GPU binding value"), err);
    ok &= require(!script.contains(QStringLiteral("Stop-Process"), Qt::CaseInsensitive),
                  QStringLiteral("script never kills MiaCode"), err);
    ok &= require(!script.contains(QStringLiteral("Wait-Process"), Qt::CaseInsensitive),
                  QStringLiteral("script does not wait for MiaCode"), err);
    ok &= require(!script.contains(QStringLiteral("Remove-Item"), Qt::CaseInsensitive),
                  QStringLiteral("script never deletes prior evidence"), err);

    if (ok) {
        out << "Idle-freeze repro script spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
