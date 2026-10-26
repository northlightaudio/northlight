#include "plugins/Biquad.h"
#include "plugins/ChannelStrip.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/strip/Modules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace northlight::plugins::strip
{

namespace
{
std::vector<engine::ParameterInfo> limiterParameters()
{
    std::vector<engine::ParameterInfo> p(limiter::count);
    p[limiter::input] = {"Input", "dB", 0.0f, 24.0f, 0.0f, false};
    p[limiter::ceiling] = {"Ceiling", "dB", -12.0f, 0.0f, -0.3f, false};
    p[limiter::release] = {"Release", "ms", 1.0f, 1000.0f, 60.0f, true};
    return p;
}

// Zero-latency peak limiter: the gain drops at once to what the sample needs
// and recovers at the release rate; the output is clamped to the ceiling. No
// lookahead, so a strip that is switched off adds no delay to the channel.
class PeakLimiter final : public FeedbackBuiltIn
{
public:
    PeakLimiter() : FeedbackBuiltIn(types::limiter, limiterParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        gain_ = 1.0f;
        input_ = dbToGain(parameter(limiter::input));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float ceiling = dbToGain(parameter(limiter::ceiling));
        const float release = coefficient(sampleRate_, parameter(limiter::release));
        const float targetInput = dbToGain(parameter(limiter::input));
        const float inputStep = (targetInput - input_) / static_cast<float>(std::max(1, numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            input_ += inputStep;
            const float l = left[i] * input_, r = right[i] * input_;
            const float peak = std::max(std::abs(l), std::abs(r));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            gain_ = needed < gain_ ? needed : std::min(1.0f, release * gain_ + (1.0f - release));
            left[i] = std::clamp(l * gain_, -ceiling, ceiling);
            right[i] = std::clamp(r * gain_, -ceiling, ceiling);
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(peak,
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    std::max(0.0f, -gainToDb(gain_)),
                                    0.0f);
        }
        input_ = targetInput;
    }

    float gain_ = 1.0f;
    float input_ = 1.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeLimiter()
{
    return std::make_unique<PeakLimiter>();
}

} // namespace northlight::plugins::strip
