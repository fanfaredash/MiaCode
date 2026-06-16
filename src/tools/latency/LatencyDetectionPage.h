#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

#include "LatencyAnalysis.h"

class MainWindow;
class QDoubleSpinBox;
class QEvent;
class QLabel;
class QPushButton;
class QRadioButton;
class QSlider;
class QTimer;
class QSpinBox;

namespace miacode::latency {

class LatencySandboxController;

// Central-area page wired into MainWindow::editorStack_. Replaces the
// LatencyDetectorDialog popup: three cards (BPM / Offset / Audition)
// rendered in the same card-on-white style as the metadata page, plus
// auto-detect buttons that run the analysis algorithms inline and
// auto-write detected values back into the document.
//
// The page delegates audition lifecycle + chart synthesis to a
// LatencySandboxController owned by MainWindow; this widget just owns
// the controls and the debounce/UI glue.
class LatencyDetectionPage : public QWidget
{
    Q_OBJECT

public:
    explicit LatencyDetectionPage(MainWindow* owner, QWidget* parent = nullptr);
    ~LatencyDetectionPage() override;

    // (Re)apply the theme-aware stylesheet. Called on construction and again
    // whenever the app theme changes (light/dark), so the cards/labels don't
    // stay frozen at the startup theme.
    void applyThemeStyles();

    // Sync controls from the document; safe to call repeatedly.
    void refreshFromDocument();

    // Lifecycle hooks called from MainWindow's outline-switch flow.
    void onPageEntered();
    void onPageLeft();

protected:
    // App-level filter: routes Ctrl+S on this page to save (see the .cpp for why
    // a filter instead of a QShortcut). NOTE: a page-local Ctrl+Z/Y undo history
    // was attempted here and reverted — see the .cpp.
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onBpmEditValueChanged(double value);
    void onOffsetEditValueChanged(double value);
    void onSubdivisionToggled();
    void onSfxVolumeChanged(int value);
    void onAuditionButtonClicked();
    void onDetectBpmClicked();
    void onDetectOffsetClicked();
    void onAuditionStateChanged(bool running);
    void onPlayheadAdvanced(double seconds);

private:
    void buildUi();
    void commitBpmEdit();
    void commitOffsetEdit();
    void commitClockCountEdit();
    void updateAuditionUi(bool running);
    void updatePositionLabel(double seconds);
    void updateAutoDetectAvailability();
    void ensureAudioEnvelopeReady();
    void clearAudioEnvelopeCache();
    QString formatPosition(double seconds) const;
    QString localizedText(const QString& zh, const QString& en) const;
    QString currentTrackPath() const;
    double documentOffsetSeconds() const;
    double documentWholeBpm() const;
    int documentClockCount() const;

    QPointer<MainWindow> owner_;
    QPointer<LatencySandboxController> sandbox_;

    // Page header: its gear icon is tinted with the theme accent, baked into a
    // QIcon (not reachable via the page stylesheet), so applyThemeStyles must
    // re-render it on a light/dark switch — keep the pointer.
    QPushButton* mediaToolsButton_ = nullptr;

    // BPM card
    QDoubleSpinBox* bpmEdit_ = nullptr;
    QPushButton* detectBpmButton_ = nullptr;
    QLabel* bpmDetectResultLabel_ = nullptr;

    // Offset card
    QDoubleSpinBox* offsetEdit_ = nullptr;
    QPushButton* detectOffsetButton_ = nullptr;
    QLabel* offsetDetectResultLabel_ = nullptr;

    // Clock count (spin box only; auto-filled together with BPM detection)
    QSpinBox* clockCountEdit_ = nullptr;

    // Audition card
    QRadioButton* subdivision4Radio_ = nullptr;
    QRadioButton* subdivision8Radio_ = nullptr;
    QPushButton* auditionButton_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QSlider* sfxVolumeSlider_ = nullptr;
    QLabel* sfxVolumeValueLabel_ = nullptr;

    QTimer* bpmDebounceTimer_ = nullptr;
    QTimer* offsetDebounceTimer_ = nullptr;
    QTimer* clockCountDebounceTimer_ = nullptr;

    // Cached audio envelopes for auto-detection (re-decoded only when
    // the track path changes).
    QString cachedAudioPath_;
    latency_analysis::Envelope cachedOnsetEnvelope_;
    latency_analysis::Envelope cachedTransientEnvelope_;
    double cachedAudioDurationSeconds_ = 0.0;

    // Carry the last BPM-detection meter hint into the next
    // offset-detection call (improves accuracy on non-4/4 songs even
    // though the user-facing meter selector is gone).
    QString lastDetectedMeterId_ = QStringLiteral("4/4");
    double lastDetectedMeterPhase_ = 0.0;
    bool hasLastDetectedMeterPhase_ = false;

    // Track whether the user is currently editing via the spin box so
    // refreshFromDocument doesn't clobber an in-flight value.
    bool suppressDocumentRefresh_ = false;
};

}  // namespace miacode::latency
