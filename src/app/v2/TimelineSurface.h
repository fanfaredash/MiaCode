#pragma once

#include <QObject>
#include <QString>

namespace miacode::v2 {

// The bottom timeline and the tab strip beneath it.
//
// Stage 3.5 item 2. QmlTimelineModel projects this into QML; the window still
// owns the QSG timeline surface, the tab visibility rules and the navigation
// that keeps the editor caret, the timeline and the preview playhead agreeing.
//
// The `shell*` names on the window date from the v1 QuickShell controller.
// They are not carried over: the QML layer names the operations, not their
// history.
//
// Deliberately Qt Widgets-free — the one object that crosses the boundary is
// the timeline state bridge, handed over as a plain QObject* for QML.
class TimelineSurface
{
public:
    virtual ~TimelineSurface() = default;

    // The QSG timeline's state bridge, for QML to bind against. Null until the
    // surface exists.
    virtual QObject* timelineStateBridge() const = 0;
    virtual bool timelineSurfaceReady() const = 0;

    // Three navigation entry points rather than one, because they differ in
    // what they do to the view: `navigateToSecond` is a plain jump, `centerOn`
    // also recentres the viewport, and `wheelNavigate` is the continuous
    // gesture that must not fight the follow-preview mode.
    virtual void navigateToSecond(double second) = 0;
    virtual void centerOnSecond(double second) = 0;
    virtual void wheelNavigateToSecond(double second) = 0;

    // Drag lifecycle. Starting a drag suspends follow-preview; finishing it
    // commits the landing position.
    virtual void timelineDragStarted() = 0;
    virtual void timelineDragFinished(double second) = 0;
    // Any user interaction on the surface, which is what drops follow mode.
    virtual void timelineUserInteractionStarted() = 0;
    virtual void setFollowPreviewEnabled(bool enabled) = 0;

    // Bottom tab strip: which tab is showing, and which tabs exist at all —
    // the timeline, 无理 and 校验 tabs each appear only when their content does.
    virtual QString bottomTabsCurrentTabId() const = 0;
    virtual void setBottomTabsCurrentTabId(const QString& tabId) = 0;
    virtual bool bottomTabsVisible() const = 0;
    virtual bool timelineTabVisible() const = 0;
    virtual bool muriTabVisible() const = 0;
    virtual bool validationTabVisible() const = 0;

    // 无理 prompt preference: when set, an analysis result never interrupts.
    virtual bool ignoreMuriIssuePrompts() const = 0;

protected:
    TimelineSurface() = default;
    TimelineSurface(const TimelineSurface&) = default;
    TimelineSurface& operator=(const TimelineSurface&) = default;
};

}  // namespace miacode::v2
