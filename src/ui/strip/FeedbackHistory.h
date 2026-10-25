#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace northlight::ui::strip
{

// The newest frames a display has read from its processor (feedback frames,
// telemetry), in a ring of a fixed number: oldest first, with the count of every
// frame received so a display can group frames by their absolute number and
// keep its columns in place as the ring scrolls. Message thread only.
template <class Frame> class FeedbackHistory
{
public:
    explicit FeedbackHistory(int capacity) : ring_(static_cast<size_t>(std::max(1, capacity))) {}

    void push(const Frame& frame)
    {
        ring_[static_cast<size_t>(head_)] = frame;
        head_ = (head_ + 1) % capacity();
        count_ = std::min(capacity(), count_ + 1);
        ++total_;
    }

    int capacity() const noexcept { return static_cast<int>(ring_.size()); }
    // The frames held, at most capacity().
    int size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    // Frames received since the history was made (the newest is number total - 1).
    std::uint64_t total() const noexcept { return total_; }
    // Frame `i` of size(), oldest first.
    const Frame& at(int i) const noexcept
    {
        return ring_[static_cast<size_t>((head_ - count_ + i + capacity()) % capacity())];
    }
    const Frame& newest() const noexcept { return at(count_ - 1); }

private:
    std::vector<Frame> ring_;
    int head_ = 0, count_ = 0;
    std::uint64_t total_ = 0;
};

} // namespace northlight::ui::strip
