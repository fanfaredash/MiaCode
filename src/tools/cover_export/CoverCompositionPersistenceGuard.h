#pragma once

#include <QJsonObject>

#include <functional>

namespace miacode::cover_export {

class CoverCompositionPersistenceGuard
{
public:
    using Supplier = std::function<QJsonObject()>;
    using Saver = std::function<bool(const QJsonObject&)>;

    CoverCompositionPersistenceGuard(Supplier supplier, Saver saver);
    ~CoverCompositionPersistenceGuard();

    bool persistNow();
    // Drop the supplier/saver so no further persistNow() (including the one in
    // ~CoverCompositionPersistenceGuard) reads the source widgets. Call this once
    // the final state has been saved while those widgets are still alive — it makes
    // the later teardown-time persistNow() a safe no-op instead of a use-after-free.
    void disarm();

private:
    Supplier supplier_;
    Saver saver_;
    QJsonObject lastSuccessfulPayload_;
    bool hasSuccessfulPayload_ = false;
};

} // namespace miacode::cover_export
