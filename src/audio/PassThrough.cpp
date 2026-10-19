#include "audio/PassThrough.h"

#include <algorithm>

namespace aural::audio
{

void renderSilence(float* const* outputs, int numOutputs, int numSamples) noexcept
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (float* out = outputs[ch])
            std::fill(out, out + numSamples, 0.0f);
}

void renderPassThrough(const float* const* inputs,
                       int numInputs,
                       float* const* outputs,
                       int numOutputs,
                       int numSamples,
                       float gainStart,
                       float gainEnd,
                       bool accumulate) noexcept
{
    if (numInputs <= 0 || numSamples <= 0)
    {
        if (!accumulate)
            renderSilence(outputs, numOutputs, numSamples);
        return;
    }

    const float step = numSamples > 1 ? (gainEnd - gainStart) / static_cast<float>(numSamples - 1) : 0.0f;

    for (int ch = 0; ch < numOutputs; ++ch)
    {
        float* out = outputs[ch];
        if (out == nullptr)
            continue;

        const float* in = inputs[ch % numInputs];
        if (in == nullptr)
        {
            if (!accumulate)
                std::fill(out, out + numSamples, 0.0f);
            continue;
        }

        float gain = gainStart;
        if (accumulate)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                out[i] += in[i] * gain;
                gain += step;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                out[i] = in[i] * gain;
                gain += step;
            }
        }
    }
}

} // namespace aural::audio
