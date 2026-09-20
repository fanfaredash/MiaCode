#include "MediaPrependPolicy.h"

#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString optionValue(const QStringList& args, const QString& option)
{
    const int index = args.indexOf(option);
    return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
}

bool audioArguments(QTextStream& err)
{
    bool ok = true;
    const struct {
        const char* suffix;
        const char* encoder;
    } formats[] = {
        {"mp3", "libmp3lame"},
        {"wav", "pcm_s16le"},
        {"flac", "flac"},
        {"ogg", "libvorbis"},
    };

    for (const auto& format : formats) {
        const QStringList args = miacode::media::makeAudioPrependArguments(
            QStringLiteral("input.%1").arg(QLatin1String(format.suffix)),
            QStringLiteral("output.%1").arg(QLatin1String(format.suffix)),
            QLatin1String(format.suffix),
            2.0);
        const QString filter = optionValue(args, QStringLiteral("-filter_complex"));
        ok &= require(
            filter.contains(QStringLiteral("afade=t=in:st=0:d=0.005000")),
            QStringLiteral("%1 prepend has a 5 ms source fade-in").arg(QLatin1String(format.suffix)),
            err);
        ok &= require(
            filter.count(QStringLiteral("sample_rates=44100:channel_layouts=stereo")) == 2,
            QStringLiteral("%1 prepend normalizes both segments").arg(QLatin1String(format.suffix)),
            err);
        ok &= require(
            filter.contains(QStringLiteral("concat=n=2:v=0:a=1"))
                && !filter.contains(QStringLiteral("acrossfade")),
            QStringLiteral("%1 prepend preserves duration with non-overlapping concat")
                .arg(QLatin1String(format.suffix)),
            err);
        ok &= require(
            optionValue(args, QStringLiteral("-c:a")) == QLatin1String(format.encoder),
            QStringLiteral("%1 prepend retains its output encoder").arg(QLatin1String(format.suffix)),
            err);
    }
    return ok;
}

bool videoArgumentsAndSizing(QTextStream& err)
{
    bool ok = true;
    const miacode::media::PvCompressionPlan plan =
        miacode::media::makePvPrependCompressionPlan(1'000'000, 100.0, 10.0);
    ok &= require(plan.targetBytes == 1'122'000,
                  QStringLiteral("prepend target follows source bitrate, duration, and allowance"), err);
    ok &= require(plan.videoBitrateKbps == 81,
                  QStringLiteral("prepend bitrate is based on total output duration"), err);

    const QStringList args = miacode::media::makePvPrependPassArguments(
        QStringLiteral("input.mp4"),
        QStringLiteral("output.mp4"),
        QStringLiteral("pass-log"),
        plan,
        10.0,
        2);
    const QString filter = optionValue(args, QStringLiteral("-vf"));
    ok &= require(filter == QStringLiteral(
                      "tpad=start_duration=10.000000:start_mode=add:color=black,format=yuv420p"),
                  QStringLiteral("video prepend uses source-preserving tpad"), err);
    ok &= require(!args.join(QLatin1Char(' ')).contains(QStringLiteral("1920x1080"))
                      && !filter.contains(QStringLiteral("scale="))
                      && optionValue(args, QStringLiteral("-fps_mode")) == QStringLiteral("passthrough"),
                  QStringLiteral("video prepend neither upscales nor forces frame rate"), err);
    ok &= require(optionValue(args, QStringLiteral("-b:v")) == QStringLiteral("81k")
                      && optionValue(args, QStringLiteral("-preset")) == QStringLiteral("veryfast")
                      && args.contains(QStringLiteral("+faststart"))
                      && args.last() == QStringLiteral("output.mp4"),
                  QStringLiteral("second pass keeps bounded-memory speed and writes the requested output"), err);

    ok &= require(miacode::media::isAcceptablePvPrependOutput(plan, 1'140'000),
                  QStringLiteral("small muxing variance is accepted"), err);
    ok &= require(!miacode::media::isAcceptablePvPrependOutput(plan, 1'150'000),
                  QStringLiteral("abnormal target overrun is rejected"), err);

    const miacode::media::PvCompressionPlan capped =
        miacode::media::makePvPrependCompressionPlan(30'000'000, 100.0, 5.0);
    ok &= require(capped.targetBytes == miacode::media::kPvCompressionWorkingTargetBytes,
                  QStringLiteral("large prepend output stays below the PV working target"), err);
    ok &= require(!miacode::media::isAcceptablePvPrependOutput(capped, 20'000'000),
                  QStringLiteral("hard limit remains exclusive"), err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    const bool ok = audioArguments(err) && videoArgumentsAndSizing(err);
    if (ok) {
        out << "Media prepend policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
