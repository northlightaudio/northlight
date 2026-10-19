#pragma once

#include "core/Model.h"

#include <cstdint>

// Transport state as the real-time thread sees it, and the commands the controller
// sends to change it. Positions are samples at the project rate.

namespace aural::engine
{

// A loop shorter than this is refused: each wrap costs a render span, and a loop
// of a handful of samples would issue thousands of them per block.
constexpr core::SamplePosition minimumLoopSamples = 64;

struct TransportState
{
    core::SamplePosition position = 0;
    bool playing = false;
    bool loopEnabled = false;
    core::SamplePosition loopStart = 0;
    core::SamplePosition loopEnd = 0; // exclusive

    // Metronome click while playing (and during the count-in).
    bool metronome = false;
    float metronomeGain = 0.5f;

    // Count-in: clicks for this many more samples before the position advances and
    // capture begins.
    core::SamplePosition countInRemaining = 0;

    // Punch: when enabled, recording captures only inside [punchIn, punchOut).
    bool punchEnabled = false;
    core::SamplePosition punchIn = 0;
    core::SamplePosition punchOut = 0;

    bool loopIsValid() const noexcept { return loopEnabled && loopEnd > loopStart; }
    bool punchIsValid() const noexcept { return punchEnabled && punchOut > punchIn; }
    bool insidePunch(core::SamplePosition p) const noexcept
    {
        return !punchIsValid() || (p >= punchIn && p < punchOut);
    }
};

struct TransportCommand
{
    enum class Type
    {
        play,
        stop,
        locate,
        setLoop,
        record,       // start capturing the pending record session (after any count-in) and play
        setMetronome, // enabled + gain in `value`
        setPunch,
    };

    Type type = Type::stop;
    core::SamplePosition position = 0; // locate target, loop/punch start, or count-in samples for record
    core::SamplePosition end = 0;      // loop/punch end (exclusive)
    bool enabled = false;              // loop/punch/metronome on/off
    double value = 0.0;                // metronome gain

    static TransportCommand play() noexcept { return {Type::play, 0, 0, false, 0.0}; }
    static TransportCommand stop() noexcept { return {Type::stop, 0, 0, false, 0.0}; }
    static TransportCommand record(core::SamplePosition countInSamples = 0) noexcept
    {
        return {Type::record, countInSamples, 0, false, 0.0};
    }
    static TransportCommand locate(core::SamplePosition p) noexcept
    {
        return {Type::locate, p, 0, false, 0.0};
    }
    static TransportCommand
    setLoop(core::SamplePosition start, core::SamplePosition end, bool enabled) noexcept
    {
        return {Type::setLoop, start, end, enabled, 0.0};
    }
    static TransportCommand setMetronome(bool enabled, double gain) noexcept
    {
        return {Type::setMetronome, 0, 0, enabled, gain};
    }
    static TransportCommand setPunch(core::SamplePosition in, core::SamplePosition out, bool enabled) noexcept
    {
        return {Type::setPunch, in, out, enabled, 0.0};
    }
};

// Applies a command to the state. Real-time safe. The engine handles `record`
// itself (it also starts the capture session) before calling this.
inline void applyTransportCommand(TransportState& state, const TransportCommand& command) noexcept
{
    switch (command.type)
    {
        case TransportCommand::Type::play:
            state.playing = true;
            break;
        case TransportCommand::Type::record:
            state.playing = true;
            state.countInRemaining = command.position > 0 ? command.position : 0;
            break;
        case TransportCommand::Type::stop:
            state.playing = false;
            state.countInRemaining = 0;
            break;
        case TransportCommand::Type::locate:
            state.position = command.position < 0 ? 0 : command.position;
            break;
        case TransportCommand::Type::setLoop:
            // Bounds are validated here so a bad region can never publish a negative
            // position or wrap on every sample.
            state.loopStart = command.position < 0 ? 0 : command.position;
            state.loopEnd = command.end;
            state.loopEnabled = command.enabled && state.loopEnd - state.loopStart >= minimumLoopSamples;
            break;
        case TransportCommand::Type::setMetronome:
            state.metronome = command.enabled;
            state.metronomeGain = static_cast<float>(command.value);
            break;
        case TransportCommand::Type::setPunch:
            state.punchIn = command.position < 0 ? 0 : command.position;
            state.punchOut = command.end;
            state.punchEnabled = command.enabled && state.punchOut > state.punchIn;
            break;
    }
}

} // namespace aural::engine
