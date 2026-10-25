#pragma once

#include "plugins/Biquad.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Test material for the vocal effects (the Listener, Vesper).

namespace northlight::testing
{

// A synthetic singer: vowels as harmonics shaped by formants, consonants and
// breaths as filtered noise. Deterministic, so the same phrase always gives the
// same hearing.
class Singer
{
public:
    explicit Singer(double rate) : rate_(rate) {}

    // A sung vowel ("ah": formants at 700, 1220 and 2600 Hz) at `hz`, `db` RMS.
    // Its phase follows the clock, so a vowel `joined` to one at the same pitch
    // carries on without a seam (and without fades).
    Singer& vowel(double seconds, float hz, float db = -18.0f, bool joined = false)
    {
        std::vector<float> v(samples(seconds), 0.0f);
        constexpr std::array<float, 3> formants{700.0f, 1220.0f, 2600.0f};
        constexpr std::array<float, 3> widths{110.0f, 120.0f, 160.0f};
        for (int k = 1; k * hz < 5000.0f; ++k)
        {
            const float f = k * hz;
            float shape = 0.04f;
            for (size_t i = 0; i < formants.size(); ++i)
            {
                const float x = (f - formants[i]) / widths[i];
                shape += 1.0f / (1.0f + x * x);
            }
            const float amp = shape / static_cast<float>(k);
            const double w = 2.0 * std::numbers::pi * f / rate_;
            const auto start = static_cast<double>(audio_.size());
            for (size_t n = 0; n < v.size(); ++n)
                v[n] += amp * static_cast<float>(std::sin(w * (start + static_cast<double>(n)) + 0.7 * k));
        }
        return add(v, db, !joined);
    }

    // An "s": noise above 4.5 kHz.
    Singer& s(double seconds, float db = -24.0f)
    {
        auto v = noise(seconds);
        plugins::Biquad a, b;
        a.setHighPass(rate_, 4500.0);
        b.setHighPass(rate_, 4500.0);
        for (auto& x : v)
            x = b.process(a.process(x));
        return add(v, db);
    }

    // A "t": a 30 ms burst of noise above 1.5 kHz, falling fast.
    Singer& t(float db = -20.0f)
    {
        auto v = noise(0.03);
        plugins::Biquad a;
        a.setHighPass(rate_, 1500.0);
        for (size_t n = 0; n < v.size(); ++n)
            v[n] = a.process(v[n]) * static_cast<float>(std::exp(-static_cast<double>(n) / (0.008 * rate_)));
        return add(v, db, false);
    }

    // A breath: noise between 500 Hz and 2.5 kHz.
    Singer& breath(double seconds, float db = -40.0f)
    {
        auto v = noise(seconds);
        plugins::Biquad a, b;
        a.setHighPass(rate_, 500.0);
        b.setLowPass(rate_, 2500.0);
        for (auto& x : v)
            x = b.process(a.process(x));
        return add(v, db);
    }

    Singer& rest(double seconds)
    {
        audio_.resize(audio_.size() + samples(seconds), 0.0f);
        return *this;
    }

    double now() const { return static_cast<double>(audio_.size()) / rate_; }
    const std::vector<float>& audio() const { return audio_; }

private:
    size_t samples(double seconds) const { return static_cast<size_t>(seconds * rate_); }

    std::vector<float> noise(double seconds)
    {
        std::vector<float> v(samples(seconds));
        for (auto& x : v)
        {
            seed_ = seed_ * 1664525u + 1013904223u;
            x = static_cast<float>(seed_ >> 8) / 8388608.0f - 1.0f;
        }
        return v;
    }

    // Appended at `db` RMS, with 5 ms fades (unless it is a burst).
    Singer& add(std::vector<float>& v, float db, bool fades = true)
    {
        double sum = 0.0;
        for (const float x : v)
            sum += static_cast<double>(x) * x;
        const auto gain = static_cast<float>(std::pow(10.0, db / 20.0) / std::sqrt(sum / v.size() + 1.0e-20));
        const auto fade = fades ? samples(0.005) : 0;
        for (size_t n = 0; n < v.size(); ++n)
        {
            float g = gain;
            if (n < fade)
                g *= static_cast<float>(n) / fade;
            if (v.size() - n < fade)
                g *= static_cast<float>(v.size() - n) / fade;
            audio_.push_back(v[n] * g);
        }
        return *this;
    }

    double rate_;
    std::vector<float> audio_;
    std::uint32_t seed_ = 99u;
};

} // namespace northlight::testing
