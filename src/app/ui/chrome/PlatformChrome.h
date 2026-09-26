#pragma once

#include <QObject>
#include <QtGlobal>

// Single decision surface for v2 window chrome / menu placement.
// QML and Bootstrap both read this object from applicationContext.
namespace miacode::ui {

class PlatformChrome final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool customTitleBar READ customTitleBar CONSTANT)
    Q_PROPERTY(bool nativeMenuBar READ nativeMenuBar CONSTANT)
    Q_PROPERTY(bool embeddedMenuInTitleBar READ embeddedMenuInTitleBar CONSTANT)
    Q_PROPERTY(bool captionButtons READ captionButtons CONSTANT)
    Q_PROPERTY(bool expandClientArea READ expandClientArea CONSTANT)
    Q_PROPERTY(bool hideBeforeChromeAttach READ hideBeforeChromeAttach CONSTANT)
    Q_PROPERTY(bool attachChromeAfterShow READ attachChromeAfterShow CONSTANT)

public:
    explicit PlatformChrome(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    bool customTitleBar() const
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
        return true;
#else
        return false;
#endif
    }

    bool nativeMenuBar() const
    {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }

    bool embeddedMenuInTitleBar() const { return customTitleBar() && !nativeMenuBar(); }

    bool captionButtons() const
    {
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        return true;
#else
        return false;
#endif
    }

    bool expandClientArea() const
    {
        return false;
    }

    bool hideBeforeChromeAttach() const
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return true;
#else
        return false;
#endif
    }

    bool attachChromeAfterShow() const
    {
        return false;
    }
};
} // namespace miacode::ui
