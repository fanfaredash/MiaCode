#include "JobProgressService.h"

#include <algorithm>

namespace miacode::v2 {

JobProgressService::JobProgressService(QObject* parent)
    : QObject(parent)
{
}

void JobProgressService::begin(const QString& title, const QString& label, bool cancellable)
{
    active_ = true;
    cancellable_ = cancellable;
    // A new job always starts uncancelled: a stale flag from the previous job
    // would abort this one at its first checkpoint.
    cancelRequested_ = false;
    percent_ = 0;
    title_ = title;
    label_ = label;
    emit changed();
}

void JobProgressService::report(int percent, const QString& label)
{
    if (!active_) {
        return;
    }
    const int clamped = std::clamp(percent, 0, 100);
    if (clamped == percent_ && label == label_) {
        return;
    }
    percent_ = clamped;
    label_ = label;
    emit changed();
}

void JobProgressService::end()
{
    if (!active_) {
        return;
    }
    active_ = false;
    cancellable_ = false;
    cancelRequested_ = false;
    percent_ = 0;
    title_.clear();
    label_.clear();
    emit changed();
}

void JobProgressService::requestCancel()
{
    if (!active_ || !cancellable_ || cancelRequested_) {
        return;
    }
    cancelRequested_ = true;
    emit changed();
}

}  // namespace miacode::v2
