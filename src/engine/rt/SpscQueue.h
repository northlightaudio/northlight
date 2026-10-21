#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
#include <vector>

// Fixed-capacity single-producer single-consumer ring. All storage is allocated in
// the constructor; push/pop never allocate, lock or block. T must be trivially
// copyable or at least nothrow move constructible; the queue holds objects by value.

namespace northlight::rt
{

template <typename T> class SpscQueue
{
public:
    explicit SpscQueue(std::size_t capacity) : slots_(capacity + 1) {}

    // Producer thread. Returns false (and drops `value`) when full.
    bool push(T value) noexcept
    {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire))
            return false;
        slots_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer thread. Returns false when empty.
    bool pop(T& out) noexcept
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        out = std::move(slots_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const noexcept
    {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

    std::size_t capacity() const noexcept { return slots_.size() - 1; }

private:
    std::size_t increment(std::size_t i) const noexcept { return i + 1 == slots_.size() ? 0 : i + 1; }

    // Producer and consumer indices live on separate cache lines (explicit padding
    // rather than alignas, which MSVC reports as C4324).
    static constexpr std::size_t cacheLine = 64;
    std::vector<T> slots_;
    std::atomic<std::size_t> head_{0};
    char padding_[cacheLine - sizeof(std::atomic<std::size_t>)]{};
    std::atomic<std::size_t> tail_{0};
};

} // namespace northlight::rt
