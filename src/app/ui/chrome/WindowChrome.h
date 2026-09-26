#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

class QWindow;

// v2 WindowTitleBar chrome. Attach only from Bootstrap (never v1).
// Windows: WM_NCCALCSIZE over the native caption.
// macOS: full-size content; native title text hidden; QWindow::title kept.
// titleBarLeadingInset: clearance past macOS traffic lights (0 elsewhere).
namespace miacode::ui {

class WindowChrome final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(qreal titleBarLeadingInset READ titleBarLeadingInset NOTIFY titleBarLeadingInsetChanged FINAL)
    Q_PROPERTY(qreal titleBarHeight READ titleBarHeight NOTIFY titleBarHeightChanged FINAL)

public:
    explicit WindowChrome(QObject* parent = nullptr);
    ~WindowChrome() override;

    void attach(QWindow* window);
    // Remeasure traffic-light clearance after native layout is ready.
    Q_INVOKABLE void refreshTitleBarMetrics();
    qreal titleBarLeadingInset() const { return titleBarLeadingInset_; }
    qreal titleBarHeight() const { return titleBarHeight_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void titleBarLeadingInsetChanged();
    void titleBarHeightChanged();

private:
    void extendDwmFrame() const;
    void applyMacOs(QWindow* window);
    void observeMacOsFullScreen(QWindow* window);
    void stopObservingMacOsFullScreen();
    void setTitleBarLeadingInset(qreal inset);
    void setTitleBarHeight(qreal height);

    QPointer<QWindow> window_;
    quintptr nativeHandle_ = 0;
    qreal titleBarLeadingInset_ = 0;
    qreal titleBarHeight_ = 0;
    qreal windowedTitleBarLeadingInset_ = 0;
    qreal windowedTitleBarHeight_ = 0;
    void* macWillEnterFullScreenObserver_ = nullptr;
    void* macDidEnterFullScreenObserver_ = nullptr;
    void* macWillExitFullScreenObserver_ = nullptr;
    void* macDidExitFullScreenObserver_ = nullptr;
};
} // namespace miacode::ui
