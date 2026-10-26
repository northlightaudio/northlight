#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

// Fixed-capacity multi-producer single-consumer queue (Vyukov's bounded queue:
// each cell carries a sequence number that says whose turn it is). All storage is
// allocated in the constructor; push and pop never allocate, lock or wait: a
// producer that finds the queue full drops its item, and a consumer that finds the
// next cell still being written (another producer is between claiming it and
// publishing it) reports the queue empty for now. Any number of threads may push
// at once, the real-time thread among them; one thread pops. The capacity is
// rounded up to a power of two. T must be nothrow default constructible and
// assignable.

namespace northlight::rt
{

template <typename T> class MpscQueue
{
public:
    explicit MpscQueue(std::size_t capacity) : size_(roundUp(capacity)), cells_(new Cell[size_])
    {
        for (std::size_t i = 0; i < size_; ++i)
            cells_[i].sequence.store(i, std::memory_order_relaxed);
    }
    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;

    // Any thread. Returns false (and drops `value`) when full.
    bool push(T value) noexcept
    {
        auto at = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            Cell& cell = cells_[at & (size_ - 1)];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(at);
            if (difference == 0)
            {
                // The cell is free for this position: claim the position.
                if (head_.compare_exchange_weak(at, at + 1, std::memory_order_relaxed))
                {
                    cell.value = std::move(value);
                    cell.sequence.store(at + 1, std::memory_order_release);
                    return true;
                }
                // Another producer took it; `at` now holds the new head.
            }
            else if (difference < 0)
                return false; // full: the consumer has not freed this cell yet
            else
                at = head_.load(std::memory_order_relaxed);
        }
    }

    // The one consumer thread. Returns false when empty (or when the next item is
    // still being written).
    bool pop(T& out) noexcept
    {
        Cell& cell = cells_[tail_ & (size_ - 1)];
        if (cell.sequence.load(std::memory_order_acquire) != tail_ + 1)
            return false;
        out = std::move(cell.value);
        cell.sequence.store(tail_ + size_, std::memory_order_release);
        ++tail_;
        return true;
    }

    std::size_t capacity() const noexcept { return size_; }

private:
    struct Cell
    {
        std::atomic<std::size_t> sequence{0};
        T value{};
    };

    static std::size_t roundUp(std::size_t n) noexcept
    {
        std::size_t size = 2;
        while (size < n)
            size *= 2;
        return size;
    }

    // The producers' index and the consumer's live on separate cache lines
    // (explicit padding rather than alignas, which MSVC reports as C4324).
    static constexpr std::size_t cacheLine = 64;
    const std::size_t size_;
    const std::unique_ptr<Cell[]> cells_;
    std::atomic<std::size_t> head_{0};
    char padding_[cacheLine - sizeof(std::atomic<std::size_t>)]{};
    std::size_t tail_ = 0; // the consumer's alone
};

} // namespace northlight::rt
