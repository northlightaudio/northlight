#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Oversampler.h"
#include "plugins/Pedals.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace northlight::plugins::flare
{

namespace
{
constexpr int oversampling = 4;
// The op-amp: its gain-bandwidth product and the rails it swings to.
constexpr double gainBandwidthHz = 1.5e6;
constexpr float railVolts = 4.2f;
// Where the gain's lower leg comes in: below it the gain falls by bassCutDb.
constexpr double shelfHz = 160.0;
constexpr float bassCutDb = 15.0f;
constexpr double couplingHz = 20.0;
constexpr double topHz = 12000.0;
// The clippers: their knee (volts) and how sharp it is.
constexpr std::array<float, 3> kneeVolts{0.62f, 1.75f, railVolts};
constexpr std::array<float, 3> sharpness{2.5f, 2.0f, 0.0f};
// The level a fully driven clipper is brought to before Level, so the three
// clippers and every Drive sit close in loudness.
constexpr float normalPeak = 0.5f;
} // namespace

float gainDb(float drive) noexcept
{
    return 6.0f + 6.0f * std::clamp(drive, 0.0f, 10.0f);
}

float toneHz(float tone) noexcept
{
    return 450.0f * std::pow(32.0f, std::clamp(tone, 0.0f, 10.0f) * 0.1f);
}

float clipCurve(int clipper, float volts) noexcept
{
    const auto c = static_cast<size_t>(std::clamp(clipper, 0, 2));
    // The op-amp's rails round everything off first.
    const float railed = railVolts * std::tanh(volts / railVolts);
    if (c == open)
        return railed;
    const float knee = kneeVolts[c], n = sharpness[c];
    return railed / std::pow(1.0f + std::pow(std::abs(railed) / knee, n), 1.0f / n);
}

namespace
{
class Flare final : public MeteredBuiltIn
{
public:
    Flare()
        : MeteredBuiltIn(type,
                         {{"Drive", "", 0.0f, 10.0f, 5.0f, false},
                          {"Tone", "", 0.0f, 10.0f, 5.0f, false},
                          {"Level", "", 0.0f, 10.0f, 5.0f, false},
                          {"Clip", "", 0.0f, 2.0f, 0.0f, false, {"Silicon", "LED", "Open"}}})
    {
    }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& ch : channels_)
        {
            ch.coupling.setHighPass1(sampleRate, couplingHz);
            ch.shelf.setLowPass1(sampleRate, shelfHz);
            ch.top.setLowPass(sampleRate, topHz, 0.6);
            for (auto* f : {&ch.coupling, &ch.shelf, &ch.bandwidth, &ch.tone, &ch.top})
                f->reset();
            ch.oversampler.setFactor(oversampling);
        }
        lastGain_ = lastTone_ = -1.0f;
        primed_ = false; // the first block takes the level as it is then
    }

    float targetOutput() const noexcept
    {
        const int c = std::clamp(static_cast<int>(std::lround(parameter(clip))), 0, 2);
        const float levelDb = -30.0f + 3.6f * parameter(level);
        return dbToGain(levelDb) * normalPeak / kneeVolts[static_cast<size_t>(c)];
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const float gainDecibels = gainDb(parameter(drive));
        const float tone = toneHz(parameter(flare::tone));
        if (gainDecibels != lastGain_)
        {
            lastGain_ = gainDecibels;
            highGain_ = dbToGain(gainDecibels);
            lowGain_ = std::max(1.0f, highGain_ * dbToGain(-bassCutDb));
            const double corner = std::min(20000.0, gainBandwidthHz / highGain_);
            for (auto& ch : channels_)
                ch.bandwidth.setLowPass1(sampleRate_, std::min(corner, 0.45 * sampleRate_));
        }
        if (tone != lastTone_)
        {
            lastTone_ = tone;
            for (auto& ch : channels_)
                ch.tone.setLowPass1(sampleRate_, std::min<double>(tone, 0.45 * sampleRate_));
        }
        const int clipper = std::clamp(static_cast<int>(std::lround(parameter(clip))), 0, 2);
        const float target = targetOutput();
        if (!primed_)
        {
            output_ = target;
            primed_ = true;
        }
        const float step = (target - output_) / static_cast<float>(std::max(1, numSamples));
        float drivePeak = 0.0f;
        float* io[2] = {left, right};
        for (int i = 0; i < numSamples; ++i)
        {
            output_ += step;
            for (size_t c = 0; c < 2; ++c)
            {
                auto& ch = channels_[c];
                const float x = ch.coupling.process(io[c][i]);
                // The gain rises with frequency: the lows through the lower leg only.
                const float low = ch.shelf.process(x);
                const float stage = ch.bandwidth.process(low * lowGain_ + (x - low) * highGain_);
                drivePeak = std::max(drivePeak, std::abs(stage));
                std::array<float, Oversampler::maxFactor> up{};
                ch.oversampler.up(stage, up.data());
                for (int k = 0; k < oversampling; ++k)
                    up[static_cast<size_t>(k)] = clipCurve(clipper, up[static_cast<size_t>(k)]);
                const float clipped = ch.oversampler.down(up.data());
                io[c][i] = ch.top.process(ch.tone.process(clipped)) * output_;
            }
        }
        output_ = target;
        blockExtra_ = drivePeak / kneeVolts[static_cast<size_t>(clipper)];
        for (auto& ch : channels_)
        {
            ch.oversampler.flush();
            for (auto* f : {&ch.coupling, &ch.shelf, &ch.bandwidth, &ch.tone, &ch.top})
            {
                if (std::abs(f->z1) < 1.0e-20f)
                    f->z1 = 0.0f;
                if (std::abs(f->z2) < 1.0e-20f)
                    f->z2 = 0.0f;
            }
        }
    }

    struct Channel
    {
        Biquad coupling, shelf, bandwidth, tone, top;
        Oversampler oversampler;
    };
    std::array<Channel, 2> channels_;
    float lastGain_ = -1.0f, lastTone_ = -1.0f;
    float highGain_ = 1.0f, lowGain_ = 1.0f;
    float output_ = 1.0f;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("flare::create");
    return std::make_unique<Flare>();
}

} // namespace northlight::plugins::flare
