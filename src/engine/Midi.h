#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// MIDI on the real-time path: a fixed-capacity list of timestamped events for one
// block, filled by the renderer (clips, live input, note-offs on stop) and read by
// instruments. Nothing allocates after construction.

namespace northlight::engine
{

struct MidiEvent
{
    enum class Type : std::uint8_t
    {
        noteOn,
        noteOff,
        controller, // controller number in `note`, value in `velocity`
        allNotesOff,
        pitchBend,       // 14 bits: the low 7 in `note`, the high 7 in `velocity` (8192 = centre)
        channelPressure, // the pressure in `velocity`
        polyPressure,    // the key in `note`, its pressure in `velocity`
    };
    int sampleOffset = 0; // within the block
    Type type = Type::noteOn;
    std::uint8_t channel = 0; // 0..15
    std::uint8_t note = 0;    // 0..127 (or the controller number)
    std::uint8_t velocity = 0;

    bool isNote() const noexcept { return type == Type::noteOn || type == Type::noteOff; }
    // A pitch bend's value, -8192..8191 (0 = centre).
    int bendValue() const noexcept { return ((velocity & 127) << 7 | (note & 127)) - 8192; }
    // The event as MIDI 1.0 bytes; returns how many (2 or 3).
    int toBytes(std::uint8_t bytes[3]) const noexcept
    {
        const auto ch = static_cast<std::uint8_t>(channel & 15);
        bytes[1] = static_cast<std::uint8_t>(note & 127);
        bytes[2] = static_cast<std::uint8_t>(velocity & 127);
        switch (type)
        {
            case Type::noteOn:
                bytes[0] = static_cast<std::uint8_t>(0x90 | ch);
                return 3;
            case Type::noteOff:
                bytes[0] = static_cast<std::uint8_t>(0x80 | ch);
                return 3;
            case Type::controller:
                bytes[0] = static_cast<std::uint8_t>(0xB0 | ch);
                return 3;
            case Type::allNotesOff:
                bytes[0] = static_cast<std::uint8_t>(0xB0 | ch);
                bytes[1] = 123;
                bytes[2] = 0;
                return 3;
            case Type::pitchBend:
                bytes[0] = static_cast<std::uint8_t>(0xE0 | ch);
                return 3;
            case Type::channelPressure:
                bytes[0] = static_cast<std::uint8_t>(0xD0 | ch);
                bytes[1] = static_cast<std::uint8_t>(velocity & 127);
                return 2;
            case Type::polyPressure:
                bytes[0] = static_cast<std::uint8_t>(0xA0 | ch);
                return 3;
        }
        return 0;
    }
};

// The controllers the engine chases after a jump, by key: 0..127 a control
// change, then pitch bend and channel pressure. (Poly pressure belongs to a
// held key, so it is not chased.)
constexpr int chaseKeyCount = 130;
constexpr int chaseKeyBend = 128;
constexpr int chaseKeyPressure = 129;

// What a chased controller returns to when the song has no value for it before
// the play position (and on stop): the centre for pitch bend, off for pressure,
// the mod wheel and the pedals, full for expression. -1: left as it is (a
// volume or pan has no safe default to assume).
constexpr int chaseRestValue(int key) noexcept
{
    switch (key)
    {
        case chaseKeyBend:
        case chaseKeyPressure:
        case 1:  // mod wheel
        case 64: // sustain
        case 65: // portamento
        case 66: // sostenuto
        case 67: // soft
        case 68: // legato
        case 69: // hold 2
            return 0;
        case 11: // expression
            return 127;
        default:
            return -1;
    }
}

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
    bool controller(int offset, int number, int value, int channel = 0) noexcept
    {
        return push({offset,
                     MidiEvent::Type::controller,
                     static_cast<std::uint8_t>(channel),
                     static_cast<std::uint8_t>(number & 127),
                     static_cast<std::uint8_t>(value & 127)});
    }
    // `value` -8192..8191.
    bool pitchBend(int offset, int value, int channel = 0) noexcept
    {
        const int raw = value < -8192 ? 0 : value > 8191 ? 16383 : value + 8192;
        return push({offset,
                     MidiEvent::Type::pitchBend,
                     static_cast<std::uint8_t>(channel),
                     static_cast<std::uint8_t>(raw & 127),
                     static_cast<std::uint8_t>(raw >> 7)});
    }
    bool channelPressure(int offset, int value, int channel = 0) noexcept
    {
        return push({offset,
                     MidiEvent::Type::channelPressure,
                     static_cast<std::uint8_t>(channel),
                     0,
                     static_cast<std::uint8_t>(value & 127)});
    }
    bool polyPressure(int offset, int note, int value, int channel = 0) noexcept
    {
        return push({offset,
                     MidiEvent::Type::polyPressure,
                     static_cast<std::uint8_t>(channel),
                     static_cast<std::uint8_t>(note & 127),
                     static_cast<std::uint8_t>(value & 127)});
    }

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

} // namespace northlight::engine
