#pragma once

// Internal header for the PreviewStageMediaHost translation units.
//
// PreviewStageMediaHost.cpp was split into multiple .cpp TUs
// (PreviewStageMediaHost_Backend/Media/Playback/Diagnostics/Timeout.cpp +
// the slimmed PreviewStageMediaHost.cpp core). File-local helpers/constants
// that were in the original .cpp's anonymous namespace and are used by MORE
// THAN ONE of those TUs live here, in a NAMED namespace, so they have
// external linkage and a single definition instead of one copy per TU.
//
// STATELESS helpers only. Helpers/constants used by exactly one TU stay in
// that TU's own anonymous namespace. Mutable shared state (e.g. the
// isIntegratedRenderAdapter() static-local cache) is NOT here — it stays in
// exactly one TU (Backend) to avoid splitting the cache.
//
// Each TU includes this header (after the same #include block the original
// .cpp had) and does `using namespace miacode::preview::psmh_detail;`.

#include "common/DebugLog.h"

#include <QString>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(HAVE_QT_MULTIMEDIA) && !defined(MIACODE_USE_QTAVPLAYER)
#include <QMediaPlayer>
#endif

namespace miacode {
namespace preview {
namespace psmh_detail {

inline constexpr qint64 kPausedSeekAckToleranceMs = 80;

// A seek request this close to the last one already submitted is a no-op: the
// decoder is parked on (or within a frame of) the target, so the frame on screen
// is already the right one. Re-issuing it costs a full decoder flush plus a
// re-decode from the nearest keyframe, which is exactly the work a play/seek is
// trying to avoid. Every site that programs a decode position coalesces against
// `lastSeekMs_` with this tolerance.
inline constexpr qint64 kSeekCoalesceToleranceMs = 40;

inline unsigned long currentBeaconTid() noexcept
{
#ifdef Q_OS_WIN
    return static_cast<unsigned long>(::GetCurrentThreadId());
#else
    return 0;
#endif
}

inline void appendPreviewStageMediaLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/stage_media"),
        text
    );
}

#if defined(HAVE_QT_MULTIMEDIA) && !defined(MIACODE_USE_QTAVPLAYER)
inline QString playbackStateName(QMediaPlayer::PlaybackState state)
{
    switch (state) {
    case QMediaPlayer::StoppedState:
        return QStringLiteral("StoppedState");
    case QMediaPlayer::PlayingState:
        return QStringLiteral("PlayingState");
    case QMediaPlayer::PausedState:
        return QStringLiteral("PausedState");
    default:
        return QStringLiteral("UnknownState");
    }
}

inline QMediaPlayer::PlaybackState playerPlaybackState(const QMediaPlayer* player)
{
    if (player == nullptr) {
        return QMediaPlayer::StoppedState;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return player->playbackState();
#else
    return player->state();
#endif
}
#endif

}  // namespace psmh_detail
}  // namespace preview
}  // namespace miacode
