#pragma once

#include <QObject>
#include <QtGlobal>

// Single decision surface for v2 window chrome / menu placement.
// QML and QmlUiBootstrap both read this object from applicationContext.
class QmlUiPlatformChrome final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool customTitleBar READ customTitleBar CONSTANT)
    Q_PROPERTY(bool embeddedMenuInTitleBar READ embeddedMenuInTitleBar CONSTANT)
    Q_PROPERTY(bool captionButtons READ captionButtons CONSTANT)
    Q_PROPERTY(bool expandClientArea READ expandClientArea CONSTANT)
    Q_PROPERTY(bool hideBeforeChromeAttach READ hideBeforeChromeAttach CONSTANT)
    Q_PROPERTY(bool attachChromeAfterShow READ attachChromeAfterShow CONSTANT)

public:
    explicit QmlUiPlatformChrome(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    bool customTitleBar() const
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return true;
#else
        return false;
#endif
    }

    bool embeddedMenuInTitleBar() const { return customTitleBar(); }

    bool captionButtons() const
    {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }

    bool expandClientArea() const
    {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }

    bool hideBeforeChromeAttach() const
    {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }

    bool attachChromeAfterShow() const
    {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }
};
