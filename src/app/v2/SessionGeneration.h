#pragma once

#include <atomic>

#include <QtGlobal>

namespace miacode::v2 {

// Session identities must remain unique across replacement sessions in one
// process. A stale command can otherwise collide with a fresh host that starts
// its local counter at the same value.
inline quint64 nextSessionGeneration()
{
    static std::atomic<quint64> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace miacode::v2
