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
std::vector<engine::ParameterInfo> deesserParameters()
{
    std::vector<engine::ParameterInfo> p(deesser::count);
    p[deesser::frequency] = {"Frequency", "Hz", 2000.0f, 16000.0f, 6000.0f, true};
    p[deesser::threshold] = {"Threshold", "dB", -60.0f, 0.0f, -30.0f, false};
    p[deesser::range] = {"Range", "dB", 0.0f, 24.0f, 8.0f, false};
    p[deesser::mode] = choice("Mode", {"Split", "Wide"}, 0.0f);
    p[deesser::listen] = choice("Listen", {"Off", "On"}, 0.0f);
    return p;
}

// A 24 dB Linkwitz-Riley crossover splits the signal at the frequency (the two
// bands sum back flat). The high band's envelope above the threshold sets the
// reduction, applied to the high band alone (split) or the whole signal
// (wide). Listen plays the band.
class DeEsser final : public FeedbackBuiltIn
{
public:
    DeEsser() : FeedbackBuiltIn(types::deesser, deesserParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        envelope_ = 0.0f;
        lastFrequency_ = -1.0f;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float frequency = parameter(deesser::frequency);
        if (frequency != lastFrequency_)
        {
            lastFrequency_ = frequency;
            // LR4: each band is two Butterworth sections in series.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* f = filters_.data() + ch * 4;
                f[0].setLowPass(sampleRate_, frequency);
                f[1].setLowPass(sampleRate_, frequency);
                f[2].setHighPass(sampleRate_, frequency);
                f[3].setHighPass(sampleRate_, frequency);
            }
        }
        const float threshold = parameter(deesser::threshold);
        const float range = parameter(deesser::range);
        const bool wide = parameter(deesser::mode) >= 0.5f;
        const bool listen = parameter(deesser::listen) >= 0.5f;
        const float attack = coefficient(sampleRate_, 0.5f);
        const float release = coefficient(sampleRate_, 60.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            const float lowL = filters_[1].process(filters_[0].process(inL));
            const float lowR = filters_[5].process(filters_[4].process(inR));
            const float bandL = filters_[3].process(filters_[2].process(inL));
            const float bandR = filters_[7].process(filters_[6].process(inR));
            const float detect = std::max(std::abs(bandL), std::abs(bandR));
            const float a = detect > envelope_ ? attack : release;
            envelope_ = detect + (envelope_ - detect) * a;
            const float reduction = deesser::reductionDb(gainToDb(envelope_), threshold, range);
            const float g = dbToGain(-reduction);
            if (listen)
            {
                left[i] = bandL;
                right[i] = bandR;
            }
            else if (wide)
            {
                left[i] = inL * g;
                right[i] = inR * g;
            }
            else
            {
                left[i] = lowL + bandL * g;
                right[i] = lowR + bandR * g;
            }
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    envelope_);
        }
    }

    std::array<Biquad, 8> filters_; // per channel: low-pass x2, high-pass x2
    float envelope_ = 0.0f;
    float lastFrequency_ = -1.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeDeEsser()
{
    return std::make_unique<DeEsser>();
}

} // namespace northlight::plugins::strip
