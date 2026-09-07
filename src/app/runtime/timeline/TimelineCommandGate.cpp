#include "TimelineCommandGate.h"

namespace miacode::runtime {

TimelineCommandGate::TimelineCommandGate(quint64 sessionGeneration)
    : sessionGeneration_(sessionGeneration != 0
                             ? sessionGeneration
                             : miacode::v2::nextSessionGeneration())
{
}

TimelineCommandStamp TimelineCommandGate::issue()
{
    return {sessionGeneration_, documentRevision_, ++commandSequence_};
}

bool TimelineCommandGate::accept(const TimelineCommandStamp& stamp)
{
    if (!accepts(stamp)) {
        return false;
    }
    lastAcceptedSequence_ = stamp.commandSequence;
    return true;
}

bool TimelineCommandGate::accepts(const TimelineCommandStamp& stamp) const
{
    return stamp.sessionGeneration == sessionGeneration_
        && stamp.documentRevision == documentRevision_
        && stamp.commandSequence != 0
        && stamp.commandSequence <= commandSequence_
        && stamp.commandSequence > lastAcceptedSequence_;
}

void TimelineCommandGate::setDocumentRevision(quint64 revision)
{
    documentRevision_ = revision;
}

void TimelineCommandGate::invalidateSession()
{
    ++sessionGeneration_;
    ++commandSequence_;
    lastAcceptedSequence_ = commandSequence_;
}

}  // namespace miacode::runtime
