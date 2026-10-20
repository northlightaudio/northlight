#pragma once

#include "core/Model.h"

#include <string>

// An instrument's outputs as tracks (07-plugins.md 7.4): the main pair is the
// instrument track itself; every further stereo pair can feed an output track
// (an aux track) with its own inserts, strip, sends and fader.

namespace aural::ui
{

// The output track reading output pair `output` (1 = the first beyond the
// main pair) of the instrument on `source`, or null.
inline const core::Track* outputTrack(const core::Project& project, const core::Track& source, int output)
{
    for (const auto& t : project.tracks)
        if (t.isAux() && t.auxTrack == source.id && t.auxInsert == source.instrument.id &&
            t.auxOutput == output)
            return &t;
    return nullptr;
}

// The channels an output pair carries, "3/4" for output 1.
inline std::string outputPairName(int output)
{
    return std::to_string(2 * output + 1) + "/" + std::to_string(2 * output + 2);
}

// A new output track for pair `output` of the instrument on `source`, in the
// source's colour, named for what the instrument says the pair carries ("Kick")
// or else after the source and the channels ("Keys 3/4").
inline core::Track makeOutputTrack(const core::Track& source, int output, const std::string& carries = {})
{
    core::Track aux;
    aux.kind = core::TrackKind::aux;
    aux.name = carries.empty() ? source.name + " " + outputPairName(output) : carries;
    aux.colour = source.colour;
    aux.auxTrack = source.id;
    aux.auxInsert = source.instrument.id;
    aux.auxOutput = output;
    return aux;
}

} // namespace aural::ui
