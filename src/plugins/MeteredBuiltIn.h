#pragma once

#include "engine/Feedback.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace aural::plugins
{

inline float peakOf(const float* left, const float* right, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    return peak;
}

// What every built-in shares: the feedback block its inline editor reads
// (levels in and out, a block's gain reduction and a processor-specific
// extra, and the before/after taps for an analyser), measured only while an
// editor is watching. Bypassed, a watched built-in measures its input.
class MeteredBuiltIn : public ProcessorBase
{
public:
    MeteredBuiltIn(std::string typeName, std::vector<engine::ParameterInfo> infos, bool taps = false)
        : ProcessorBase(std::move(typeName), std::move(infos)), feedback_(taps)
    {
    }

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

protected:
    void prepareAudio(double sampleRate, int maxBlockSize) final
    {
        feedback_.prepare(sampleRate);
        prepareBuiltIn(sampleRate, maxBlockSize);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept final
    {
        const bool watched = feedback_.watched();
        float in = 0.0f;
        if (watched)
        {
            in = peakOf(left, right, numSamples);
            feedback_.tap(0, left, right, numSamples);
        }
        blockReduction_ = 0.0f;
        blockExtra_ = 0.0f;
        processBuiltIn(left, right, numSamples);
        if (watched)
        {
            feedback_.tap(1, left, right, numSamples);
            feedback_.addBlock(in, peakOf(left, right, numSamples), blockReduction_, blockExtra_, numSamples);
        }
    }

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        if (!feedback_.watched())
            return;
        const float peak = peakOf(left, right, numSamples);
        feedback_.addBlock(peak, peak, 0.0f, 0.0f, numSamples);
        feedback_.tap(0, left, right, numSamples);
        feedback_.tap(1, left, right, numSamples);
    }

    virtual void prepareBuiltIn(double sampleRate, int maxBlockSize) = 0;
    virtual void processBuiltIn(float* left, float* right, int numSamples) noexcept = 0;

    // Set by processBuiltIn for the block's frame: the most gain reduction (dB)
    // and the processor's extra (a detector level, linear).
    float blockReduction_ = 0.0f;
    float blockExtra_ = 0.0f;

private:
    engine::ProcessorFeedback feedback_;
};

} // namespace aural::plugins
