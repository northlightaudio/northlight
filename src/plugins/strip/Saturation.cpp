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
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::strip
{

namespace
{
std::vector<engine::ParameterInfo> saturationParameters()
{
    std::vector<engine::ParameterInfo> p(saturation::count);
    p[saturation::drive] = {"Drive", "dB", 0.0f, 36.0f, 6.0f, false};
    p[saturation::type] = choice("Type", {"Tape", "Tube", "Clip"}, 0.0f);
    p[saturation::tone] = {"Tone", "", -1.0f, 1.0f, 0.0f, false};
    p[saturation::output] = {"Output", "dB", -24.0f, 12.0f, 0.0f, false};
    p[saturation::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// Drive into a static curve with first-order antiderivative anti-aliasing (no
// oversampling, so no latency), a DC blocker for the biased curve, a tone
// shelf, output trim and an internal wet/dry.
class Saturation final : public FeedbackBuiltIn
{
public:
    Saturation() : FeedbackBuiltIn(types::saturation, saturationParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& s : state_)
            s = {};
        lastTone_ = std::nanf("");
        drive_ = dbToGain(parameter(saturation::drive));
        trim_ = currentTrim();
        dcCoefficient_ = 1.0f - static_cast<float>(2.0 * std::numbers::pi * 10.0 / sampleRate_);
    }

    float currentTrim() const noexcept
    {
        const float drive = parameter(saturation::drive);
        return dbToGain(saturation::driveCompensationDb(drive) + parameter(saturation::output));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const int type = std::clamp(static_cast<int>(std::lround(parameter(saturation::type))), 0, 2);
        if (type != type_)
        {
            type_ = type;
            for (auto& s : state_)
            {
                s.previous = 0.0; // the antiderivative changed: restart from silence
                s.primed = false;
            }
        }
        const float tone = parameter(saturation::tone);
        const bool ramp = tone != lastTone_;
        Biquad from = state_[0].shelf;
        Biquad to;
        to.setHighShelf(sampleRate_, 3000.0, tone * 6.0);
        if (std::isnan(lastTone_))
            for (auto& s : state_)
                s.shelf.blend(to, to, 1.0f);
        const bool rampNow = ramp && !std::isnan(lastTone_);
        lastTone_ = tone;
        const float wet = parameter(saturation::mix) * 0.01f;
        const float targetDrive = dbToGain(parameter(saturation::drive));
        const float targetTrim = currentTrim();
        const float n = static_cast<float>(std::max(1, numSamples));
        const float driveStep = (targetDrive - drive_) / n;
        const float trimStep = (targetTrim - trim_) / n;
        float inPeak = 0.0f, outPeak = 0.0f, drivenPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            drive_ += driveStep;
            trim_ += trimStep;
            if (rampNow)
            {
                const float t = static_cast<float>(i + 1) / n;
                for (auto& s : state_)
                    s.shelf.blend(from, to, t);
            }
            float* samples[2] = {left + i, right + i};
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& s = state_[ch];
                const float in = *samples[ch];
                const double x = static_cast<double>(in) * drive_;
                drivenPeak = std::max(drivenPeak, static_cast<float>(std::abs(x)));
                // ADAA1: the mean of the curve over the step from the previous input.
                const double dx = x - s.previous;
                double y = 0.0;
                if (std::abs(dx) > 1.0e-5)
                {
                    // F at the previous sample was worked out then (x already holds the drive).
                    const double fPrevious =
                        s.primed ? s.previousF : saturation::antiderivative(type_, s.previous);
                    const double fx = saturation::antiderivative(type_, x);
                    y = (fx - fPrevious) / dx;
                    s.previousF = fx;
                    s.primed = true;
                }
                else
                {
                    y = saturation::curve(type_, 0.5 * (x + s.previous));
                    s.primed = false;
                }
                s.previous = x;
                // DC blocker (the tube curve is asymmetric).
                const double blocked = y - s.dcIn + dcCoefficient_ * s.dcOut;
                s.dcIn = y;
                s.dcOut = blocked;
                y = s.shelf.process(static_cast<float>(blocked)) * trim_;
                const float out = in * (1.0f - wet) + static_cast<float>(y) * wet;
                inPeak = std::max(inPeak, std::abs(in));
                outPeak = std::max(outPeak, std::abs(out));
                *samples[ch] = out;
            }
        }
        drive_ = targetDrive;
        trim_ = targetTrim;
        if (feedback_.watched())
            feedback_.addBlock(inPeak, outPeak, 0.0f, drivenPeak, numSamples);
    }

    struct Channel
    {
        double previous = 0.0;
        double previousF = 0.0; // the antiderivative at `previous` (valid when primed)
        bool primed = false;
        double dcIn = 0.0, dcOut = 0.0;
        Biquad shelf;
    };
    std::array<Channel, 2> state_;
    int type_ = 0;
    float lastTone_ = 0.0f;
    float drive_ = 1.0f, trim_ = 1.0f;
    float dcCoefficient_ = 0.999f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeSaturation()
{
    return std::make_unique<Saturation>();
}

} // namespace northlight::plugins::strip
