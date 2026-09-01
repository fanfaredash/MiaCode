#pragma once

#include "app/runtime/timeline/TimelineCommandGate.h"
#include "app/v2/TimelineSurface.h"

namespace miacode::runtime {

// Transitional Timeline projection host. It owns the command gate and the
// TimelineSurface boundary while the implementation still lives in the
// composite PlaybackCoordinator. It owns no playhead, timer, QSG scene or document.
class TimelineHost final : public miacode::v2::TimelineSurface
{
public:
    explicit TimelineHost(miacode::v2::TimelineSurface& legacySurface,
                          quint64 sessionGeneration = 1);

    void setDocumentRevision(quint64 revision);
    void invalidateSession();
    TimelineCommandStamp issueCommandStamp() override;
    bool acceptsCommand(const TimelineCommandStamp& stamp) const;

    void noteTimelineSurfaceReady(const TimelineCommandStamp& stamp) override;
    QObject* timelineStateBridge() const override;
    void noteTimelineSurfaceReady() override;
    void navigateToSecond(const TimelineCommandStamp& stamp, double second) override;
    void navigateToSecond(double second) override;
    void centerOnSecond(const TimelineCommandStamp& stamp, double second) override;
    void centerOnSecond(double second) override;
    void wheelNavigateToSecond(const TimelineCommandStamp& stamp, double second) override;
    void wheelNavigateToSecond(double second) override;
    void timelineDragStarted(const TimelineCommandStamp& stamp) override;
    void timelineDragStarted() override;
    void timelineDragFinished(const TimelineCommandStamp& stamp, double second) override;
    void timelineDragFinished(double second) override;
    void timelineUserInteractionStarted(const TimelineCommandStamp& stamp) override;
    void timelineUserInteractionStarted() override;
    void setFollowPreviewEnabled(const TimelineCommandStamp& stamp, bool enabled) override;
    void setFollowPreviewEnabled(bool enabled) override;
    QString bottomTabsCurrentTabId() const override;
    void setBottomTabsCurrentTabId(const TimelineCommandStamp& stamp,
                                   const QString& tabId) override;
    void setBottomTabsCurrentTabId(const QString& tabId) override;
    bool bottomTabsVisible() const override;
    bool timelineTabVisible() const override;
    bool muriTabVisible() const override;
    bool validationTabVisible() const override;
    bool ignoreMuriIssuePrompts() const override;

private:
    bool acceptCommand(const TimelineCommandStamp& stamp);

    miacode::v2::TimelineSurface* legacySurface_ = nullptr;
    TimelineCommandGate commandGate_;
};

}  // namespace miacode::runtime
