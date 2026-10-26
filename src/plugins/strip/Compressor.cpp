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
std::vector<engine::ParameterInfo> compressorParameters()
{
    std::vector<engine::ParameterInfo> p(compressor::count);
    p[compressor::threshold] = {"Threshold", "dB", -60.0f, 0.0f, -18.0f, false};
    p[compressor::ratio] = {"Ratio", ":1", 1.0f, 20.0f, 3.0f, true};
    p[compressor::attack] = {"Attack", "ms", 0.1f, 100.0f, 10.0f, true};
    p[compressor::release] = {"Release", "ms", 10.0f, 2000.0f, 120.0f, true};
    p[compressor::knee] = {"Knee", "dB", 0.0f, 24.0f, 6.0f, false};
    p[compressor::makeup] = {"Makeup", "dB", 0.0f, 24.0f, 0.0f, false};
    p[compressor::autoMakeup] = choice("Auto makeup", {"Off", "On"}, 0.0f);
    p[compressor::detector] = choice("Detector", {"Peak", "RMS"}, 0.0f);
    p[compressor::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// Feed-forward, stereo-linked: a peak or RMS detector, the soft-knee curve on
// its level, the reduction smoothed with attack and release (smoothing the
// curve's output rather than the level keeps a waveform's troughs from
// dragging the envelope down), make-up (manual plus auto) and an internal
// wet/dry for parallel compression. The one compressor: the channel strip's
// module, and (keyed) the "northlight.compressor" insert, whose detector listens to
// a sidechain key when one is chosen.
class Compressor final : public FeedbackBuiltIn
{
public:
    explicit Compressor(const std::string& type = types::compressor, bool keyed = false)
        : FeedbackBuiltIn(type, compressorParameters()), keyed_(keyed)
    {
    }

    bool acceptsSidechain() const noexcept override { return keyed_; }

private:
    void prepareBuiltIn(double, int) override
    {
        reductionDb_ = 0.0f;
        displayLevel_ = 0.0f;
        meanSquare_ = 0.0f;
        makeup_ = currentMakeup();
    }

    float currentMakeup() const noexcept
    {
        float db = parameter(compressor::makeup);
        if (parameter(compressor::autoMakeup) >= 0.5f)
            db += compressor::autoMakeupDb(parameter(compressor::threshold), parameter(compressor::ratio));
        return dbToGain(db);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float threshold = parameter(compressor::threshold);
        const float ratio = parameter(compressor::ratio);
        const float knee = parameter(compressor::knee);
        const float attack = coefficient(sampleRate_, parameter(compressor::attack));
        const float release = coefficient(sampleRate_, parameter(compressor::release));
        const bool rms = parameter(compressor::detector) >= 0.5f;
        const float rmsCoefficient = coefficient(sampleRate_, 10.0f);
        const float wet = parameter(compressor::mix) * 0.01f;
        const float targetMakeup = currentMakeup();
        const float makeupStep = (targetMakeup - makeup_) / static_cast<float>(std::max(1, numSamples));
        // The detector listens to the key when there is one, else to the audio.
        const float* detectL = keyed_ && keyLeft_ != nullptr ? keyLeft_ : left;
        const float* detectR = keyed_ && keyRight_ != nullptr ? keyRight_ : right;
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            const float keyL = detectL[i], keyR = detectR[i];
            float level = std::max(std::abs(keyL), std::abs(keyR));
            if (rms)
            {
                // Scaled so a sine reads its peak, like the peak detector.
                meanSquare_ = rmsCoefficient * meanSquare_ +
                              (1.0f - rmsCoefficient) * (keyL * keyL + keyR * keyR) * 0.5f;
                level = std::sqrt(2.0f * meanSquare_);
            }
            const float target = compressor::gainReductionDb(gainToDb(level), threshold, ratio, knee);
            const float a = target > reductionDb_ ? attack : release;
            reductionDb_ = a * reductionDb_ + (1.0f - a) * target;
            const float reduction = reductionDb_;
            // The level the editor puts on the curve: a peak envelope with the release.
            displayLevel_ = level > displayLevel_ ? level : level + (displayLevel_ - level) * release;
            makeup_ += makeupStep;
            const float g = dbToGain(-reduction) * makeup_;
            const float mixGain = (1.0f - wet) + wet * g;
            left[i] = inL * mixGain;
            right[i] = inR * mixGain;
            // The input meter shows what the detector hears: the key when there
            // is one (the threshold acts on it), else the audio.
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(keyL), std::abs(keyR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    displayLevel_);
        }
        makeup_ = targetMakeup;
    }

    float reductionDb_ = 0.0f;
    float displayLevel_ = 0.0f;
    float meanSquare_ = 0.0f;
    float makeup_ = 1.0f;
    bool keyed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeCompressor(const std::string& type, bool keyed)
{
    return std::make_unique<Compressor>(type, keyed);
}

} // namespace northlight::plugins::strip
