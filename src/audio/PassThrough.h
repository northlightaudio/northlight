#pragma once

#include <cstddef>

// Phase 1 input-to-output pass-through with a master gain. Pure, allocation-free,
// real-time safe; called from the audio callback. Replaced by the engine graph in
// Phase 2 but kept as the simplest possible "is audio flowing" path.

namespace aural::audio
{

// Copies inputs to outputs with a linear gain ramp from `gainStart` to `gainEnd`
// across the block. Output channel i receives input channel (i % numInputs), so a
// single input fans out to every output; with no inputs the outputs are cleared.
// Null channel pointers (JUCE passes them for inactive channels) are treated as
// silence on input and skipped on output. With `accumulate` the inputs are added to
// whatever is already in the outputs (monitoring on top of playback) and outputs
// are never cleared.
void renderPassThrough(const float* const* inputs,
                       int numInputs,
                       float* const* outputs,
                       int numOutputs,
                       int numSamples,
                       float gainStart,
                       float gainEnd,
                       bool accumulate = false) noexcept;

// Writes silence to every output channel.
void renderSilence(float* const* outputs, int numOutputs, int numSamples) noexcept;

} // namespace aural::audio
