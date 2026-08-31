#include "ChartDropImportService.h"

#include <utility>

namespace miacode::v2 {

ChartDropImportService::ChartDropImportService(QObject* parent)
    : QObject(parent)
{
}

bool ChartDropImportService::isCurrent(quint64 requestId, quint64 generation) const
{
    return pending_ && !released_ && requestId_ == requestId && generation_ == generation;
}

void ChartDropImportService::complete(const ChartDropImportResult& result)
{
    if (!pending_ || released_) {
        return;
    }
    ChartDropImportResult finished = result;
    finished.requestId = requestId_;
    finished.generation = generation_;
    finished.completed = true;
    Completion completion = std::move(completion_);
    pending_ = false;
    phase_ = Phase::None;
    candidates_.clear();
    adapter_ = {};
    result_ = {};
    if (completion) {
        completion(finished);
    }
}

void ChartDropImportService::completeCancelled()
{
    result_.accepted = true;
    result_.cancelled = true;
    complete(result_);
}

bool ChartDropImportService::submit(const QStringList& paths,
                                    quint64 requestId,
                                    quint64 generation,
                                    DocumentImportAdapter adapter,
                                    Completion completion)
{
    if (pending_ || released_) {
        return false;
    }

    if (paths.isEmpty()) {
        if (completion) {
            completion(ChartDropImportResult{
                requestId,
                generation,
                false,
                true,
                false,
                0,
                0,
                {},
            });
        }
        return true;
    }

    pending_ = true;
    requestId_ = requestId;
    generation_ = generation;
    adapter_ = std::move(adapter);
    completion_ = std::move(completion);
    result_ = {};
    result_.requestId = requestId;
    result_.generation = generation;
    result_.accepted = true;

    if (!adapter_.validate) {
        result_.failedCount = paths.size();
        complete(result_);
        return true;
    }

    QString validationError;
    candidates_ = adapter_.validate(paths, &validationError);
    if (candidates_.isEmpty()) {
        // No regular supported path is not an accepted import request. A
        // document adapter error, on the other hand, is an accepted request
        // that failed before chart creation.
        if (validationError.trimmed().isEmpty()) {
            result_.accepted = false;
        } else {
            result_.failedCount = paths.size();
        }
        complete(result_);
        return true;
    }

    if (!adapter_.requestFirstConfirmation) {
        completeCancelled();
        return true;
    }

    phase_ = Phase::FirstConfirmation;
    const quint64 callbackRequestId = requestId_;
    const quint64 callbackGeneration = generation_;
    adapter_.requestFirstConfirmation(
        candidates_,
        [this, callbackRequestId, callbackGeneration](bool accepted) {
            if (!isCurrent(callbackRequestId, callbackGeneration) || phase_ != Phase::FirstConfirmation) {
                return;
            }
            phase_ = Phase::None;
            if (!accepted) {
                completeCancelled();
                return;
            }
            beginLeaveDocument();
        });
    return true;
}

void ChartDropImportService::beginLeaveDocument()
{
    if (!pending_ || released_) {
        return;
    }
    if (!adapter_.requestLeaveDocument) {
        completeCancelled();
        return;
    }
    phase_ = Phase::LeaveDocument;
    const quint64 callbackRequestId = requestId_;
    const quint64 callbackGeneration = generation_;
    adapter_.requestLeaveDocument([this, callbackRequestId, callbackGeneration](bool mayLeave) {
        if (!isCurrent(callbackRequestId, callbackGeneration) || phase_ != Phase::LeaveDocument) {
            return;
        }
        phase_ = Phase::None;
        if (!mayLeave) {
            completeCancelled();
            return;
        }
        beginCreateCharts();
    });
}

void ChartDropImportService::beginCreateCharts()
{
    if (!pending_ || released_) {
        return;
    }
    if (!adapter_.createCharts) {
        result_.failedCount = candidates_.size();
        complete(result_);
        return;
    }
    phase_ = Phase::CreateCharts;
    const quint64 callbackRequestId = requestId_;
    const quint64 callbackGeneration = generation_;
    adapter_.createCharts(
        candidates_,
        [this, callbackRequestId, callbackGeneration](const ChartDropCreateResult& result) {
            if (!isCurrent(callbackRequestId, callbackGeneration) || phase_ != Phase::CreateCharts) {
                return;
            }
            phase_ = Phase::None;
            beginFinalSwitch(result);
        });
}

void ChartDropImportService::beginFinalSwitch(const ChartDropCreateResult& result)
{
    if (!pending_ || released_) {
        return;
    }
    result_.createdCount = qMax(0, result.createdCount);
    result_.failedCount = qMax(0, result.failedCount);
    result_.targetPath = result.targetPath;
    if (result_.createdCount == 0) {
        complete(result_);
        return;
    }

    if (candidates_.size() == 1 && result_.createdCount == 1 && result_.failedCount == 0
        && !result_.targetPath.isEmpty() && adapter_.requestFinalSwitch) {
        phase_ = Phase::FinalSwitch;
        const quint64 callbackRequestId = requestId_;
        const quint64 callbackGeneration = generation_;
        adapter_.requestFinalSwitch(
            result_.targetPath,
            [this, callbackRequestId, callbackGeneration](bool accepted) {
                if (!isCurrent(callbackRequestId, callbackGeneration) || phase_ != Phase::FinalSwitch) {
                    return;
                }
                phase_ = Phase::None;
                result_.cancelled = !accepted;
                complete(result_);
            });
        return;
    }

    complete(result_);
}

void ChartDropImportService::release()
{
    if (released_) {
        return;
    }
    released_ = true;
    ++generation_;
    pending_ = false;
    phase_ = Phase::None;
    candidates_.clear();
    adapter_ = {};
    completion_ = {};
    result_ = {};
}

} // namespace miacode::v2
