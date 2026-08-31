#pragma once

#include <QVariantMap>

namespace miacode::v2 {

// 音视频处理's single-file operations.
//
// Stage 3.5 item 2. QmlMediaToolsModel owns the PV batch queue itself — that is
// the only part with state worth keeping between openings — and forwards these
// six to the window, which still holds the ffmpeg plumbing, the track/背景视频
// paths and the backup files.
//
// `isTrack` selects which media the operation applies to: true for the audio
// track, false for the background video. The two flows are otherwise identical,
// which is why they share one entry point rather than doubling the surface.
//
// Deliberately Qt-GUI-free: the results are plain value maps, so the page can
// render them and a Core-only spec can link the contract.
class MediaToolsEngine
{
public:
    virtual ~MediaToolsEngine() = default;

    // The two conversions run as jobs and report through JobProgressService.
    virtual void convertTrackTo44100Hz() = 0;
    virtual void compressBackgroundVideo() = 0;

    // What the 前置空白 editor needs before it can offer anything: the current
    // media path, whether a backup exists, the detected bpm, and so on. An
    // empty map means the operation is not available for that media.
    virtual QVariantMap mediaBlankContext(bool isTrack) = 0;
    // Detected leading-silence timing for the same media, as a value map.
    virtual QVariantMap detectMediaBlankTiming(bool isTrack) = 0;

    virtual void restoreMediaBlankBackup(bool isTrack) = 0;
    virtual void applyMediaBlank(bool isTrack, double beats, double bpm) = 0;

protected:
    MediaToolsEngine() = default;
    MediaToolsEngine(const MediaToolsEngine&) = default;
    MediaToolsEngine& operator=(const MediaToolsEngine&) = default;
};

}  // namespace miacode::v2
