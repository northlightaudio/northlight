#include "plugins/patina/Crush.h"

#include "plugins/Patina.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

void Crush::prepare(double sampleRate) noexcept
{
    fs_ = sampleRate;
    reset();
}

void Crush::reset() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        for (auto& f : pre_[ch])
            f.reset();
        for (auto& f : post_[ch])
            f.reset();
        held_[ch] = 0.0f;
    }
    phase_ = 1.0f;
    threshold_ = 1.0f;
    designed_ = -1.0f;
}

void Crush::controls(int flavour, Accumulator& acc) noexcept
{
    flavour_ = flavour;
    bitsNow_ = crushBitsFor(bitsAmount);
    rateNow_ = crushRateFor(rateAmount);
    levels_ = std::pow(2.0f, bitsNow_ - 1.0f);
    filtered_ = flavour == soft || flavour == mulaw;
    if (filtered_ && std::abs(rateNow_ - designed_) > 0.002f * designed_)
    {
        designed_ = rateNow_;
        // Eighth-order Butterworth either side of the hold, below its
        // Nyquist: a converter of the time with good filters.
        static constexpr double qs[4] = {0.5098, 0.6013, 0.9000, 2.5629};
        const double hz = std::min(0.4 * rateNow_, 0.45 * fs_);
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t k = 0; k < 4; ++k)
            {
                pre_[ch][k].setLowPass(fs_, hz, qs[k]);
                post_[ch][k].setLowPass(fs_, hz, qs[k]);
            }
    }
    acc.crushBits = bitsNow_;
    acc.crushRate = rateNow_;
}

float Crush::sample(size_t ch, float x) noexcept
{
    const int flavour = flavour_;
    if (filtered_)
        for (auto& f : pre_[ch])
            x = f.process(x);
    // The hold: a new sample each time the converter's clock ticks (one
    // clock for both sides, as a real converter).
    if (ch == 0)
    {
        tick_ = false;
        phase_ += rateNow_ / static_cast<float>(fs_);
        if (phase_ >= threshold_)
        {
            phase_ -= threshold_;
            tick_ = true;
            threshold_ = flavour == jitter ? 1.0f + 0.45f * rng_.bipolar() : 1.0f;
        }
    }
    if (tick_ || rateNow_ >= static_cast<float>(fs_))
    {
        float v = std::clamp(x, -1.0f, 1.0f);
        if (flavour == mulaw)
        {
            constexpr float mu = 255.0f;
            const float c = std::copysign(std::log1p(mu * std::abs(v)) / std::log1p(mu), v);
            const float q = std::round(c * levels_) / levels_;
            v = std::copysign((std::pow(1.0f + mu, std::abs(q)) - 1.0f) / mu, q);
        }
        else
            v = std::round(v * levels_) / levels_;
        held_[ch] = v;
    }
    float y = held_[ch];
    if (filtered_)
        for (auto& f : post_[ch])
            y = f.process(y);
    return y;
}

} // namespace northlight::plugins::patina
