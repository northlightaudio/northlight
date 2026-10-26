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
std::vector<engine::ParameterInfo> eqParameters()
{
    std::vector<engine::ParameterInfo> infos;
    const std::vector<std::string> slopeNames{"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"};
    const std::vector<std::string> shapes{"Peak", "Low shelf", "High shelf", "Notch"};
    const std::vector<std::string> offOn{"Off", "On"};
    infos.push_back(choice("Low cut", offOn, 0.0f));
    infos.push_back(choice("Low cut slope", slopeNames, 1.0f));
    infos.push_back({"Low cut freq", "Hz", 20.0f, 2000.0f, 80.0f, true});
    const float freqs[4] = {100.0f, 400.0f, 2500.0f, 8000.0f};
    const float shapeDefaults[4] = {1.0f, 0.0f, 0.0f, 2.0f};
    const float qs[4] = {0.71f, 1.0f, 1.0f, 0.71f};
    for (int b = 0; b < 4; ++b)
    {
        const auto n = "Band " + std::to_string(b + 1);
        infos.push_back(choice(n, offOn, 1.0f));
        infos.push_back(choice(n + " shape", shapes, shapeDefaults[b]));
        infos.push_back({n + " freq", "Hz", 20.0f, 20000.0f, freqs[b], true});
        infos.push_back({n + " gain", "dB", -24.0f, 24.0f, 0.0f, false});
        infos.push_back({n + " Q", "", 0.1f, 18.0f, qs[b], true});
    }
    infos.push_back(choice("High cut", offOn, 0.0f));
    infos.push_back(choice("High cut slope", slopeNames, 1.0f));
    infos.push_back({"High cut freq", "Hz", 1000.0f, 20000.0f, 16000.0f, true});
    infos.push_back({"Output", "dB", -24.0f, 24.0f, 0.0f, false});
    return infos;
}

// Six bands in a fixed layout of twelve sections (identity where a band is
// off, skipped). A parameter change ramps the coefficients across the block.
class SixBandEq final : public FeedbackBuiltIn
{
public:
    SixBandEq() : FeedbackBuiltIn(types::eq, eqParameters(), true) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& channel : live_)
            for (auto& s : channel)
            {
                s.bypass();
                s.reset();
            }
        last_.fill(std::nanf(""));
        outputGain_ = dbToGain(parameter(eq::output));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const bool watched = feedback_.watched();
        if (watched)
            feedback_.tap(0, left, right, numSamples);
        const float inPeak = watched ? peakOf(left, right, numSamples) : 0.0f;
        const bool ramp = updateCoefficients();
        bool active[eq::maxSections];
        for (int s = 0; s < eq::maxSections; ++s)
            active[s] = !live_[0][static_cast<size_t>(s)].isIdentity() ||
                        (ramp && !(previous_[static_cast<size_t>(s)].isIdentity() &&
                                   targets_[static_cast<size_t>(s)].isIdentity()));
        const float targetGain = dbToGain(parameter(eq::output));
        const float gainStep = (targetGain - outputGain_) / static_cast<float>(std::max(1, numSamples));
        float g = outputGain_;
        for (int i = 0; i < numSamples; ++i)
        {
            if (ramp)
            {
                const float t = static_cast<float>(i + 1) / static_cast<float>(numSamples);
                for (size_t s = 0; s < eq::maxSections; ++s)
                    if (active[s])
                    {
                        live_[0][s].blend(previous_[s], targets_[s], t);
                        live_[1][s].blend(previous_[s], targets_[s], t);
                    }
            }
            float l = left[i], r = right[i];
            for (size_t s = 0; s < eq::maxSections; ++s)
                if (active[s])
                {
                    l = live_[0][s].process(l);
                    r = live_[1][s].process(r);
                }
            g += gainStep;
            left[i] = l * g;
            right[i] = r * g;
        }
        outputGain_ = targetGain;
        if (ramp) // land exactly on the targets, so a band switched off reads as identity again
            for (auto& channel : live_)
                for (size_t s = 0; s < eq::maxSections; ++s)
                    channel[s].blend(targets_[s], targets_[s], 1.0f);
        if (watched)
        {
            feedback_.addBlock(inPeak, peakOf(left, right, numSamples), 0.0f, 0.0f, numSamples);
            feedback_.tap(1, left, right, numSamples);
        }
    }

    // Recomputes the targets when a parameter changed; true when this block
    // should ramp to them.
    bool updateCoefficients() noexcept
    {
        std::array<float, eq::count> params{};
        bool changed = false;
        for (int i = 0; i < eq::count; ++i)
        {
            params[static_cast<size_t>(i)] = parameter(i);
            changed = changed || params[static_cast<size_t>(i)] != last_[static_cast<size_t>(i)];
        }
        if (!changed)
            return false;
        const bool first = std::isnan(last_[0]);
        last_ = params;
        previous_ = live_[0];
        eq::design(params.data(), sampleRate_, targets_.data());
        for (size_t s = 0; s < eq::maxSections; ++s)
        {
            // A section switching on or off starts from (or ends at) its memory
            // cleared, so an old state never rings out of a re-enabled band.
            if (previous_[s].isIdentity() && !targets_[s].isIdentity())
            {
                live_[0][s].reset();
                live_[1][s].reset();
            }
        }
        if (first)
        {
            for (auto& channel : live_)
                for (size_t s = 0; s < eq::maxSections; ++s)
                    channel[s].blend(targets_[s], targets_[s], 1.0f);
            return false;
        }
        return true;
    }

    std::array<std::array<Biquad, eq::maxSections>, 2> live_;
    std::array<Biquad, eq::maxSections> previous_, targets_;
    std::array<float, eq::count> last_{};
    float outputGain_ = 1.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> makeEq()
{
    return std::make_unique<SixBandEq>();
}

} // namespace northlight::plugins::strip
