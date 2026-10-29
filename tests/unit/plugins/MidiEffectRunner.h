#pragma once

#include "engine/Insert.h"
#include "engine/Midi.h"

#include <cstdint>
#include <memory>
#include <vector>

// Drives a MIDI effect block by block for the MIDI effect tests, keeping the
// play head moving and stamping what comes out with its absolute sample time.

namespace northlight::test
{

struct TimedEvent
{
    std::int64_t at = 0;
    engine::MidiEvent event;
    bool isOn(int note) const noexcept
    {
        return event.type == engine::MidiEvent::Type::noteOn && event.velocity > 0 && event.note == note;
    }
    bool isOff(int note) const noexcept
    {
        return event.type == engine::MidiEvent::Type::noteOff && event.note == note;
    }
    bool isNoteOn() const noexcept
    {
        return event.type == engine::MidiEvent::Type::noteOn && event.velocity > 0;
    }
};

inline engine::MidiEvent noteOn(int offset, int note, int velocity = 100, int channel = 0)
{
    return {offset,
            engine::MidiEvent::Type::noteOn,
            static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity)};
}
inline engine::MidiEvent noteOff(int offset, int note, int channel = 0)
{
    return {offset,
            engine::MidiEvent::Type::noteOff,
            static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(note),
            0};
}
inline engine::MidiEvent controller(int offset, int number, int value)
{
    return {offset,
            engine::MidiEvent::Type::controller,
            0,
            static_cast<std::uint8_t>(number),
            static_cast<std::uint8_t>(value)};
}

class EffectRunner
{
public:
    explicit EffectRunner(std::unique_ptr<engine::InsertProcessor> effect, int blockSize = 512)
        : effect_(std::move(effect)), blockSize_(blockSize)
    {
        head.sampleRate = 48000.0;
        head.tempoBpm = 120.0; // a quarter is 24000 samples
        effect_->prepare(head.sampleRate, blockSize_);
    }

    engine::InsertProcessor& effect() noexcept { return *effect_; }
    std::int64_t now() const noexcept { return clock_; }

    // One block with these events (offsets within it); returns what came out.
    std::vector<TimedEvent> block(const std::vector<engine::MidiEvent>& events = {})
    {
        in_.clear();
        out_.clear();
        for (const auto& e : events)
            in_.push(e);
        effect_->setPlayHead(head);
        effect_->processMidiEffect(in_, out_, blockSize_);
        std::vector<TimedEvent> result;
        for (const auto& e : out_)
            result.push_back({clock_ + e.sampleOffset, e});
        clock_ += blockSize_;
        if (head.playing)
        {
            head.samples += blockSize_;
            head.quarterNotes += blockSize_ * head.quartersPerSample();
        }
        return result;
    }
    // Runs until the clock reaches `until` (whole blocks), events placed at
    // their absolute times; returns everything that came out.
    std::vector<TimedEvent> runUntil(std::int64_t until, std::vector<TimedEvent> events = {})
    {
        std::vector<TimedEvent> all;
        while (clock_ < until)
        {
            std::vector<engine::MidiEvent> here;
            for (const auto& e : events)
                if (e.at >= clock_ && e.at < clock_ + blockSize_)
                {
                    auto copy = e.event;
                    copy.sampleOffset = static_cast<int>(e.at - clock_);
                    here.push_back(copy);
                }
            const auto out = block(here);
            all.insert(all.end(), out.begin(), out.end());
        }
        return all;
    }

    engine::PlayHead head;

private:
    std::unique_ptr<engine::InsertProcessor> effect_;
    int blockSize_;
    std::int64_t clock_ = 0;
    engine::MidiEventList in_{1024}, out_{8192};
};

// The note-ons in a stream.
inline std::vector<TimedEvent> onsOf(const std::vector<TimedEvent>& events)
{
    std::vector<TimedEvent> ons;
    for (const auto& e : events)
        if (e.isNoteOn())
            ons.push_back(e);
    return ons;
}

} // namespace northlight::test
