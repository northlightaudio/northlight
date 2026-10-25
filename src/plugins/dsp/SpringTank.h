#pragma once

#include "engine/rt/RealtimeGuard.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <vector>

// A spring reverb's loop: the dispersion of a stretched allpass cascade (each stage
// (a + z^-K) / (1 + a z^-K), K putting its whole sweep below the spring's transition
// frequency, so the highs arrive later than the lows: the chirp) inside a delay loop
// that sends each chirp round again, more dispersed, through a one-pole low-pass
// that darkens it. Band-limiting in and out is the caller's (a tank's driver and
// recovery differ from one effect to the next).

namespace northlight::plugins::dsp
{

struct SpringTuning
{
    int stages = 60;           // allpasses in the cascade
    float coefficient = 0.65f; // each allpass's a
    float dampingHz = 3800.0f; // the loop's one-pole low-pass
};

class SpringTank
{
public:
    // loopSeconds is the whole round trip (the cascade's own delay included);
    // loopGain scales what comes round again.
    void prepare(const SpringTuning& tuning,
                 double sampleRate,
                 double loopSeconds,
                 double transitionHz,
                 float loopGain)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("SpringTank::prepare");
        stages_ = std::max(1, tuning.stages);
        coefficient_ = tuning.coefficient;
        k_ = std::max(1, static_cast<int>(std::lround(sampleRate / (2.0 * transitionHz))));
        memory_.assign(static_cast<size_t>(stages_ * k_), 0.0f);
        // The cascade itself delays the signal; the line makes up the rest of the loop.
        const double cascadeSamples = stages_ * k_ * (1.0 - coefficient_) / (1.0 + coefficient_);
        length_ = std::max(8, static_cast<int>(sampleRate * loopSeconds - cascadeSamples));
        loop_.assign(static_cast<size_t>(length_), 0.0f);
        position_ = write_ = 0;
        low_ = 0.0f;
        lowStep_ = stepForHz(tuning.dampingHz, static_cast<float>(sampleRate));
        gain_ = loopGain;
    }

    void reset() noexcept
    {
        std::fill(memory_.begin(), memory_.end(), 0.0f);
        std::fill(loop_.begin(), loop_.end(), 0.0f);
        low_ = 0.0f;
    }

    float process(float x) noexcept
    {
        const float back = loop_[static_cast<size_t>(write_)]; // the oldest sample: a full loop ago
        low_ += lowStep_ * (back - low_);
        float u = x + gain_ * low_;
        auto* m = memory_.data() + position_;
        for (int s = 0; s < stages_; ++s, m += k_)
        {
            const float old = *m;
            const float w = u - coefficient_ * old;
            u = coefficient_ * w + old;
            *m = w;
        }
        if (++position_ == k_)
            position_ = 0;
        loop_[static_cast<size_t>(write_)] = u;
        if (++write_ == length_)
            write_ = 0;
        if (std::abs(low_) < 1.0e-15f)
            low_ = 0.0f;
        return u;
    }

private:
    std::vector<float> memory_, loop_;
    int stages_ = 1, k_ = 1, position_ = 0, length_ = 8, write_ = 0;
    float coefficient_ = 0.65f, low_ = 0.0f, lowStep_ = 0.5f, gain_ = 0.9f;
};

} // namespace northlight::plugins::dsp
