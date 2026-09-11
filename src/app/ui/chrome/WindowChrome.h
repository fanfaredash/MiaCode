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

public:
    explicit WindowChrome(QObject* parent = nullptr);
    ~WindowChrome() override;

    void attach(QWindow* window);
    // Remeasure traffic-light clearance after native layout is ready.
    Q_INVOKABLE void refreshTitleBarMetrics();
    qreal titleBarLeadingInset() const { return titleBarLeadingInset_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void titleBarLeadingInsetChanged();

private:
    void extendDwmFrame() const;
    void applyMacOs(QWindow* window);
    void setTitleBarLeadingInset(qreal inset);
    void handleWindowVisibleChanged(bool visible);

    QPointer<QWindow> window_;
    quintptr nativeHandle_ = 0;
    qreal titleBarLeadingInset_ = 0;
};
} // namespace miacode::ui
