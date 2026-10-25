#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Pedals.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace northlight::plugins::comet
{

namespace
{
constexpr double bufferSeconds = 1.0;
// How quickly the clock follows Time: slow enough that a turn bends the repeats.
constexpr float glideSeconds = 0.12f;
// The loop's bass cut, so long feedback does not pile up mud under the guitar.
constexpr double loopHighPassHz = 90.0;
// The compander's ceiling: the loop's soft limit.
constexpr float ceiling = 0.9f;
// Mod: the clock's slow wobble, as a share of the delay.
constexpr float modHz = 0.7f, modDepth = 0.0035f;
} // namespace

float bandwidthHz(float timeMs) noexcept
{
    const float seconds = std::clamp(timeMs, minTimeMs, maxTimeMs) * 0.001f;
    const float clock = static_cast<float>(bucketStages) / (2.0f * seconds);
    return std::clamp(0.38f * clock, 900.0f, 9000.0f);
}

float feedbackGain(float repeats) noexcept
{
    return 1.12f * std::pow(std::clamp(repeats, 0.0f, 10.0f) * 0.1f, 1.1f);
}

namespace
{
class Comet final : public MeteredBuiltIn
{
public:
    Comet()
        : MeteredBuiltIn(type,
                         {{"Time", "ms", minTimeMs, maxTimeMs, 350.0f, true},
                          {"Repeats", "", 0.0f, 10.0f, 4.0f, false},
                          {"Mix", "", 0.0f, 10.0f, 5.0f, false},
                          {"Mod", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}}})
    {
    }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        const auto size = static_cast<size_t>(sampleRate * bufferSeconds) + 4;
        for (auto& ch : channels_)
        {
            ch.buffer.assign(size, 0.0f);
            ch.highPass.setHighPass1(sampleRate, loopHighPassHz);
            ch.highPass.reset();
            ch.lowPass[0].reset();
            ch.lowPass[1].reset();
        }
        write_ = 0;
        phase_ = 0.0f;
        primed_ = false;
        lastBandwidth_ = -1.0f;
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const auto rate = static_cast<float>(sampleRate_);
        const int size = static_cast<int>(channels_[0].buffer.size());
        if (size < 8)
            return;
        const float target = std::clamp(parameter(time) * 0.001f * rate, 1.0f, static_cast<float>(size - 4));
        if (!primed_)
        {
            current_ = target;
            primed_ = true;
        }
        const float glide = dsp::stepForSeconds(glideSeconds, rate);
        // The bandwidth follows the clock (the gliding time), once a block.
        const float bandwidth = bandwidthHz(current_ / rate * 1000.0f);
        if (std::abs(bandwidth - lastBandwidth_) > 1.0f)
        {
            lastBandwidth_ = bandwidth;
            for (auto& ch : channels_)
            {
                // Two poles each way of a Butterworth pair: the clock's filters.
                ch.lowPass[0].setLowPass(sampleRate_, bandwidth, 0.54);
                ch.lowPass[1].setLowPass(sampleRate_, bandwidth, 1.31);
            }
        }
        const float feedback = feedbackGain(parameter(repeats));
        const float wet = parameter(mix) * 0.1f;
        const bool modOn = parameter(mod) >= 0.5f;
        const float phaseStep = modHz / rate;
        float* io[2] = {left, right};
        float wetPeak = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            current_ += (target - current_) * glide;
            float delay = current_;
            if (modOn)
            {
                delay *= 1.0f + modDepth * std::sin(2.0f * std::numbers::pi_v<float> * phase_);
                phase_ += phaseStep;
                if (phase_ >= 1.0f)
                    phase_ -= 1.0f;
            }
            float read = static_cast<float>(write_) - delay;
            if (read < 0.0f)
                read += static_cast<float>(size);
            const int i0 = std::clamp(static_cast<int>(read), 0, size - 1);
            const int i1 = i0 + 1 == size ? 0 : i0 + 1;
            const float t = read - static_cast<float>(i0);
            for (size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels_[c];
                const float tapped =
                    ch.buffer[static_cast<size_t>(i0)] * (1.0f - t) + ch.buffer[static_cast<size_t>(i1)] * t;
                const float echo = ch.lowPass[1].process(ch.lowPass[0].process(tapped));
                const float dry = io[c][n];
                // Into the line through the compander's limit, the echo fed back thinned.
                const float in = dry + feedback * ch.highPass.process(echo);
                ch.buffer[static_cast<size_t>(write_)] = ceiling * std::tanh(in / ceiling);
                io[c][n] = dry + echo * wet;
                wetPeak = std::max(wetPeak, std::abs(echo));
            }
            if (++write_ == size)
                write_ = 0;
        }
        blockExtra_ = wetPeak;
        for (auto& ch : channels_)
            for (auto* f : {&ch.highPass, &ch.lowPass[0], &ch.lowPass[1]})
            {
                if (std::abs(f->z1) < 1.0e-20f)
                    f->z1 = 0.0f;
                if (std::abs(f->z2) < 1.0e-20f)
                    f->z2 = 0.0f;
            }
    }

    struct Channel
    {
        std::vector<float> buffer;
        Biquad highPass;
        std::array<Biquad, 2> lowPass;
    };
    std::array<Channel, 2> channels_;
    int write_ = 0;
    float current_ = 0.0f; // the delay in samples, gliding to Time
    float phase_ = 0.0f;
    float lastBandwidth_ = -1.0f;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("comet::create");
    return std::make_unique<Comet>();
}

} // namespace northlight::plugins::comet
