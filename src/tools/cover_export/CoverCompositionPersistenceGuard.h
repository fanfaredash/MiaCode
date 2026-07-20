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

private:
    Supplier supplier_;
    Saver saver_;
    QJsonObject lastSuccessfulPayload_;
    bool hasSuccessfulPayload_ = false;
};

} // namespace miacode::cover_export
