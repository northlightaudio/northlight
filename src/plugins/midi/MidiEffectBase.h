#pragma once

#include "engine/Insert.h"
#include "engine/Midi.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// The base of the built-in MIDI effects (docs/plans/midi-inserts.md): atomic
// parameters, the play head, and the bookkeeping every MIDI effect needs so a
// note never hangs:
//
// - Output notes are counted per (channel, note). A note-off goes out only for a
//   note that is sounding, and only when the last thing holding it lets go. At a
//   discontinuity, or when bypass changes, everything sounding ends.
// - Each held input note remembers the output notes it started, so its note-off
//   ends exactly those, whatever the parameters became since.
// - A queue holds events for later blocks (an echo, a strum, a fixed length),
//   timed on the effect's own sample clock, which runs whether or not the
//   transport does.
//
// A derived effect overrides noteOn (and otherEvent, advance, reset as it
// needs) and calls startNote, emit and schedule. JUCE-free; everything is
// allocated in the constructor.

namespace northlight::plugins::midi
{

class MidiEffectBase : public engine::InsertProcessor
{
public:
    // Output notes one input note can hold at once (a chord's voices).
    static constexpr int maxOutputsPerNote = 16;
    // Events waiting for a later block.
    static constexpr int queueCapacity = 2048;

    MidiEffectBase(std::string typeName, std::vector<engine::ParameterInfo> infos);

    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return infos_; }
    void setParameter(int index, float value) noexcept override;
    float parameter(int index) const noexcept override;

    void prepare(double sampleRate, int maxBlockSize) final;
    // A MIDI effect makes no audio: the buffers are left as they are.
    void process(float*, float*, int) noexcept final {}
    void setPlayHead(const engine::PlayHead& head) noexcept final { playHead_ = head; }

    bool isMidiEffect() const noexcept final { return true; }
    void processMidiEffect(const engine::MidiEventList& in,
                           engine::MidiEventList& out,
                           int numSamples) noexcept final;

    // How many output notes are sounding (for tests and the card's activity).
    int soundingCount() const noexcept;

protected:
    // ----- hooks (real-time) -------------------------------------------------------------
    // Once per block, before any event: read the parameters.
    virtual void beginBlock() noexcept {}
    // An input note-on (velocity > 0). The default passes it through.
    virtual void noteOn(const engine::MidiEvent& e) noexcept { startNote(e, e.note, e.velocity, e.channel); }
    // An input note-off (a note-on with velocity 0 arrives as one). The default
    // ends what that note started (releaseNote).
    virtual void noteOff(const engine::MidiEvent& e) noexcept { releaseNote(e); }
    // Anything but a note or an all-notes-off (controllers, bend, pressure). The
    // default passes it through; poly pressure on a held note goes to the notes
    // that note started.
    virtual void otherEvent(const engine::MidiEvent& e) noexcept;
    // The time from `from` to `to` on the effect's clock has passed with no input
    // event in it (a tempo-synced effect schedules its steps here).
    virtual void advance(std::int64_t from, std::int64_t to) noexcept
    {
        (void)from;
        (void)to;
    }
    // A discontinuity or a bypass change: forget what the effect was doing (the
    // base has already ended every note and emptied the queue).
    virtual void reset() noexcept {}
    // Controller thread, from prepare.
    virtual void prepareEffect(double sampleRate) { (void)sampleRate; }

    // ----- services (real-time) ----------------------------------------------------------
    // Starts an output note now (at the input's offset) for the input note
    // `input`, remembering it so the input's note-off ends it. A note outside
    // 0..127 is not played, but the input still counts as taken, so its
    // note-off ends nothing else. False when nothing was started.
    bool startNote(const engine::MidiEvent& input, int note, int velocity, int channel) noexcept;
    // The input note is taken and starts nothing (yet): its note-off is swallowed.
    void take(const engine::MidiEvent& input) noexcept;
    // Ends, at the note-off's offset, the output notes its note-on started and
    // forgets it. A note-off for a note the effect never took passes through (it
    // ends that note only if it is sounding).
    void releaseNote(const engine::MidiEvent& noteOff) noexcept;
    // An event out now, at its own offset (clamped into the block). Note events
    // go through the sounding count.
    bool emit(engine::MidiEvent e) noexcept;
    // An event out at a time on the effect's clock (now or later; an earlier time
    // plays now). False when the queue is full.
    bool schedule(std::int64_t clockTime, const engine::MidiEvent& e) noexcept;

    float value(int index) const noexcept
    {
        return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }
    const engine::PlayHead& playHead() const noexcept { return playHead_; }
    double sampleRate() const noexcept { return sampleRate_; }
    // The effect's clock at the block's first sample, and the block's length.
    std::int64_t blockClock() const noexcept { return clock_; }
    int blockLength() const noexcept { return blockLength_; }

private:
    struct Held
    {
        bool taken = false;
        std::uint8_t count = 0;
        std::array<std::uint8_t, maxOutputsPerNote> channel{};
        std::array<std::uint8_t, maxOutputsPerNote> note{};
    };
    struct Pending
    {
        std::int64_t time = 0;
        engine::MidiEvent event;
    };

    void endEverything(int offset) noexcept;
    void flushQueue(std::int64_t through) noexcept;
    void advanceTo(std::int64_t to) noexcept;
    Held& held(int channel, int note) noexcept
    {
        return held_[static_cast<size_t>((channel & 15) * 128 + (note & 127))];
    }
    std::uint8_t& sounding(int channel, int note) noexcept
    {
        return sounding_[static_cast<size_t>((channel & 15) * 128 + (note & 127))];
    }

    std::string typeName_;
    std::vector<engine::ParameterInfo> infos_;
    std::vector<std::atomic<float>> values_;
    double sampleRate_ = 48000.0;
    engine::PlayHead playHead_;

    std::vector<Held> held_;             // 16 x 128, by input
    std::vector<std::uint8_t> sounding_; // 16 x 128, by output
    std::vector<Pending> queue_;         // sorted by time, `queued_` long
    int queued_ = 0;
    std::int64_t clock_ = 0;
    std::int64_t advanced_ = 0; // how far advance() has been told
    int blockLength_ = 0;
    engine::MidiEventList* out_ = nullptr; // the block's output while processing
    bool wasBypassed_ = false;
};

} // namespace northlight::plugins::midi
