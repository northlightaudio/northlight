#pragma once

#include "plugins/Biquad.h"
#include "plugins/dsp/Random.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>

// Patina's Grain stage: the medium's own noise (a Source: vinyl crackle and
// surface, tape hiss, mains hum, room air), riding or ducking under what plays.
// The stage makes the noise and its gain; Patina adds it. Real-time safe.
// JUCE-free.

namespace northlight::plugins::patina
{

class Grain
{
public:
    // The smoothed controls Patina sets each sub-block: the amount (Age and
    // Breath applied, 0..1), the texture (0..1) and Follow (-1 ducks .. +1 rides).
    float amount = 0, texture = 0, follow = 0;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // Per sub-block (`seconds` long), for a Source.
    void controls(int source, float seconds) noexcept;
    // Every sample, on or off: follows the level of what plays.
    void track(float level, float attack, float release) noexcept
    {
        envelope_ += (level - envelope_) * (level > envelope_ ? attack : release);
    }
    // The noise's gain now, Follow applied.
    float gain() const noexcept { return gain_ * followGain(); }
    // A stereo sample of the noise, before gain().
    void sample(float& outL, float& outR) noexcept;
    // Once a block: keeps the level follower out of denormals.
    void flush() noexcept;

private:
    static constexpr size_t humHarmonics = 15;

    struct NoiseChannel
    {
        dsp::XorShift32 rng;
        float clickEnv = 0.0f, clickDecay = 0.0f, popEnv = 0.0f, popDecay = 0.0f;
        Biquad clickTone, popTone, surface, surfaceTop, rumble, hissLow, hissPeak, hissTop, airTop, airDrone;
        Pink pink;
        Contour level;
    };

    float followGain() const noexcept;

    double fs_ = 48000.0;
    int source_ = 0;
    std::array<NoiseChannel, 2> noise_;
    std::array<float, humHarmonics> humAmp_{};
    float humPhase_ = 0, recordPhase_ = 0, gain_ = 0, clickProbability_ = 0, envelope_ = 0;
    Contour humLevel_;
    int designed_ = -1;
    float textureDesigned_ = -1;
};

} // namespace northlight::plugins::patina
