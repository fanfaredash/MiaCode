#pragma once

#include <QString>

#include <QtGlobal>

class QObject;

namespace miacode::hang_watchdog {

void installGuiHeartbeat(QObject* owner);
void setPhase(const char* phase, const QString& detail = QString());
void clearPhase(const char* phase = nullptr);

class PhaseScope {
public:
    explicit PhaseScope(const char* phase, QString detail = QString()) noexcept;
    ~PhaseScope() noexcept;

    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
    PhaseScope(PhaseScope&&) = delete;
    PhaseScope& operator=(PhaseScope&&) = delete;

private:
    const char* phase_ = nullptr;
    bool armed_ = false;
    bool previousActive_ = false;
    QString previousPhase_;
    QString previousDetail_;
    qint64 previousStartMs_ = 0;
    quint64 previousGeneration_ = 0;
    quint64 generation_ = 0;
};

}  // namespace miacode::hang_watchdog

#define MIACODE_HANG_JOIN_IMPL(a, b) a##b
#define MIACODE_HANG_JOIN(a, b) MIACODE_HANG_JOIN_IMPL(a, b)
#define MIACODE_HANG_PHASE(name, detail) \
    ::miacode::hang_watchdog::PhaseScope MIACODE_HANG_JOIN(_miacode_hang_phase_, __LINE__)(name, detail)
