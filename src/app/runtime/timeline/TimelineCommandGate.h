#pragma once

#include "app/v2/SessionGeneration.h"
#include "app/v2/TimelineSurface.h"

namespace miacode::runtime {

using TimelineCommandStamp = miacode::v2::TimelineCommandStamp;

// Serializes Timeline-originated writes at the host boundary. A command is
// valid only while it belongs to the current runtime generation/revision and
// is the newest command issued by this gate.
class TimelineCommandGate final
{
public:
    explicit TimelineCommandGate(quint64 sessionGeneration = 0);

    TimelineCommandStamp issue();
    bool accept(const TimelineCommandStamp& stamp);
    bool accepts(const TimelineCommandStamp& stamp) const;

    void setDocumentRevision(quint64 revision);
    void invalidateSession();

private:
    quint64 sessionGeneration_ = 1;
    quint64 documentRevision_ = 0;
    quint64 commandSequence_ = 0;
    quint64 lastAcceptedSequence_ = 0;
};

}  // namespace miacode::runtime
