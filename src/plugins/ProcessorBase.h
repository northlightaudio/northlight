#pragma once

#include "engine/Insert.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

// The base of the built-in processors: atomic parameter storage the real-time
// thread reads per block, click-free bypass, blocks longer than prepare()
// announced taken in pieces, and the key input and MIDI parked for
// processAudio. JUCE-free.

namespace aural::plugins
{

constexpr float minusInfinityDb = -120.0f;

inline float dbToGain(float db) noexcept
{
    return db <= minusInfinityDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

inline float gainToDb(float gain) noexcept
{
    return gain > 0.0f ? 20.0f * std::log10(gain) : minusInfinityDb;
}

// Shared parameter storage: atomics the real-time thread snapshots per block.
class ProcessorBase : public engine::InsertProcessor
{
public:
    ProcessorBase(std::string typeName, std::vector<engine::ParameterInfo> infos)
        : typeName_(std::move(typeName)), infos_(std::move(infos)), values_(infos_.size())
    {
        for (size_t i = 0; i < infos_.size(); ++i)
            values_[i].store(infos_[i].defaultValue, std::memory_order_relaxed);
    }

    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return infos_; }

    void setParameter(int index, float value) noexcept override
    {
        if (index < 0 || static_cast<size_t>(index) >= infos_.size())
            return;
        const auto& info = infos_[static_cast<size_t>(index)];
        values_[static_cast<size_t>(index)].store(std::clamp(value, info.minimum, info.maximum),
                                                  std::memory_order_relaxed);
    }

    float parameter(int index) const noexcept override
    {
        if (index < 0 || static_cast<size_t>(index) >= infos_.size())
            return 0.0f;
        return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }

    // Bypass is a 10 ms crossfade between the processed signal and what the
    // processor passes when bypassed (its input, or for a latent processor its
    // input delayed by the latency). The processor keeps running while bypassed
    // so its state (filter memory, envelopes) follows the audio and un-bypassing
    // does not click.
    void prepare(double sampleRate, int maxBlockSize) final
    {
        sampleRate_ = sampleRate;
        dryLeft_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        dryRight_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        bypassStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.010));
        bypassMix_ = isBypassed() ? 1.0f : 0.0f;
        prepareAudio(sampleRate, maxBlockSize);
    }

    void process(float* left, float* right, int numSamples) noexcept final
    {
        const int capacity = static_cast<int>(dryLeft_.size());
        if (capacity == 0)
            return; // process before prepare: nothing is allocated
        // Longer blocks than prepare() announced are taken in pieces.
        for (int done = 0; done < numSamples; done += capacity)
        {
            chunkStart_ = done;
            processChunk(left + done, right + done, std::min(capacity, numSamples - done));
        }
        chunkStart_ = 0;
    }

    // MIDI goes through the same bypass handling as the key input: parked for
    // processAudio (an instrument reads it with offsets relative to chunkStart_).
    void
    processMidi(const engine::MidiEventList& events, float* left, float* right, int numSamples) noexcept final
    {
        midi_ = &events;
        process(left, right, numSamples);
        midi_ = nullptr;
    }

    void processChunk(float* left, float* right, int numSamples) noexcept
    {
        const float target = isBypassed() ? 1.0f : 0.0f;
        if (bypassMix_ == target && target == 1.0f)
        {
            processBypassed(left, right, numSamples);
            return;
        }
        std::copy(left, left + numSamples, dryLeft_.data());
        std::copy(right, right + numSamples, dryRight_.data());
        processAudio(left, right, numSamples);
        if (bypassMix_ == target)
            return;
        // The bypassed signal for the ramp is the dry input (a latent processor's
        // bypassed path only differs by its delay, which the ramp hides).
        for (int i = 0; i < numSamples; ++i)
        {
            bypassMix_ = target > bypassMix_ ? std::min(target, bypassMix_ + bypassStep_)
                                             : std::max(target, bypassMix_ - bypassStep_);
            left[i] = left[i] * (1.0f - bypassMix_) + dryLeft_[static_cast<size_t>(i)] * bypassMix_;
            right[i] = right[i] * (1.0f - bypassMix_) + dryRight_[static_cast<size_t>(i)] * bypassMix_;
        }
    }

    // The key input goes through the same bypass handling: it is parked for
    // processAudio to pick up (null = no key, use the audio itself).
    void processWithSidechain(
        float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept final
    {
        keyLeft_ = keyLeft;
        keyRight_ = keyRight;
        process(left, right, numSamples);
        keyLeft_ = keyRight_ = nullptr;
    }

protected:
    virtual void prepareAudio(double sampleRate, int maxBlockSize) = 0;
    virtual void processAudio(float* left, float* right, int numSamples) noexcept = 0;
    // Settled bypass: by default the input passes untouched (zero latency).
    virtual void processBypassed(float*, float*, int) noexcept {}

    double sampleRate_ = 48000.0;
    const float* keyLeft_ = nullptr;
    const float* keyRight_ = nullptr;
    const engine::MidiEventList* midi_ = nullptr;
    int chunkStart_ = 0; // offset of the current chunk within the block

private:
    std::string typeName_;
    std::vector<engine::ParameterInfo> infos_;
    std::vector<std::atomic<float>> values_;
    std::vector<float> dryLeft_, dryRight_;
    float bypassMix_ = 0.0f;
    float bypassStep_ = 0.0f;
};

} // namespace aural::plugins
