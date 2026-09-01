#pragma once

#include "runtime/Session.h"

class QMouseEvent;
class QPoint;
class QPointF;
class QString;

namespace miacode::runtime {

class ShellHost {
public:
    ShellHost(Session& session, Session::HostUi& ui, Session::HostState& state);

    bool rootWindowFrameGeometryAvailable() const;
    QRect rootWindowFrameGeometry() const;
    void attachRootWindow(QWindow* window);
    void requestShellClose(std::function<void(bool)> onDecided);
    bool finishShellClose(QElapsedTimer totalTimer);
    void setBottomTabsHeight(int height);
    void setRootWindowFrameGeometry(const QRect& geometry);
    void noteRootWindowReady();

    void configureRuntimeDebugOutput();
    void setupInitialWindowGeometry();
    void applyUiTheme();
    void updateOutlineDockCollapseButton();
    void setOutlineDockCollapsed(bool collapsed);
    void applySystemWindowBackdrop(QWidget* target = nullptr) const;
    int computeBottomTabsDeviceHeight() const;
    int computeBottomTabsDeviceHeightForScale(double contentScale) const;
    void applyBottomTabsContentScale();
    void updateBottomTabsDeviceHeight();
    QString formatWindowStateFlags(Qt::WindowStates states) const;
    void logWindowGeometryDebug(const QString& tag, const QString& detail = QString());
    void logTopLevelWindowSnapshot(const QString& tag);
    void closeEvent(QCloseEvent* event);
    void appendOutput(const QString& title, const QString& payload);
    QString describeFocusWidget(QWidget* widget) const;
    QString formatFocusReason(Qt::FocusReason reason) const;
    void logFocusDebug(const QString& reason, QWidget* oldWidget = nullptr, QWidget* nowWidget = nullptr, const QString& detail = QString());
    QTextEdit* activeFindTarget() const;
    bool runFindInEditor(bool backward);
    void updateEditorFindBarGeometry();
    void applyFindOverlayInset();
    void hideFindReplaceBar();
    void onToggleFindReplace();
    void onFindNext();
    void onFindPrevious();
    void onReplaceOne();
    void onReplaceAll();
    void refreshQuickShellRehostedWidgetParent(QWidget* widget);
    void setInvalidStarPreviewEasterEggEnabled(bool enabled);
    void ensureInvalidStarPreviewEasterEggSounds();
    void playInvalidStarPreviewEasterEggSound(bool enabled);
    bool eventFilter(QObject* watched, QEvent* event);
    void resizeEvent(QResizeEvent* event);
    void moveEvent(QMoveEvent* event);
    void showEvent(QShowEvent* event);
    void hideEvent(QHideEvent* event);
    bool event(QEvent* event);
    void changeEvent(QEvent* event);

private:
    QTextEdit* resolveRestorableTextEdit(QWidget* widget) const;
    bool isTextInputWidget(QWidget* widget) const;
    bool hasActiveTextInputFocus() const;
    bool shouldRespectFocusedWidgetOnRestore(QWidget* widget, QTextEdit* target) const;
    bool quickShellFocusBridgeActive() const;
    QWindow* previewVisibleHostWindow() const;
    void focusPreviewInteractionTarget(QObject* watched, Qt::FocusReason reason);
    bool touchPadAuthoringEditableContext() const;
    void setHoveredTouchPad(const QString& pad);
    void handleApplicationFocusChanged(QWidget* old, QWidget* now);
    void handleApplicationStateChanged(Qt::ApplicationState state);
    void recoverPreviewBackendsAfterApplicationResume();
    void rememberFocusedTextEditState(QTextEdit* textEdit);
    void rememberFocusedTextEditState();
    void restoreFocusedTextEditState();
    void restoreFocusedTextEditStateAttempt(QPointer<QTextEdit> target, int savedAnchor, int savedPosition, int attempt);
    void clearFocusedTextEditState();

    Session& session_;
    Session::HostUi& ui_;
    Session::HostState& state_;
    QPointer<QTextEdit> pendingTextFocusWidget_;
    int pendingTextCursorAnchor_ = -1;
    int pendingTextCursorPosition_ = -1;
};

}  // namespace miacode::runtime
