#pragma once

#include "core/Model.h"

#include <cstdint>

// Transport state as the real-time thread sees it, and the commands the controller
// sends to change it. Positions are samples at the project rate.

namespace aural::engine
{

struct TransportState
{
    core::SamplePosition position = 0;
    bool playing = false;
    bool loopEnabled = false;
    core::SamplePosition loopStart = 0;
    core::SamplePosition loopEnd = 0; // exclusive

    bool loopIsValid() const noexcept { return loopEnabled && loopEnd > loopStart; }
};

struct TransportCommand
{
    enum class Type
    {
        play,
        stop,
        locate,
        setLoop,
        record, // start capturing the pending record session and play
    };

    Type type = Type::stop;
    core::SamplePosition position = 0; // locate target, or loop start
    core::SamplePosition end = 0;      // loop end (exclusive)
    bool enabled = false;              // loop on/off

    static TransportCommand play() noexcept { return {Type::play, 0, 0, false}; }
    static TransportCommand stop() noexcept { return {Type::stop, 0, 0, false}; }
    static TransportCommand record() noexcept { return {Type::record, 0, 0, false}; }
    static TransportCommand locate(core::SamplePosition p) noexcept { return {Type::locate, p, 0, false}; }
    static TransportCommand
    setLoop(core::SamplePosition start, core::SamplePosition end, bool enabled) noexcept
    {
        return {Type::setLoop, start, end, enabled};
    }
};

// Applies a command to the state. Real-time safe.
inline void applyTransportCommand(TransportState& state, const TransportCommand& command) noexcept
{
    switch (command.type)
    {
        case TransportCommand::Type::play:
        case TransportCommand::Type::record:
            state.playing = true;
            break;
        case TransportCommand::Type::stop:
            state.playing = false;
            break;
        case TransportCommand::Type::locate:
            state.position = command.position < 0 ? 0 : command.position;
            break;
        case TransportCommand::Type::setLoop:
            state.loopStart = command.position;
            state.loopEnd = command.end;
            state.loopEnabled = command.enabled;
            break;
    }
}

} // namespace aural::engine
