#include "tools/cover_export/CoverCompositionPersistenceGuard.h"

#include <utility>

namespace miacode::cover_export {

CoverCompositionPersistenceGuard::CoverCompositionPersistenceGuard(Supplier supplier, Saver saver)
    : supplier_(std::move(supplier))
    , saver_(std::move(saver))
{
}

CoverCompositionPersistenceGuard::~CoverCompositionPersistenceGuard()
{
    persistNow();
}

bool CoverCompositionPersistenceGuard::persistNow()
{
    if (!supplier_ || !saver_) {
        return false;
    }
    const QJsonObject payload = supplier_();
    if (hasSuccessfulPayload_ && payload == lastSuccessfulPayload_) {
        return true;
    }
    if (!saver_(payload)) {
        return false;
    }
    lastSuccessfulPayload_ = payload;
    hasSuccessfulPayload_ = true;
    return true;
}

} // namespace miacode::cover_export
