#pragma once

#include <QIcon>
#include <QWidget>

class QEvent;
class QLabel;
class QObject;
class QPushButton;
class QSlider;

namespace miacode::cover_export {

class CoverStudioPanel;

// Bottom playback bar (§5 transport, P4). A dumb view over the selected chart
// frame: play/pause + step + a styled scrubber + a mm:ss.cs / total readout,
// modelled on QuickShellPreviewTransport. All playback state lives in
// CoverStudioPanel; this only emits intents (toggle / seek / step) and reflects
// the panel's playheadChanged / playbackStateChanged signals.
class CoverFramePickerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverFramePickerPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

protected:
    // Space / Home / End / ←/→ on the bar drive play / seek (§8 transport focus).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();                       // enable + range from the active layer
    void setPositionSeconds(double seconds);
    void setPlaying(bool playing);

    CoverStudioPanel* studio_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* forwardButton_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* label_ = nullptr;
    QIcon playIcon_;
    QIcon pauseIcon_;
};

}  // namespace miacode::cover_export
