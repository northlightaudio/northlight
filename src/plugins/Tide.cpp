#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Pedals.h"
#include "plugins/dsp/DelayLine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::plugins::tide
{

namespace
{
constexpr double bufferSeconds = 0.03;
constexpr double bbdHz = 7000.0;
constexpr float wetLevel = 0.85f;
} // namespace

float rateHz(float rate) noexcept
{
    return 0.1f * std::pow(80.0f, std::clamp(rate, 0.0f, 10.0f) * 0.1f); // 0.1 .. 8 Hz
}

float sweepMs(float depth) noexcept
{
    return 5.5f * std::clamp(depth, 0.0f, 10.0f) * 0.1f;
}

namespace
{
// A triangle from 0..1 phase: -1 at 0, +1 at a half.
float triangle(float phase) noexcept
{
    return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
}

std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[rate] = {"Rate", "", 0.0f, 10.0f, 3.0f, false};
    p[depth] = {"Depth", "", 0.0f, 10.0f, 5.0f, false};
    p[mix] = {"Mix", "", 0.0f, 10.0f, 7.0f, false};
    p[mode] = {"Mode", "", 0.0f, 1.0f, 0.0f, false, {"Chorus", "Vibrato"}};
    return p;
}

class Tide final : public MeteredBuiltIn
{
public:
    Tide() : MeteredBuiltIn(type, parameterInfos()) {}

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        // A power of two, so a read wraps with a mask instead of a modulo.
        size_t size = 8;
        while (size < static_cast<size_t>(sampleRate * bufferSeconds) + 4)
            size *= 2;
        for (auto& ch : channels_)
        {
            ch.buffer.assign(size, 0.0f);
            ch.bbd[0].setLowPass(sampleRate, bbdHz, 0.54);
            ch.bbd[1].setLowPass(sampleRate, bbdHz, 1.31);
            ch.wetHighPass.setHighPass(sampleRate, wetHighPassHz);
            ch.dryHighPass.setHighPass(sampleRate, wetHighPassHz);
            for (auto* f : {&ch.bbd[0], &ch.bbd[1], &ch.wetHighPass, &ch.dryHighPass})
                f->reset();
        }
        write_ = 0;
        phase_ = 0.0f;
        primed_ = false; // the first block takes the settings as they are then
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const auto rate = static_cast<float>(sampleRate_);
        const int size = static_cast<int>(channels_[0].buffer.size());
        if (size < 8)
            return;
        const float phaseStep = rateHz(parameter(tide::rate)) / rate;
        const float centre = centreMs * 0.001f * rate;
        const float sweep = sweepMs(parameter(depth)) * 0.001f * rate;
        const float mixTarget = parameter(tide::mix) * 0.1f;
        const float vibratoTarget = parameter(mode) >= 0.5f ? 1.0f : 0.0f;
        if (!primed_)
        {
            mix_ = mixTarget;
            vibrato_ = vibratoTarget;
            primed_ = true;
        }
        const float mixStep = (mixTarget - mix_) / static_cast<float>(std::max(1, numSamples));
        const float vibratoStep = (vibratoTarget - vibrato_) / static_cast<float>(std::max(1, numSamples));
        float* io[2] = {left, right};
        float wetPeak = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            mix_ += mixStep;
            vibrato_ += vibratoStep;
            for (size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels_[c];
                const float dry = io[c][n];
                ch.buffer[static_cast<size_t>(write_)] = dry;
                // The right side swept in the opposite phase.
                float p = phase_ + (c == 0 ? 0.0f : 0.5f);
                if (p >= 1.0f)
                    p -= 1.0f;
                const float delay = centre + sweep * triangle(p);
                const float wet = ch.wetHighPass.process(bbd(ch, readCubic(ch.buffer, delay)));
                wetPeak = std::max(wetPeak, std::abs(wet));
                // Chorus: the dry signal and the wet over it. Vibrato: the dry lows
                // and the wet highs, Mix crossfading the dry highs out.
                const float dryHigh = ch.dryHighPass.process(dry);
                const float chorus = dry + wetLevel * mix_ * wet;
                const float vibrato = dry - dryHigh * mix_ + wet * mix_;
                io[c][n] = chorus + (vibrato - chorus) * vibrato_;
            }
            phase_ += phaseStep;
            if (phase_ >= 1.0f)
                phase_ -= 1.0f;
            if (++write_ == size)
                write_ = 0;
        }
        mix_ = mixTarget;
        vibrato_ = vibratoTarget;
        blockExtra_ = wetPeak;
        for (auto& ch : channels_)
            for (auto* f : {&ch.bbd[0], &ch.bbd[1], &ch.wetHighPass, &ch.dryHighPass})
                f->flushDenormals();
    }

    struct Channel
    {
        std::vector<float> buffer;
        std::array<Biquad, 2> bbd;
        Biquad wetHighPass, dryHighPass;
    };

    // The bucket brigade's band limit and its soft saturation.
    static float bbd(Channel& ch, float x) noexcept
    {
        constexpr float k = 1.5f;
        return ch.bbd[1].process(ch.bbd[0].process(std::tanh(k * x) / k));
    }

    // A cubic (Hermite) read `delay` samples behind the write head. The position
    // wraps by adding the ring's length rather than flooring a negative position
    // (dsp::readHermite's way, which rounds differently), so only the kernel is shared.
    float readCubic(const std::vector<float>& buffer, float delay) const noexcept
    {
        const int size = static_cast<int>(buffer.size()); // a power of two
        const int mask = size - 1;
        float read = static_cast<float>(write_) - delay;
        while (read < 0.0f)
            read += static_cast<float>(size);
        const int i1 = std::clamp(static_cast<int>(read), 0, size - 1);
        const float t = read - static_cast<float>(i1);
        const auto at = [&](int i) { return buffer[static_cast<size_t>(i & mask)]; };
        return dsp::interpolateHermite(at(i1 - 1), at(i1), at(i1 + 1), at(i1 + 2), t);
    }

    std::array<Channel, 2> channels_;
    int write_ = 0;
    float phase_ = 0.0f;
    float mix_ = 0.7f, vibrato_ = 0.0f;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("tide::create");
    return std::make_unique<Tide>();
}

} // namespace northlight::plugins::tide
