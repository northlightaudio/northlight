#pragma once

#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// What Patina's stages share: small building blocks and the telemetry they
// gather between frames. Real-time safe except the prepare() calls. JUCE-free.

namespace northlight::plugins::patina
{

inline constexpr float pi = std::numbers::pi_v<float>;
inline constexpr float twoPi = 2.0f * pi;

// Patina's floor for a decaying state.
inline constexpr float denormalFloor = 1.0e-15f;

// A smooth random contour, 0..1: a new random target about every 1/hz seconds,
// followed through two one-pole stages, so it wanders rather than steps.
struct Contour
{
    float a = 0.0f, b = 0.0f, target = 0.0f, timer = 0.0f;
    float step(float seconds, float hz, dsp::XorShift32& rng) noexcept
    {
        timer -= seconds * hz;
        if (timer <= 0.0f)
        {
            timer += 0.5f + rng.uniform();
            target = rng.uniform();
        }
        const float k = 1.0f - std::exp(-twoPi * hz * 0.8f * seconds);
        a += (target - a) * k;
        b += (a - b) * k;
        return b;
    }
    float bipolar(float seconds, float hz, dsp::XorShift32& rng) noexcept
    {
        return 2.0f * step(seconds, hz, rng) - 1.0f;
    }
};

// A Schroeder allpass of a fixed length; `extra` modulates its delay.
struct Allpass
{
    dsp::DelayLine line;
    int length = 1;
    void prepare(int samples, int modulation = 0)
    {
        length = std::max(1, samples);
        line.prepare(length + modulation + 2);
    }
    float process(float x, float g, float extra = 0.0f) noexcept
    {
        const float delayed =
            extra > 0.0f ? line.linear(static_cast<float>(length - 1) + extra) : line.at(length - 1);
        const float w = x + g * delayed;
        line.push(w);
        return delayed - g * w;
    }
};

inline float onePoleCoefficient(double sampleRate, double hz) noexcept
{
    return 1.0f - static_cast<float>(dsp::poleForHz(hz, sampleRate));
}

inline float smoothingCoefficient(double rate, float seconds) noexcept
{
    return dsp::stepForSeconds(std::max(1.0e-4f, seconds), static_cast<float>(rate));
}

// Pink noise from white (Paul Kellet's economy filter), about unit RMS for
// uniform white of RMS 0.577.
struct Pink
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float process(float white) noexcept
    {
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return (b0 + b1 + b2 + white * 0.1848f) * 0.55f;
    }
};

// What the stages measure between two telemetry frames (Patina publishes it as
// a Telemetry::Frame and starts again).
struct Accumulator
{
    float heatLevel = 0, heatDriven = 0, heatShaped = 0, chamberWet = 0;
    float driftLeft = 0, driftRight = 0;
    int driftCount = 0;
    float grainSquare = 0, grainSignal = 0;
    int grainCount = 0;
    float wearDip = 0, wearCutoff = 20000, wearSkew = 0, crushBits = 16, crushRate = 48000;
};

} // namespace northlight::plugins::patina
