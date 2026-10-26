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
std::vector<engine::ParameterInfo> gateParameters()
{
    std::vector<engine::ParameterInfo> p(gate::count);
    p[gate::threshold] = {"Threshold", "dB", -80.0f, 0.0f, -40.0f, false};
    p[gate::range] = {"Range", "dB", 0.0f, 80.0f, 40.0f, false};
    p[gate::attack] = {"Attack", "ms", 0.05f, 50.0f, 0.5f, true};
    p[gate::hold] = {"Hold", "ms", 0.0f, 500.0f, 30.0f, false};
    p[gate::release] = {"Release", "ms", 5.0f, 2000.0f, 120.0f, true};
    p[gate::keyHighPass] = {"Key HP", "Hz", 20.0f, 2000.0f, 20.0f, true};
    p[gate::keyLowPass] = {"Key LP", "Hz", 1000.0f, 20000.0f, 20000.0f, true};
    return p;
}

// A key-filtered peak detector opens the gate above the threshold; it closes
// after the hold time once the key falls `hysteresisDb` below it. The gain
// moves between unity and -range at the attack and release rates.
class Gate final : public FeedbackBuiltIn
{
public:
    Gate() : FeedbackBuiltIn(types::gate, gateParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        envelope_ = 0.0f;
        gain_ = 1.0f;
        open_ = true;
        holdLeft_ = 0;
        lastHighPass_ = lastLowPass_ = -1.0f;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float threshold = parameter(gate::threshold);
        // The detector is compared as a level, not in dB: no log per sample.
        const float openLevel = dbToGain(threshold);
        const float closeLevel = dbToGain(threshold - gate::hysteresisDb);
        const float closedGain = dbToGain(-parameter(gate::range));
        const float attack = 1.0f - coefficient(sampleRate_, parameter(gate::attack));
        const float release = 1.0f - coefficient(sampleRate_, parameter(gate::release));
        const int hold = static_cast<int>(sampleRate_ * parameter(gate::hold) * 0.001);
        const float detectorFall = coefficient(sampleRate_, 10.0f);
        const float highPass = parameter(gate::keyHighPass);
        const float lowPass = parameter(gate::keyLowPass);
        const bool useHighPass = highPass > 20.5f;
        const bool useLowPass = lowPass < 19999.0f;
        if (highPass != lastHighPass_ || lowPass != lastLowPass_)
        {
            lastHighPass_ = highPass;
            lastLowPass_ = lowPass;
            for (int ch = 0; ch < 2; ++ch)
            {
                filters_[static_cast<size_t>(ch * 2)].setHighPass(sampleRate_, highPass);
                filters_[static_cast<size_t>(ch * 2 + 1)].setLowPass(sampleRate_, lowPass);
            }
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            float keyL = inL, keyR = inR;
            if (useHighPass)
            {
                keyL = filters_[0].process(keyL);
                keyR = filters_[2].process(keyR);
            }
            if (useLowPass)
            {
                keyL = filters_[1].process(keyL);
                keyR = filters_[3].process(keyR);
            }
            const float key = std::max(std::abs(keyL), std::abs(keyR));
            envelope_ = std::max(key, envelope_ * detectorFall);
            if (envelope_ > openLevel)
            {
                open_ = true;
                holdLeft_ = hold;
            }
            else if (open_ && envelope_ < closeLevel)
            {
                if (holdLeft_ > 0)
                    --holdLeft_;
                else
                    open_ = false;
            }
            const float target = open_ ? 1.0f : closedGain;
            gain_ += (target - gain_) * (target > gain_ ? attack : release);
            left[i] = inL * gain_;
            right[i] = inR * gain_;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    std::max(0.0f, -gainToDb(gain_)),
                                    key);
        }
    }

    std::array<Biquad, 4> filters_; // high-pass and low-pass per channel
    float envelope_ = 0.0f, gain_ = 1.0f;
    bool open_ = true;
    int holdLeft_ = 0;
    float lastHighPass_ = -1.0f, lastLowPass_ = -1.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeGate()
{
    return std::make_unique<Gate>();
}

} // namespace northlight::plugins::strip
