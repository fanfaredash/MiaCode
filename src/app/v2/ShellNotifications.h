#pragma once

#include <QObject>

namespace miacode::v2 {

// What the window pushes at the QML layer.
//
// Stage 3.5 item 2's last piece. Every method call has moved onto an interface,
// but the Qml*Models still held a MainWindow& to connect its signals — so the
// hidden window remained a compile-time dependency of eleven files for
// notification alone.
//
// These are re-emitted here rather than moved because most of them are still
// raised from widget-era code paths. That is fine for a notification: unlike the
// handler hooks on DocumentBridge, nothing is waiting on a result, so a relay
// changes nothing about the contract.
//
// Owned by ApplicationServices; MainWindow forwards into it.
class ShellNotifications final : public QObject
{
    Q_OBJECT

public:
    explicit ShellNotifications(QObject* parent = nullptr);

signals:
    // Discrete shell state (tab visibility, playback flags, geometry) changed
    // and every projection should re-read. Replaced the v1 polling timer.
    void presentationChanged();
    // The preview playhead advanced. Separate from presentationChanged because
    // it fires per frame during playback and nothing else should re-read that
    // often.
    void previewPlayheadChanged();
    void previewSkinDirectoryChanged();

    // The document was replaced by a path outside the QML façade — startup,
    // root chart drop, native File/Open, crash recovery.
    void documentReplaced();

    void editorPreferencesChanged();
    void muriPromptPreferenceChanged();
    void videoExportWorkerRunningChanged(bool running);

    // Menu / shortcut entry points that land on the window and have to reach
    // whichever QML surface owns the action.
    void normalizeWholeChartRequested();
    void mediaToolsRequested();
    void preferencesRequested();
    void coverExportRequested(int difficultyId);
};

}  // namespace miacode::v2
