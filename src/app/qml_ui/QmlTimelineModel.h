#pragma once

#include <QObject>
#include <QString>

#include "app/v2/TimelineSurface.h"

#include "app/v2/ShellNotifications.h"


namespace miacode::qml_ui {

// The bottom panel — the timeline surface and the tabs beside it — as one
// object the shell talks to directly.
//
// This is half of what QuickShellController used to be. That object was a
// polling adapter: a QTimer pulled ~25 unrelated values off MainWindow and
// emitted one coarse signal, so a tab change and a playback position arrived
// the same way and every reader woke for both. Here the state is pushed when it
// changes, and preview state lives in QmlPreviewModel rather than in the same
// bag.
//
// It does not own TimelineQuickModel; that migration is deferred. What it owns
// is the shell's side of the panel: which tab is showing, which tabs exist, and
// the navigation gestures the timeline surface reports.
class QmlTimelineModel final : public QObject
{
    Q_OBJECT

    // The render bridge the QML TimelineQuickItem binds to. Constant for the
    // session: MainWindow builds it once at startup.
    Q_PROPERTY(QObject* stateBridge READ stateBridge CONSTANT)

    Q_PROPERTY(QString currentTabId READ currentTabId NOTIFY tabsChanged)
    Q_PROPERTY(bool panelVisible READ panelVisible NOTIFY tabsChanged)
    Q_PROPERTY(bool timelineTabVisible READ timelineTabVisible NOTIFY tabsChanged)
    Q_PROPERTY(bool validationTabVisible READ validationTabVisible NOTIFY tabsChanged)
    Q_PROPERTY(bool muriTabVisible READ muriTabVisible NOTIFY tabsChanged)

    // Localized once per session; the locale does not change at runtime.
    Q_PROPERTY(QString timelineTabLabel READ timelineTabLabel CONSTANT)
    Q_PROPERTY(QString validationTabLabel READ validationTabLabel CONSTANT)
    Q_PROPERTY(QString muriTabLabel READ muriTabLabel CONSTANT)
    Q_PROPERTY(QString followCodeLabel READ followCodeLabel CONSTANT)

public:
    explicit QmlTimelineModel(miacode::v2::ShellNotifications& notifications,
                              miacode::v2::TimelineSurface*& surfaceSlot,
                              QObject* parent = nullptr);

    QObject* stateBridge() const;
    QString currentTabId() const;
    bool panelVisible() const;
    bool timelineTabVisible() const;
    bool validationTabVisible() const;
    bool muriTabVisible() const;
    QString timelineTabLabel() const;
    QString validationTabLabel() const;
    QString muriTabLabel() const;
    QString followCodeLabel() const;

    Q_INVOKABLE void setCurrentTabId(const QString& tabId);
    Q_INVOKABLE void headerNavigate(double second);
    Q_INVOKABLE void wheelNavigate(double second);
    Q_INVOKABLE void centerNavigate(double second);
    Q_INVOKABLE void dragStarted();
    Q_INVOKABLE void dragFinished(double second);
    Q_INVOKABLE void userInteractionStarted();
    Q_INVOKABLE void surfaceReady();
    Q_INVOKABLE void followPreviewToggled(bool enabled);

signals:
    void tabsChanged();

private:
    miacode::v2::ShellNotifications* notifications_ = nullptr;
    // Bound to the assembly's slot, not a snapshot.
    miacode::v2::TimelineSurface** surfaceSlot_ = nullptr;
    miacode::v2::TimelineSurface* surface() const
    {
        return surfaceSlot_ != nullptr ? *surfaceSlot_ : nullptr;
    }
};

}  // namespace miacode::qml_ui
