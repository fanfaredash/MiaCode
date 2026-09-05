#include "PvCompressionPolicy.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTextStream>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

int argumentValueIndex(const QStringList& args, const QString& option)
{
    return args.indexOf(option);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    bool ok = true;
    using namespace miacode::media;
    ok &= check(
        kPvCompressionHardLimitBytes == 20'000'000LL,
        "PV compression uses the decimal 20 MB hard limit");
    ok &= check(
        kPvCompressionWorkingTargetBytes < kPvCompressionHardLimitBytes,
        "PV compression reserves space below the hard limit");

    const PvCompressionPlan plan = makePvCompressionPlan(180.0);
    ok &= check(plan.videoBitrateKbps == 866, "180 second plan uses the working target");

    const QStringList firstPass = makePvCompressionPassArguments(
        QStringLiteral("input.mp4"),
        QStringLiteral("output.mp4"),
        QStringLiteral("passlog"),
        plan,
        1);
    const QStringList secondPass = makePvCompressionPassArguments(
        QStringLiteral("input.mp4"),
        QStringLiteral("output.mp4"),
        QStringLiteral("passlog"),
        plan,
        2);
    ok &= check(firstPass.contains(QStringLiteral("-an")), "first pass removes audio");
    ok &= check(secondPass.contains(QStringLiteral("-an")), "second pass removes audio");
    const int fpsModeIndex = argumentValueIndex(secondPass, QStringLiteral("-fps_mode"));
    ok &= check(
        fpsModeIndex >= 0 && secondPass.value(fpsModeIndex + 1) == QStringLiteral("passthrough"),
        "PV compression preserves source frame timestamps");
    ok &= check(!secondPass.contains(QStringLiteral("-r")), "PV compression never overrides frame rate");
    ok &= check(!secondPass.contains(QStringLiteral("-c:a")), "PV compression does not encode audio");
    ok &= check(
        firstPass.contains(QStringLiteral("-f"))
            && firstPass.contains(QStringLiteral("null"))
            && firstPass.contains(QProcess::nullDevice()),
        "first pass writes to the null device");
    const int firstPassNumberIndex = argumentValueIndex(firstPass, QStringLiteral("-pass"));
    const int secondPassNumberIndex = argumentValueIndex(secondPass, QStringLiteral("-pass"));
    ok &= check(
        firstPassNumberIndex >= 0 && firstPass.value(firstPassNumberIndex + 1) == QStringLiteral("1"),
        "first pass uses pass number one");
    ok &= check(
        secondPassNumberIndex >= 0 && secondPass.value(secondPassNumberIndex + 1) == QStringLiteral("2"),
        "second pass uses pass number two");
    ok &= check(
        secondPass.constLast() == QStringLiteral("output.mp4"),
        "second pass writes the MP4 output");
    ok &= check(
        firstPass.constLast() == QProcess::nullDevice(),
        "first pass does not write an MP4 output");

    ok &= check(
        isAcceptablePvCompressionOutput(30'000'000LL, 19'999'999LL),
        "19,999,999-byte output is accepted");
    ok &= check(
        isAcceptablePvCompressionOutput(kPvCompressionHardLimitBytes, 19'999'999LL),
        "an original exactly at 20,000,000 bytes is compressible");
    ok &= check(
        !isAcceptablePvCompressionOutput(30'000'000LL, 20'000'000LL),
        "20,000,000-byte output is rejected");
    ok &= check(
        !isAcceptablePvCompressionOutput(30'000'000LL, 30'000'000LL),
        "output equal to the original is rejected");
    ok &= check(
        !isAcceptablePvCompressionOutput(30'000'000LL, 0),
        "empty output is rejected");

    const PvCompressionPlan retryPlan = adjustedPvCompressionPlan(plan, 20'500'000LL);
    ok &= check(
        retryPlan.videoBitrateKbps < plan.videoBitrateKbps,
        "oversize retry lowers the video bitrate");

    return ok ? 0 : 1;
}
