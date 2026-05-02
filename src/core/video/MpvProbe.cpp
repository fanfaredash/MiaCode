#include "core/video/MpvProbe.h"

#include "common/DebugLog.h"

#include <mpv/client.h>

#include <QStringLiteral>

namespace miacode::preview::video {

bool probeLibmpvAtStartup()
{
    // Static link via libmpv-2.lib (third_party/mpv/lib/win64). If the
    // DLL is missing at load time the process aborts before main runs;
    // by the time we reach here, libmpv-2.dll is loaded. The version
    // we report is libmpv's CLIENT API version, not the mpv build
    // version: the API is what we code against, the build version
    // can drift more freely.
    const unsigned long version = mpv_client_api_version();

    const unsigned long major = (version >> 16) & 0xFFFFu;
    const unsigned long minor = version & 0xFFFFu;

    // force=true: write the probe outcome synchronously regardless of
    // whether --debug was passed. This is a one-shot startup record;
    // bypassing the runtime-channel gate makes it usable for
    // packaging/CI smoke tests that don't run with debug logging.
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/mpv_probe"),
        QStringLiteral("api_version=0x%1 major=%2 minor=%3")
            .arg(version, 8, 16, QChar('0'))
            .arg(major)
            .arg(minor),
        /*force=*/true);

    return version != 0 && major > 0;
}

}  // namespace miacode::preview::video
