#pragma once

#include <utility>

namespace miacode::timeline {

// Applies a completed slow-analysis result to its three consumer-facing state
// groups and notifies observers.  The call order is intentionally centralized
// so a validation notification cannot expose a partly published Muri snapshot.
template <typename PublishValidation, typename PublishMuri,
          typename PublishStaticReferences, typename NotifyObservers>
void publishTimelineAnalysisState(
    PublishValidation&& publishValidation,
    PublishMuri&& publishMuri,
    PublishStaticReferences&& publishStaticReferences,
    NotifyObservers&& notifyObservers)
{
    std::forward<PublishValidation>(publishValidation)();
    std::forward<PublishMuri>(publishMuri)();
    std::forward<PublishStaticReferences>(publishStaticReferences)();
    std::forward<NotifyObservers>(notifyObservers)();
}

}  // namespace miacode::timeline
