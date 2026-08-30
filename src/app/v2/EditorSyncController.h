#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

namespace miacode::v2 {

// The QML editor's live selection, as last reported through setEditorContext.
// `valid` means an editor is showing a chart and its identity is known; a
// caller that needs to act on the selection must still stamp its own request
// with the same (difficultyId, revision) so the gate can refuse a stale one.
struct EditorSelectionState {
    int difficultyId = -1;
    quint64 revision = 0;
    int anchor = 0;
    int position = 0;
    bool focused = false;
    bool valid = false;
};

struct EditorFollowState {
    int difficultyId = -1;
    quint64 revision = 0;
    int start = 0;
    int end = 0;
    int caret = 0;
    bool active = false;
    bool reveal = false;
    bool playbackActive = false;
};

class EditorSyncController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool followActive READ followActive NOTIFY followChanged)
    Q_PROPERTY(int followDifficultyId READ followDifficultyId NOTIFY followChanged)
    Q_PROPERTY(qulonglong followRevision READ followRevision NOTIFY followChanged)
    Q_PROPERTY(int followStart READ followStart NOTIFY followChanged)
    Q_PROPERTY(int followEnd READ followEnd NOTIFY followChanged)
    Q_PROPERTY(int followCaret READ followCaret NOTIFY followChanged)
    Q_PROPERTY(bool followReveal READ followReveal NOTIFY followChanged)
    Q_PROPERTY(bool followPlaybackActive READ followPlaybackActive NOTIFY followChanged)

public:
    explicit EditorSyncController(QObject* parent = nullptr);

    bool followActive() const;
    int followDifficultyId() const;
    qulonglong followRevision() const;
    int followStart() const;
    int followEnd() const;
    int followCaret() const;
    bool followReveal() const;
    bool followPlaybackActive() const;

    void publishFollow(const EditorFollowState& state);
    void setPlaybackActive(bool active);
    Q_INVOKABLE qulonglong requestNavigation(int difficultyId, qulonglong revision,
                                             int start, int end, bool focus, bool reveal);
    bool requestTouchPadAuthoring(const QString& pad, bool useBacktickSeparator);
    bool editorContextActive() const;
    EditorSelectionState editorSelection() const;

    Q_INVOKABLE void setEditorReadiness(int difficultyId, qulonglong revision,
                                        bool visible, bool metadataMode);
    Q_INVOKABLE void setEditorContext(int difficultyId, qulonglong revision,
                                     int anchor, int position, bool focused,
                                     bool imeComposing, int line, int column,
                                     bool publishCaret);
    Q_INVOKABLE void acknowledgeNavigation(qulonglong sequence, bool applied);
    Q_INVOKABLE void setTouchPadControlHold(bool active);
    Q_INVOKABLE bool beginPointerInteraction(int difficultyId, qulonglong revision);
    Q_INVOKABLE bool setTouchPadPreviewAnchor(int difficultyId, qulonglong revision,
                                               const QString& text, int tokenStart);
    Q_INVOKABLE bool seekPreviewToEditorLocation(int difficultyId, qulonglong revision,
                                                  int line, int column);

signals:
    void followChanged();
    void navigationRequested(qulonglong sequence, int difficultyId, qulonglong revision,
                             int start, int end, bool focus, bool reveal);
    void navigationFinished(qulonglong sequence, bool applied);
    void touchPadAuthoringRequested(const QString& pad, bool useBacktickSeparator,
                                    int difficultyId, qulonglong revision,
                                    int anchor, int position);
    void caretLocationPublished(int difficultyId, qulonglong revision, int line, int column);
    void pointerInteractionStarted(int difficultyId);
    void touchPadControlHoldChanged(bool active);
    void touchPadPreviewAnchorPublished(int difficultyId, int line, int column);
    void previewSeekPublished(int difficultyId, int line, int column);
    void editorContextChanged();

private:
    struct NavigationState {
        quint64 sequence = 0;
        int difficultyId = -1;
        quint64 revision = 0;
        int start = 0;
        int end = 0;
        bool focus = false;
        bool reveal = false;
    };

    struct EditorLocationState {
        int difficultyId = -1;
        quint64 revision = 0;
        int line = 1;
        int column = 1;
    };

    struct TouchPadRequest {
        QString pad;
        bool useBacktickSeparator = false;
        int difficultyId = -1;
        quint64 revision = 0;
        int anchor = 0;
        int position = 0;
    };

    bool readinessAccepts(int difficultyId, quint64 revision) const;
    void scheduleFollowDelivery();
    void scheduleEditorContextDelivery();
    void scheduleCaretDelivery();
    void schedulePointerInteractionDelivery();
    void scheduleTouchPadControlHoldDelivery();
    void scheduleTouchPadPreviewAnchorDelivery();
    void schedulePreviewSeekDelivery();
    void scheduleNavigationDelivery();
    void scheduleNavigationFinished(quint64 sequence, bool applied);
    void scheduleTouchPadDelivery();

    EditorFollowState follow_;
    bool followDeliveryQueued_ = false;

    int readyDifficultyId_ = -1;
    quint64 readyRevision_ = 0;
    bool editorVisible_ = false;
    bool metadataMode_ = false;

    int contextDifficultyId_ = -1;
    quint64 contextRevision_ = 0;
    int contextAnchor_ = 0;
    int contextPosition_ = 0;
    bool editorFocused_ = false;
    bool imeComposing_ = false;
    bool editorContextDeliveryQueued_ = false;

    EditorLocationState pendingCaret_;
    EditorLocationState deliveredCaret_;
    bool caretPending_ = false;
    bool caretDeliveryQueued_ = false;
    bool deliveredCaretValid_ = false;

    int pendingPointerDifficultyId_ = -1;
    quint64 pendingPointerRevision_ = 0;
    bool pointerInteractionPending_ = false;
    bool pointerInteractionDeliveryQueued_ = false;

    bool pendingTouchPadControlHold_ = false;
    bool deliveredTouchPadControlHold_ = false;
    bool touchPadControlHoldDeliveryQueued_ = false;

    EditorLocationState pendingTouchPadPreviewAnchor_;
    bool touchPadPreviewAnchorPending_ = false;
    bool touchPadPreviewAnchorDeliveryQueued_ = false;

    EditorLocationState pendingPreviewSeek_;
    bool previewSeekPending_ = false;
    bool previewSeekDeliveryQueued_ = false;

    quint64 nextNavigationSequence_ = 0;
    NavigationState pendingNavigation_;
    bool navigationPending_ = false;
    bool navigationDeliveryQueued_ = false;
    quint64 deliveredNavigationSequence_ = 0;

    QQueue<TouchPadRequest> pendingTouchPadRequests_;
    bool touchPadDeliveryQueued_ = false;
};

} // namespace miacode::v2
