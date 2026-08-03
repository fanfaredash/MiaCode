#pragma once

#include <QAbstractNativeEventFilter>

class QWindow;

// v2-only Windows client-side frame. Keeps a standard top-level window
// (snap / DWM / taskbar) while WM_NCCALCSIZE expands the client area over
// the native caption so QML WindowTitleBar can draw the chrome.
// Attach only from QmlUiBootstrap — never from QuickShellBootstrap (v1).
class QmlUiWindowChrome final : public QAbstractNativeEventFilter
{
public:
    QmlUiWindowChrome() = default;
    ~QmlUiWindowChrome() override;

    void attach(QWindow* window);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void extendDwmFrame() const;

    quintptr nativeHandle_ = 0;
};
