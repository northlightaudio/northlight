#pragma once

#include "engine/Insert.h"

#include <memory>

// The first built-ins: gain, the three-band EQ with high-pass, the lookahead
// limiter, the reverb and the delay (types::gain, eq, limiter, reverb and delay
// in plugins/BuiltIns.h, which lists them in the registry). Kept as they are so
// saved projects keep their sound. JUCE-free, real-time safe after prepare().

namespace northlight::plugins::classic
{

std::unique_ptr<engine::InsertProcessor> createGain();
std::unique_ptr<engine::InsertProcessor> createEq();
std::unique_ptr<engine::InsertProcessor> createLimiter();
std::unique_ptr<engine::InsertProcessor> createReverb();
std::unique_ptr<engine::InsertProcessor> createDelay();

} // namespace northlight::plugins::classic
