#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// MIDI on the real-time path: a fixed-capacity list of timestamped events for one
// block, filled by the renderer (clips, live input, note-offs on stop) and read by
// instruments. Nothing allocates after construction.

namespace aural::engine
{

struct MidiEvent
{
    enum class Type : std::uint8_t
    {
        noteOn,
        noteOff,
        controller, // controller number in `note`, value in `velocity`
        allNotesOff,
    };
    int sampleOffset = 0; // within the block
    Type type = Type::noteOn;
    std::uint8_t channel = 0; // 0..15
    std::uint8_t note = 0;    // 0..127 (or the controller number)
    std::uint8_t velocity = 0;
};

// A MIDI event leaving for the output device: the event with its channel, at
// a time on the engine's sample clock (samples rendered since it started).
struct MidiOutEvent
{
    MidiEvent event;
    std::int64_t clock = 0;
};

class MidiEventList
{
public:
    explicit MidiEventList(size_t capacity = 1024) : events_(capacity) {}

    void clear() noexcept { count_ = 0; }
    bool empty() const noexcept { return count_ == 0; }
    size_t size() const noexcept { return count_; }
    size_t capacity() const noexcept { return events_.size(); }
    const MidiEvent& operator[](size_t i) const noexcept { return events_[i]; }
    const MidiEvent* begin() const noexcept { return events_.data(); }
    const MidiEvent* end() const noexcept { return events_.data() + count_; }

    // False (and the event dropped) when the list is full.
    bool push(const MidiEvent& e) noexcept
    {
        if (count_ >= events_.size())
            return false;
        events_[count_++] = e;
        return true;
    }
    bool noteOn(int offset, int note, int velocity, int channel = 0) noexcept
    {
        return push({offset,
                     MidiEvent::Type::noteOn,
                     static_cast<std::uint8_t>(channel),
                     static_cast<std::uint8_t>(note),
                     static_cast<std::uint8_t>(velocity)});
    }
    bool noteOff(int offset, int note, int channel = 0) noexcept
    {
        return push({offset,
                     MidiEvent::Type::noteOff,
                     static_cast<std::uint8_t>(channel),
                     static_cast<std::uint8_t>(note),
                     0});
    }
    bool allNotesOff(int offset) noexcept { return push({offset, MidiEvent::Type::allNotesOff, 0, 0, 0}); }

    // Orders the events by sample offset (stable), so instruments see them in time.
    void sortByTime() noexcept
    {
        // Insertion sort: lists are short and already nearly ordered.
        for (size_t i = 1; i < count_; ++i)
        {
            const MidiEvent e = events_[i];
            size_t j = i;
            while (j > 0 && events_[j - 1].sampleOffset > e.sampleOffset)
            {
                events_[j] = events_[j - 1];
                --j;
            }
            events_[j] = e;
        }
    }

private:
    std::vector<MidiEvent> events_;
    size_t count_ = 0;
};

} // namespace aural::engine
