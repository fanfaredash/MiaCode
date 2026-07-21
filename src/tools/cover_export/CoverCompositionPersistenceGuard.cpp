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
        // Disarmed (or never configured): nothing to persist, and — crucially — we
        // must NOT invoke the supplier, which reads live widgets. Treat as success.
        return true;
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

void CoverCompositionPersistenceGuard::disarm()
{
    supplier_ = nullptr;
    saver_ = nullptr;
}

} // namespace miacode::cover_export
