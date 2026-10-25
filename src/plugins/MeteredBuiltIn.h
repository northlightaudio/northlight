#pragma once

#include "engine/Feedback.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace northlight::plugins
{

inline float peakOf(const float* left, const float* right, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    return peak;
}

// What every built-in with an editor shares: the feedback block the editor
// reads (levels in and out, a block's gain reduction and processor-specific
// extras, and the before/after taps for an analyser), measured only while
// something is watching (Watcher::editor or Watcher::mixer). The processor
// writes its frames itself while running; bypassed, a watched built-in
// measures what passes (its input, or a latent one's input delayed), unless its
// bypassed upkeep measured what it would do instead.
class FeedbackBuiltIn : public ProcessorBase
{
public:
    FeedbackBuiltIn(std::string typeName, std::vector<engine::ParameterInfo> infos, bool taps = false)
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
    virtual void prepareBuiltIn(double sampleRate, int maxBlockSize) = 0;

    void processBypassed(float* left, float* right, int numSamples) noexcept final
    {
        if (!bypassedBuiltIn(left, right, numSamples))
            feedback_.addBypassed(left, right, numSamples);
    }
    // The settled bypass's own work: by default none (the input passes
    // untouched). A processor that keeps running while bypassed (a tape's
    // delay line, a reverb's tail) does it here, leaving in left/right what
    // passes. It returns true when it measured for a watching editor itself
    // (what it would add, say), false to have what passes measured.
    virtual bool bypassedBuiltIn(float*, float*, int) noexcept { return false; }

    engine::ProcessorFeedback feedback_;
};

// A built-in measured per block: its input and output peaks and taps around
// processBuiltIn, which sets the block's gain reduction and extra.
class MeteredBuiltIn : public FeedbackBuiltIn
{
public:
    using FeedbackBuiltIn::FeedbackBuiltIn;

protected:
    void processAudio(float* left, float* right, int numSamples) noexcept final
    {
        const bool watched = feedback_.watched();
        feedback_.setTempoBpm(playHead().tempoBpm);
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

    virtual void processBuiltIn(float* left, float* right, int numSamples) noexcept = 0;

    // Set by processBuiltIn for the block's frame: the most gain reduction (dB)
    // and the processor's extra (a detector level, linear).
    float blockReduction_ = 0.0f;
    float blockExtra_ = 0.0f;
};

} // namespace northlight::plugins
