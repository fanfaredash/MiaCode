#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

#include <QString>
#include <QtGlobal>

namespace miacode::preview_audio::bass {

enum class PlayedSfxKind : quint8 {
    Answer,
    Judge,
    JudgeBreak,
    Slide,
    Break,
    BreakSlideStart,
    BreakSlideFinish,
    BreakSlideTailBreak,
    JudgeBreakSlide,
    Ex,
    Touch,
    BreakTouch,
    BreakSlide,
    Firework,
    Count,
};

inline constexpr std::size_t kPlayedSfxKindCount =
    static_cast<std::size_t>(PlayedSfxKind::Count);

inline int playedSfxKindIndex(const QString& kind)
{
    if (kind == QLatin1String("answer")) return static_cast<int>(PlayedSfxKind::Answer);
    if (kind == QLatin1String("judge")) return static_cast<int>(PlayedSfxKind::Judge);
    if (kind == QLatin1String("judge_break")) return static_cast<int>(PlayedSfxKind::JudgeBreak);
    if (kind == QLatin1String("slide")) return static_cast<int>(PlayedSfxKind::Slide);
    if (kind == QLatin1String("break")) return static_cast<int>(PlayedSfxKind::Break);
    if (kind == QLatin1String("break_slide_start")) return static_cast<int>(PlayedSfxKind::BreakSlideStart);
    if (kind == QLatin1String("break_slide_finish")) return static_cast<int>(PlayedSfxKind::BreakSlideFinish);
    if (kind == QLatin1String("break_slide_tail_break")) return static_cast<int>(PlayedSfxKind::BreakSlideTailBreak);
    if (kind == QLatin1String("judge_break_slide")) return static_cast<int>(PlayedSfxKind::JudgeBreakSlide);
    if (kind == QLatin1String("ex")) return static_cast<int>(PlayedSfxKind::Ex);
    if (kind == QLatin1String("touch")) return static_cast<int>(PlayedSfxKind::Touch);
    if (kind == QLatin1String("break_touch")) return static_cast<int>(PlayedSfxKind::BreakTouch);
    if (kind == QLatin1String("break_slide")) return static_cast<int>(PlayedSfxKind::BreakSlide);
    if (kind == QLatin1String("firework")) return static_cast<int>(PlayedSfxKind::Firework);
    return -1;
}

inline const char* playedSfxKindName(std::size_t index)
{
    static constexpr const char* kNames[kPlayedSfxKindCount] = {
        "answer", "judge", "judge_break", "slide", "break",
        "break_slide_start", "break_slide_finish", "break_slide_tail_break",
        "judge_break_slide", "ex", "touch", "break_touch", "break_slide",
        "firework",
    };
    return index < kPlayedSfxKindCount ? kNames[index] : "unknown";
}

struct PlayedSfxSnapshot {
    std::array<float, kPlayedSfxKindCount> gains{};
    quint32 mask = 0;

    void record(const QString& kind, double gain)
    {
        const int index = playedSfxKindIndex(kind);
        if (index < 0) {
            return;
        }
        mask |= quint32(1) << static_cast<quint32>(index);
        gains[static_cast<std::size_t>(index)] = static_cast<float>(gain);
    }
};

enum class SfxCallbackEventKind : quint8 {
    None,
    Trigger,
    Drop,
    Deferred,
};

enum class SfxCallbackDropReason : quint8 {
    None,
    Inactive,
    StaleHandle,
};

struct SfxCallbackEvent {
    SfxCallbackEventKind kind = SfxCallbackEventKind::None;
    SfxCallbackDropReason dropReason = SfxCallbackDropReason::None;
    quint32 handle = 0;
    quint32 expectedHandle = 0;
    int groupIndex = -1;
    double groupSecond = 0.0;
    quint64 triggeredCount = 0;
    bool startedBackground = false;
    bool processedAfterContention = false;
    PlayedSfxSnapshot played;
    bool touchholdChanged = false;
    int touchholdOwner = -1;
    int touchholdPreviousOwner = -1;
    double touchholdSecond = 0.0;
    double touchholdSpanStartSecond = -1.0;
    bool armFailurePending = false;
    int armFailureBassError = 0;
    double armFailureTargetChartSecond = 0.0;
    int callbackBassError = 0;
};

static_assert(std::is_trivially_copyable_v<SfxCallbackEvent>);

class SfxCallbackEventRing
{
public:
    static constexpr std::size_t kCapacity = 256;

    bool tryPush(const SfxCallbackEvent& event)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t nextHead = advance(head);
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            droppedCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events_[head] = event;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    bool tryPop(SfxCallbackEvent* out)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        *out = events_[tail];
        tail_.store(advance(tail), std::memory_order_release);
        return true;
    }

    quint64 takeDroppedCount()
    {
        return droppedCount_.exchange(0, std::memory_order_relaxed);
    }

    void reset()
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        droppedCount_.store(0, std::memory_order_relaxed);
    }

private:
    static std::size_t advance(std::size_t index)
    {
        const std::size_t next = index + 1;
        return next == kCapacity ? 0 : next;
    }

    std::array<SfxCallbackEvent, kCapacity> events_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<quint64> droppedCount_{0};
};

}  // namespace miacode::preview_audio::bass
