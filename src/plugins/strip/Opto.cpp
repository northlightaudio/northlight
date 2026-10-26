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
std::vector<engine::ParameterInfo> optoParameters()
{
    std::vector<engine::ParameterInfo> p(opto::count);
    p[opto::peakReduction] = {"Peak reduction", "%", 0.0f, 100.0f, 40.0f, false};
    p[opto::gain] = {"Gain", "dB", -12.0f, 24.0f, 0.0f, false};
    p[opto::mode] = choice("Mode", {"Compress", "Limit"}, 0.0f);
    p[opto::emphasis] = {"Emphasis", "%", 0.0f, 100.0f, 0.0f, false};
    p[opto::colour] = {"Colour", "%", 0.0f, 100.0f, 30.0f, false};
    p[opto::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// A model of an LA-2A style optical compressor, stereo-linked:
//  - Feedback: the detector hears the previous output sample (after the cell,
//    before the make-up), through an optional high shelf (Emphasis). A peak
//    envelope of that, lifted by Peak reduction, goes through a softplus knee
//    times the loop gain; the loop settles at a soft ratio of 1 + loop gain.
//  - The cell: a fast stage follows the target (attack about 30 ms in the
//    loop, which feedback shortens to about 10, release 60 ms). A slow stage
//    charges from it over 200 ms toward a share of it and releases over 0.4 to
//    4 s. Both the share and the slow release grow with the cell's memory,
//    which fills over seconds of heavy compression and drains slower still.
//    The reduction is the larger of the two.
//  - Make-up, then the output valve stage (Colour): the biased tanh with
//    first-order antiderivative anti-aliasing, unity gain for small signals,
//    and a DC blocker. Then the internal wet/dry.
// No lookahead, no latency.
class Opto final : public FeedbackBuiltIn
{
public:
    explicit Opto(const std::string& type = types::opto) : FeedbackBuiltIn(type, optoParameters()) {}

private:
    static constexpr double valveBias = 0.25; // saturation's tube curve
    static inline const double valveSlope = 1.0 - std::tanh(valveBias) * std::tanh(valveBias);
    static float valveDrive(float colourPercent) noexcept { return 0.25f + 0.0175f * colourPercent; }

    void prepareBuiltIn(double, int) override
    {
        envelope_ = fast_ = slow_ = memory_ = 0.0f;
        feedbackL_ = feedbackR_ = 0.0f;
        for (auto& s : shelf_)
            s.reset();
        lastEmphasis_ = std::nanf("");
        for (auto& v : valve_)
            v = {};
        makeup_ = dbToGain(parameter(opto::gain));
        drive_ = valveDrive(parameter(opto::colour));
        dcCoefficient_ = 1.0f - static_cast<float>(2.0 * std::numbers::pi * 10.0 / sampleRate_);
        const float sr = static_cast<float>(sampleRate_);
        const auto step = [sr](float seconds) { return dsp::stepForSeconds(seconds, sr); };
        envelopeFall_ = coefficient(sampleRate_, 20.0f);
        attackStep_ = step(0.030f);
        fastReleaseStep_ = step(opto::fastReleaseMs * 0.001f);
        slowAttackStep_ = step(0.2f);
        memoryFillStep_ = step(2.0f);
        memoryDrainStep_ = step(6.0f);
    }

    // The valve stage on one channel's sample at drive `d` (ADAA1 over the
    // step from the previous sample at the same drive).
    float valve(size_t ch, float in, float d) noexcept
    {
        auto& v = valve_[ch];
        const double x = static_cast<double>(in) * d;
        const double previous = v.previousIn * d;
        const double dx = x - previous;
        double shaped = 0.0;
        if (std::abs(dx) > 1.0e-5)
        {
            // The antiderivative at the previous sample is the one worked out then,
            // unless the drive moved.
            const double fPrevious =
                v.primedDrive == d ? v.previousF : saturation::antiderivative(saturation::tube, previous);
            const double fx = saturation::antiderivative(saturation::tube, x);
            shaped = (fx - fPrevious) / dx;
            v.previousF = fx;
            v.primedDrive = d;
        }
        else
        {
            shaped = saturation::curve(saturation::tube, 0.5 * (x + previous));
            v.primedDrive = -1.0f; // F(x) not worked out: the next step does both ends
        }
        v.previousIn = in;
        // Unity gain for small signals: the curve's slope at rest is sech^2(bias).
        const double y = shaped / (static_cast<double>(d) * valveSlope);
        const double blocked = y - v.dcIn + dcCoefficient_ * v.dcOut;
        v.dcIn = y;
        v.dcOut = blocked;
        return static_cast<float>(blocked);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float peakReduction = parameter(opto::peakReduction);
        const int mode = parameter(opto::mode) >= 0.5f ? 1 : 0;
        const float emphasis = parameter(opto::emphasis);
        const float colour = parameter(opto::colour);
        const float wet = parameter(opto::mix) * 0.01f;
        if (emphasis != lastEmphasis_)
        {
            lastEmphasis_ = emphasis;
            for (auto& s : shelf_)
                s.setHighShelf(sampleRate_, 1500.0, emphasis * 0.12); // up to +12 dB
        }
        const bool emphasise = emphasis > 0.05f;
        const bool coloured = colour > 0.05f || drive_ > valveDrive(0.05f); // or ramping down to clean
        // The slow stage's release follows the memory (slow enough to set per block).
        const float slowReleaseStep =
            dsp::stepForSeconds(opto::slowReleaseSeconds(memory_), static_cast<float>(sampleRate_));
        const float n = static_cast<float>(std::max(1, numSamples));
        const float targetMakeup = dbToGain(parameter(opto::gain));
        const float makeupStep = (targetMakeup - makeup_) / n;
        const float targetDrive = valveDrive(colour);
        const float driveStep = (targetDrive - drive_) / n;
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            // The detector hears the last output of the cell.
            float keyL = feedbackL_, keyR = feedbackR_;
            if (emphasise)
            {
                keyL = shelf_[0].process(keyL);
                keyR = shelf_[1].process(keyR);
            }
            const float rectified = std::max(std::abs(keyL), std::abs(keyR));
            envelope_ =
                rectified > envelope_ ? rectified : rectified + (envelope_ - rectified) * envelopeFall_;
            const float target = opto::cellTargetDb(gainToDb(envelope_), peakReduction, mode);
            fast_ += (target - fast_) * (target > fast_ ? attackStep_ : fastReleaseStep_);
            const float fill = std::clamp(fast_ / 12.0f, 0.0f, 1.0f);
            memory_ += (fill - memory_) * (fill > memory_ ? memoryFillStep_ : memoryDrainStep_);
            const float slowTarget = fast_ * opto::slowShare(memory_);
            slow_ += (slowTarget - slow_) * (slowTarget > slow_ ? slowAttackStep_ : slowReleaseStep);
            const float reduction = std::max(fast_, slow_);
            const float g = dbToGain(-reduction);
            feedbackL_ = inL * g;
            feedbackR_ = inR * g;
            makeup_ += makeupStep;
            drive_ += driveStep;
            float outL = feedbackL_ * makeup_, outR = feedbackR_ * makeup_;
            if (coloured)
            {
                outL = valve(0, outL, drive_);
                outR = valve(1, outR, drive_);
            }
            else
            {
                // Clean: keep the valve's history current, so switching it on
                // does not start from a stale sample.
                valve_[0].previousIn = outL;
                valve_[1].previousIn = outR;
                valve_[0].primedDrive = valve_[1].primedDrive = -1.0f;
            }
            left[i] = inL * (1.0f - wet) + outL * wet;
            right[i] = inR * (1.0f - wet) + outR * wet;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    memory_);
        }
        makeup_ = targetMakeup;
        drive_ = targetDrive;
        // Denormals: a long silence leaves the stages creeping toward zero.
        if (fast_ < 1.0e-6f)
            fast_ = 0.0f;
        if (slow_ < 1.0e-6f)
            slow_ = 0.0f;
    }

    struct Valve
    {
        double previousIn = 0.0;
        double previousF = 0.0;    // the antiderivative at previousIn * primedDrive
        float primedDrive = -1.0f; // the drive previousF was worked out at (-1: none)
        double dcIn = 0.0, dcOut = 0.0;
    };
    std::array<Valve, 2> valve_;
    std::array<Biquad, 2> shelf_;
    float lastEmphasis_ = 0.0f;
    float envelope_ = 0.0f, fast_ = 0.0f, slow_ = 0.0f, memory_ = 0.0f;
    float feedbackL_ = 0.0f, feedbackR_ = 0.0f;
    float makeup_ = 1.0f, drive_ = 1.0f;
    float dcCoefficient_ = 0.999f;
    float envelopeFall_ = 0.0f, attackStep_ = 0.0f, fastReleaseStep_ = 0.0f, slowAttackStep_ = 0.0f;
    float memoryFillStep_ = 0.0f, memoryDrainStep_ = 0.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeOpto(const std::string& type)
{
    return std::make_unique<Opto>(type);
}

} // namespace northlight::plugins::strip
