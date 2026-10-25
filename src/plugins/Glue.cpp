#include "plugins/Glue.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace northlight::plugins::glue
{

float gainReductionDb(float levelDb, float thresholdDb, float ratio) noexcept
{
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    if (2.0f * over <= -kneeDb)
        return 0.0f;
    if (2.0f * over >= kneeDb)
        return over * slope;
    // Inside the knee: the quadratic that meets both straight parts.
    const float x = over + 0.5f * kneeDb;
    return slope * x * x / (2.0f * kneeDb);
}

namespace
{
template <size_t N> float pick(const std::array<float, N>& values, float choice) noexcept
{
    const int i = std::clamp(static_cast<int>(std::lround(choice)), 0, static_cast<int>(N) - 1);
    return values[static_cast<size_t>(i)];
}
} // namespace

float ratioFor(float choice) noexcept
{
    return pick(ratios, choice);
}

float attackMsFor(float choice) noexcept
{
    return pick(attacksMs, choice);
}

bool isAutoRelease(float choice) noexcept
{
    return std::lround(choice) >= autoRelease;
}

float releaseSecondsFor(float choice) noexcept
{
    return isAutoRelease(choice) ? autoSlowSeconds : pick(releasesSeconds, choice);
}

namespace
{
std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[threshold] = {"Threshold", "dB", -40.0f, 0.0f, -18.0f, false};
    p[ratio] = choice("Ratio", {"1.5:1", "2:1", "4:1", "10:1"}, 1.0f);
    p[attack] = choice("Attack", {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"}, 4.0f);
    p[release] =
        choice("Release", {"0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto"}, static_cast<float>(autoRelease));
    p[makeup] = {"Makeup", "dB", 0.0f, 20.0f, 0.0f, false};
    p[keyHighPass] = {"Key HP", "Hz", keyHighPassOffHz, 400.0f, keyHighPassOffHz, true};
    p[mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// Stereo linked: one detector for both sides, on the louder of the two after the
// key high-pass. The reduction is smoothed in dB: toward a larger reduction at
// the attack rate, back at the release rate. Auto release runs two stages: the
// fast one releases over 0.1 s, the slow one charges from it over 0.4 s while the
// compression is held and releases over 1.2 s, and the reduction is the larger. A short burst barely
// charges the slow stage and recovers quickly; seconds of compression charge it
// and recover slowly, without pumping.
class Glue final : public ProcessorBase
{
public:
    Glue() : ProcessorBase(type, parameterInfos()) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

private:
    void prepareAudio(double sampleRate, int) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Glue::prepareAudio");
        feedback_.prepare(sampleRate);
        fast_ = slow_ = 0.0f;
        inMeanSquare_ = outMeanSquare_ = 0.0f;
        for (auto& f : keyFilter_)
            f.reset();
        lastKeyHz_ = -1.0f;
        makeup_ = dbToGain(parameter(makeup));
        rmsCoefficient_ = coefficient(rmsSeconds);
        chargeStep_ = 1.0f - coefficient(autoChargeSeconds);
    }

    float coefficient(float seconds) const noexcept
    {
        return dsp::poleForSeconds(std::max(1.0e-5f, seconds), static_cast<float>(sampleRate_));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float thresholdDb = parameter(threshold);
        const float slope = ratioFor(parameter(ratio));
        const float attackStep = 1.0f - coefficient(attackMsFor(parameter(attack)) * 0.001f);
        const bool autoMode = isAutoRelease(parameter(release));
        const float releaseStep =
            1.0f - coefficient(autoMode ? autoFastSeconds : releaseSecondsFor(parameter(release)));
        const float slowReleaseStep = 1.0f - coefficient(autoSlowSeconds);
        const float wet = std::clamp(parameter(mix) * 0.01f, 0.0f, 1.0f);
        const float keyHz = parameter(keyHighPass);
        const bool filtered = keyHz > keyHighPassOffHz + 0.5f;
        if (filtered && keyHz != lastKeyHz_)
        {
            lastKeyHz_ = keyHz;
            for (auto& f : keyFilter_)
                f.setHighPass(sampleRate_, keyHz);
        }
        const float targetMakeup = dbToGain(parameter(makeup));
        const float makeupStep = (targetMakeup - makeup_) / static_cast<float>(std::max(1, numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            float keyL = inL, keyR = inR;
            if (filtered)
            {
                keyL = keyFilter_[0].process(inL);
                keyR = keyFilter_[1].process(inR);
            }
            const float level = std::max(std::abs(keyL), std::abs(keyR));
            const float target = gainReductionDb(gainToDb(level), thresholdDb, slope);
            fast_ += (target - fast_) * (target > fast_ ? attackStep : releaseStep);
            float reduction = fast_;
            if (autoMode)
            {
                // The slow stage charges only while the compression is held (the
                // detector still asks for at least what the fast stage gives), so a
                // hit that has passed does not keep charging it on its way down.
                if (fast_ > slow_ && target >= fast_)
                    slow_ += (fast_ - slow_) * chargeStep_;
                else if (fast_ < slow_)
                    slow_ += (fast_ - slow_) * slowReleaseStep;
                reduction = std::max(fast_, slow_);
            }
            else
                slow_ = fast_; // ready for a switch to Auto
            makeup_ += makeupStep;
            const float g = dbToGain(-reduction) * makeup_;
            const float mixGain = (1.0f - wet) + wet * g;
            left[i] = inL * mixGain;
            right[i] = inR * mixGain;

            const float inSquare = 0.5f * (inL * inL + inR * inR);
            const float outSquare = 0.5f * (left[i] * left[i] + right[i] * right[i]);
            inMeanSquare_ = rmsCoefficient_ * inMeanSquare_ + (1.0f - rmsCoefficient_) * inSquare;
            outMeanSquare_ = rmsCoefficient_ * outMeanSquare_ + (1.0f - rmsCoefficient_) * outSquare;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    std::sqrt(inMeanSquare_),
                                    std::sqrt(outMeanSquare_));
        }
        makeup_ = targetMakeup;
        // A long silence leaves the stages creeping toward zero: no denormals.
        if (fast_ < 1.0e-6f)
            fast_ = 0.0f;
        if (slow_ < 1.0e-6f)
            slow_ = 0.0f;
        if (inMeanSquare_ < 1.0e-20f)
            inMeanSquare_ = 0.0f;
        if (outMeanSquare_ < 1.0e-20f)
            outMeanSquare_ = 0.0f;
    }

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        feedback_.addBypassed(left, right, numSamples);
    }

    engine::ProcessorFeedback feedback_;
    std::array<Biquad, 2> keyFilter_;
    float lastKeyHz_ = -1.0f;
    float fast_ = 0.0f, slow_ = 0.0f;
    float inMeanSquare_ = 0.0f, outMeanSquare_ = 0.0f;
    float makeup_ = 1.0f;
    float rmsCoefficient_ = 0.0f, chargeStep_ = 0.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Glue>();
}

} // namespace northlight::plugins::glue
