#pragma once

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/dsp/DelayLine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// Modulation effects as parts a processor runs on its own output: a stereo
// tremolo (amplitude or an equal-power auto-pan), a four-stage phaser and a
// short stereo chorus. Lumen uses them after its voices; a studio tremolo,
// auto-pan or phaser can be built on the same parts. JUCE-free; real-time safe
// after prepare().

namespace northlight::plugins::dsp
{

// A sine bent towards a square: shape 0 is a sine, 1 nearly square (the
// suitcase tremolo's lopsided wave). Phase 0..1; returns -1..1.
inline float tremoloWave(double phase, float shape) noexcept
{
    const float s = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
    const float k = 1.0f + std::clamp(shape, 0.0f, 1.0f) * 7.0f;
    return k <= 1.0f ? s : std::tanh(k * s) / std::tanh(k);
}

// A tremolo on a stereo pair. Pan moves the sound between the sides with equal
// power (at full depth one side falls silent at each swing); amplitude moves
// both sides' level together, down by the depth at the wave's bottom.
struct Tremolo
{
    enum class Mode
    {
        off,
        pan,
        amplitude
    };

    double phase = 0.0; // 0..1
    float value = 0.0f; // the wave's last value, -1..1

    void reset() noexcept
    {
        phase = 0.0;
        value = 0.0f;
    }

    // One sample: advances the wave by `step` (cycles a sample) and applies it.
    void process(float& l, float& r, Mode mode, double step, float depth, float shape) noexcept
    {
        value = tremoloWave(phase, shape);
        phase += step;
        phase -= std::floor(phase);
        apply(l, r, mode, depth);
    }

    // Applies the current value without moving the wave.
    void apply(float& l, float& r, Mode mode, float depth) const noexcept
    {
        depth = std::clamp(depth, 0.0f, 1.0f);
        if (mode == Mode::pan)
        {
            // Mono in the middle at rest (0.7071 a side), swinging fully across at depth 1.
            const float p = depth * value; // -1 left .. 1 right
            const float angle = (p + 1.0f) * static_cast<float>(std::numbers::pi) * 0.25f;
            const float gl = std::cos(angle) * static_cast<float>(std::numbers::sqrt2);
            const float gr = std::sin(angle) * static_cast<float>(std::numbers::sqrt2);
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);
            l = (mid + side) * gl;
            r = (mid - side) * gr;
        }
        else if (mode == Mode::amplitude)
        {
            const float g = 1.0f - depth * 0.5f * (1.0f - value);
            l *= g;
            r *= g;
        }
    }
};

// A four-stage phaser per channel: first-order all-passes swept together by a
// sine, with feedback around them, summed with the dry signal so the stages'
// phase makes two notches that move. The right side's sweep runs a quarter
// cycle behind the left's.
class Phaser
{
public:
    static constexpr int stages = 4;
    static constexpr int controlInterval = 8;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        reset();
    }
    void reset() noexcept
    {
        for (auto& ch : channels_)
            ch = Channel{};
        phase_ = 0.0;
        countdown_ = 0;
    }

    // rateHz: the sweep; depth 0..1: how far it reaches (two octaves and a bit
    // each way at 1); feedback 0..0.9; mix 0..1 (1: equal dry and wet, the
    // deepest notches).
    void process(float* left,
                 float* right,
                 int numSamples,
                 float rateHz,
                 float depth,
                 float feedback,
                 float mix) noexcept
    {
        if (mix <= 0.0f)
        {
            // Keep the sweep moving, so turning it up does not restart it.
            phase_ += rateHz / sampleRate_ * numSamples;
            phase_ -= std::floor(phase_);
            return;
        }
        float* io[2] = {left, right};
        for (int i = 0; i < numSamples; ++i)
        {
            if (countdown_ <= 0)
            {
                countdown_ = controlInterval;
                for (int c = 0; c < 2; ++c)
                {
                    const double p = phase_ + (c == 0 ? 0.0 : 0.25);
                    const double lfo = std::sin(2.0 * std::numbers::pi * p);
                    const double hz =
                        std::clamp(centreHz * std::exp2(depth * octaves * lfo), 20.0, sampleRate_ * 0.45);
                    const double t = std::tan(std::numbers::pi * hz / sampleRate_);
                    channels_[static_cast<size_t>(c)].a = static_cast<float>((t - 1.0) / (t + 1.0));
                }
            }
            --countdown_;
            phase_ += rateHz / sampleRate_;
            phase_ -= std::floor(phase_);
            for (size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels_[c];
                const float dry = io[c][i];
                float x = dry + feedback * ch.last;
                for (auto& s : ch.state)
                {
                    const float y = ch.a * x + s;
                    s = x - ch.a * y;
                    x = y;
                }
                ch.last = x;
                io[c][i] = dry * (1.0f - 0.5f * mix) + x * 0.5f * mix;
            }
        }
        for (auto& ch : channels_)
        {
            for (auto& s : ch.state)
                if (std::abs(s) < 1.0e-20f)
                    s = 0.0f;
            if (std::abs(ch.last) < 1.0e-20f)
                ch.last = 0.0f;
        }
    }

    double phase() const noexcept { return phase_; }

    static constexpr double centreHz = 640.0;
    static constexpr double octaves = 2.3;

private:
    struct Channel
    {
        std::array<float, stages> state{};
        float a = 0.0f, last = 0.0f;
    };
    std::array<Channel, 2> channels_{};
    double sampleRate_ = 48000.0, phase_ = 0.0;
    int countdown_ = 0;
};

// A short stereo chorus: a delay of about 7 ms swept by a triangle, the right
// side in the opposite phase, its wet signal darkened as a bucket brigade's is,
// added over the dry.
class Chorus
{
public:
    static constexpr double centreMs = 7.0;
    static constexpr double sweepMs = 3.0;

    void prepare(double sampleRate)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Chorus::prepare");
        sampleRate_ = sampleRate;
        const int maxDelay = static_cast<int>(sampleRate * (centreMs + sweepMs) * 0.001) + 8;
        for (auto& ch : channels_)
        {
            ch.line.prepare(maxDelay);
            ch.tone.setLowPass(sampleRate, 7500.0, 0.6);
            ch.tone.reset();
        }
        phase_ = 0.0;
    }
    void reset() noexcept
    {
        for (auto& ch : channels_)
        {
            ch.line.clear();
            ch.tone.reset();
        }
        phase_ = 0.0;
    }

    // rateHz: the sweep; depth 0..1; mix 0..1.
    void process(float* left, float* right, int numSamples, float rateHz, float depth, float mix) noexcept
    {
        if (channels_[0].line.data.empty())
            return;
        float* io[2] = {left, right};
        const double centre = centreMs * 0.001 * sampleRate_;
        const double sweep = sweepMs * 0.001 * sampleRate_ * std::clamp(depth, 0.0f, 1.0f);
        const float norm = 1.0f / (1.0f + 0.35f * mix);
        for (int i = 0; i < numSamples; ++i)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels_[c];
                const float dry = io[c][i];
                ch.line.push(dry);
                if (mix <= 0.0f)
                    continue;
                double p = phase_ + (c == 0 ? 0.0 : 0.5);
                p -= std::floor(p);
                const double tri = p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
                const float wet = ch.tone.process(ch.line.hermite(static_cast<float>(centre + sweep * tri)));
                io[c][i] = (dry + 0.8f * mix * wet) * norm;
            }
            phase_ += rateHz / sampleRate_;
            phase_ -= std::floor(phase_);
        }
        for (auto& ch : channels_)
            ch.tone.flushDenormals();
    }

private:
    struct Channel
    {
        DelayLine line;
        Biquad tone;
    };
    std::array<Channel, 2> channels_;
    double sampleRate_ = 48000.0, phase_ = 0.0;
};

} // namespace northlight::plugins::dsp
