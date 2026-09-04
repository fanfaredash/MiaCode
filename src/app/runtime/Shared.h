#pragma once

#include <QByteArray>
#include <QFont>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "SimaiNativeParser.h"
#include "WindowParityMetrics.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "runtime/RuntimeContext.h"

class QFileInfo;
class QObject;

namespace miacode::v2 {
class ShellNotifications;
}

namespace miacode::runtime::shared {

// Single writer for the preview pause second.
//
// Why this exists: `pauseSecond_` was written directly from many call sites,
// none of them logged, and one of them (`applyQtPreviewPosition`) fires unconditionally
// on every playback tick. A reported symptom — the timeline stepping BACKWARD some time
// after a pause, by roughly the length of an audio-device stall — was therefore
// un-diagnosable: nothing recorded who moved the playhead, when, or from where, so every
// explanation for it was a guess from reading code, and two such guesses were wrong.
//
// Routing a write through here makes a backward move name its own author. Only backward
// moves while paused are logged: forward motion during playback is the normal per-frame
// case and would bury the channel.
//
// The invariant this depends on is a rule, not a headcount: `pauseSecond_` must
// never be assigned except through this function. Check it with
//
//   grep -rn "pauseSecond_[[:space:]]*=" src --include="*.cpp" --include="*.h" \
//     --include="*.inc" | grep -v writePreviewPauseSecond
//
// which should match only the declaration and the reference alias in
// SessionMembers.inc. Any other hit is a hole: that writer can move the pause
// second backward without leaving a row, which defeats the whole point of the channel.
// One such hole shipped and was missed by the original "every write is routed" claim —
// `latency::LatencySandboxController::applyPlayheadToScene`, outside the Session
// section files the audit looked at — so run the grep rather than trusting this comment.
inline void writePreviewPauseSecond(
    double& slot,
    double next,
    bool previewPlaying,
    const char* reason)
{
    const double previous = slot;
    slot = next;
    if (previewPlaying || !miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    // Half a timeline frame at 120 Hz — below this a "move" is re-flush noise from the
    // same logical position, not a jump anyone can see.
    constexpr double kBackwardMoveEpsilonSeconds = 0.004;
    if (!(next < previous - kBackwardMoveEpsilonSeconds)) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/playback"),
        QStringLiteral("action=pause_second_moved_back reason=%1 from=%2 to=%3 delta_ms=%4")
            .arg(QLatin1String(reason))
            .arg(previous, 0, 'f', 6)
            .arg(next, 0, 'f', 6)
            .arg((next - previous) * 1000.0, 0, 'f', 3));
}

// Single writer for the preview playing flag.
//
// Why this exists: `playing_` sits right next to `pauseSecond_` in
// RuntimeContext::State and used to have the same problem — it was written from
// `Session::setPreviewPlayingFlag`, a method any holder of a Session& could call
// directly. That was fine while playback logic lived on Session, but once it
// moved into PlaybackCoordinator (which holds the same `state_` by reference),
// the coordinator could just as easily have assigned `state_.playing_` inline
// at its six call sites instead of forwarding through Session. Either path
// compiles; only one keeps `playing_` at a single assignment site, which is the
// property `preview_transport_push_spec` pins.
//
// So this follows the same rule as writePreviewPauseSecond immediately above:
// a rule, not a headcount. `playing_` must never be assigned except through
// this function. Check it with
//
//   grep -rn "playing_[[:space:]]*=[^=]" src --include="*.cpp" --include="*.h" \
//     --include="*.inc" | grep -v writePreviewPlayingFlag
//
// which should match only the declaration in RuntimeContext.h and the
// const-reference alias in SessionMembers.inc. Any other hit is a second
// writer.
//
// Stage 4.9e-4: `playing_` and `previewTransportState_` moved from
// RuntimeContext::State to RuntimeContext::PlaybackState (canonical
// playback-authority storage, owned by PlaybackCoordinator). This writer
// takes the mutable record directly — State only exposes a const& to it
// now — so PlaybackCoordinator is the only caller that can compile a call
// here.
void writePreviewPlayingFlag(
    RuntimeContext::PlaybackState& state,
    miacode::v2::ShellNotifications& notifications,
    bool playing);

inline constexpr int kEmbeddedPreviewPanelMinWidth = miacode::window_parity::kEmbeddedPreviewPanelMinWidth;
inline constexpr int kPreviewPanelMarginX = miacode::window_parity::kPreviewPanelMarginX;
inline constexpr int kPreviewControlStatsCardMinWidth = miacode::window_parity::kPreviewControlStatsCardMinWidth;
inline constexpr int kEditorTextFontSizeMin = 8;
inline constexpr int kEditorTextFontSizeMax = 28;
inline constexpr double kEditorLineSpacingFactorDefault = 3.0;
inline constexpr int kAutosaveIntervalMs = 2 * 60 * 1000;
inline constexpr int kAutosaveHistoryMaxVersions = 30;
inline constexpr int kAutosaveLatestIdleMs = 2 * 1000;
inline constexpr double kTimelineMaxUiUpdateFps = 3600.0;
// Cap interactive preview scrub updates at <= ? FPS so timeline dragging and
// preview-slider dragging do not spam seek work faster than the video path can settle.
inline constexpr int kPreviewScrubRenderIntervalMs = 67;

extern const QList<double> kEditorLineSpacingFactorOptions;

double normalizeEditorLineSpacingFactor(double factor);
QString editorLineSpacingFactorLabel(double factor);
int nearestPreviewPlaybackRateIndex(double rate);
double steppedPreviewPlaybackRate(double rate, int direction);
// The parser validation locale matching the session UI language.
SimaiNativeValidationLocale uiValidationLocale();
QByteArray autosaveContentSignature(const QString& text);
QString resolveProjectDataDirectoryPath(const QString& filePath);
void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs);
QFont editorFont(int pointSize = -1);
int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor);
qint64 fileLastModifiedMs(const QFileInfo& fileInfo);
double probeAudioDurationSeconds(const QString& trackPath);

// Stage 4.9d-4a: bodies pulled out of miacode::runtime::StageMediaHost (and, for
// setPreviewFixedTimerHighResolutionActive, Session) so PlaybackCoordinator can call
// them without going through a Session reference. Both the original host method and the
// coordinator's own copy now call these — see runtime/preview/StageMediaRoute.cpp,
// runtime/preview/WarmupAndSettings.cpp, runtime/playback/FramePacing.cpp and
// runtime/playback/SurfaceContract.cpp for the forwarding shells.
void ensurePreviewSfxRuntimePrepared(RuntimeContext::State& state);
void applyPreviewStageMediaRouteVisualSettings(RuntimeContext::State& state);
void applyPreviewStageMediaRoutePlaybackRate(RuntimeContext::State& state, double rate, const char* site = nullptr);
// owner is only used as the QuickShellPreviewCompositeSurface's QObject
// parent, never as a Session — see Shared.Preview.cpp.
void refreshQuickShellPreviewCompositeSurfaceState(RuntimeContext::State& state, QObject& owner);
void refreshPreviewStageMediaRouteDebugState(RuntimeContext::State& state, bool requestUpdate = true);
// Windows-only body (Q_OS_WIN); a no-op elsewhere. See the definition for why the
// platform conditional must not be simplified away.
void setPreviewFixedTimerHighResolutionActive(RuntimeContext::State& state, bool active);
// Skin-directory / outline-directory resolution: pure filesystem + asset-path
// lookups, no member state. previewSkinDisplayName and availablePreviewSkinDirectoryNames
// depend on the small name-normalization helpers below, which used to be
// WarmupAndSettings.cpp anonymous-namespace-local; promoted here so both
// StageMediaHost and PlaybackCoordinator can call the same implementation.
QString standardPreviewSkinDirectoryName();
QString dxPreviewSkinDirectoryName();
QString normalizePreviewSkinDirectoryName(QString name);
bool hasCorePreviewSkinAssets(const QString& directory);
QString resolvePreviewSkinRootDir();
QString resolvePreviewCustomOutlineDir();
QStringList availablePreviewSkinDirectoryNames();
QString previewSkinDisplayName(const QString& directoryName);

}  // namespace miacode::runtime::shared
