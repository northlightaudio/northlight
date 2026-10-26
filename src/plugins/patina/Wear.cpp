#include "plugins/patina/Wear.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Patina.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

void Wear::prepare(double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("patina::Wear::prepare");
    fs_ = sampleRate;
    // The skew: up to 0.35 ms either way.
    for (auto& line : skewLine_)
        line.prepare(static_cast<int>(0.002 * sampleRate) + 8);
    reset();
}

void Wear::reset() noexcept
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        low_[ch].reset();
        high_[ch].reset();
        bump_[ch].reset();
        skewFrom_[ch] = skewTo_[ch] = 0.0f;
        skewGain_[ch] = 1.0f;
    }
    dropRemaining_ = 0;
    dropDip_ = 0.0f;
    dropDepthDb_ = 0.0f;
    dropFrom_ = dropTo_ = 1.0f;
    designed_ = {-1.0f, -1.0f};
    skewShape_ = {};
    skewLevel_ = {};
}

void Wear::controls(int n, float seconds, bool first, Accumulator& acc) noexcept
{
    // Dropouts arrive at random, up to three a second, each a dip of 3 to
    // 27 dB lasting 20 to 220 ms, the highs lost deeper.
    const float d = dropouts;
    if (dropRemaining_ <= 0 && d > 0.0f && rng_.uniform() < 3.0f * d * d * seconds)
    {
        dropRemaining_ = static_cast<int>((0.02f + 0.2f * rng_.uniform()) * static_cast<float>(fs_));
        dropDepthDb_ = 3.0f + (6.0f + 18.0f * d) * rng_.uniform();
    }
    const bool dropping = dropRemaining_ > 0;
    dropRemaining_ -= n;
    const float k = smoothingCoefficient(fs_ / static_cast<double>(n), dropping ? 0.004f : 0.03f);
    dropDip_ += ((dropping ? 1.0f : 0.0f) - dropDip_) * k;
    if (dropDip_ < 1.0e-4f)
        dropDip_ = 0.0f;
    dropFrom_ = dropTo_;
    dropTo_ = dbToGain(-dropDepthDb_ * dropDip_);
    if (first)
        dropFrom_ = dropTo_;

    // Skew: the azimuth wanders, the right channel ahead or behind by up
    // to 0.35 ms, the lagging side duller and the levels apart.
    const float shape = skewShape_.bipolar(seconds, 0.08f, rng_);
    const float levelShape = skewLevel_.bipolar(seconds, 0.11f, rng_);
    const float lagMs = 0.35f * skew * shape;
    const float lag = lagMs * 0.001f * static_cast<float>(fs_);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        skewFrom_[ch] = skewTo_[ch];
        skewTo_[ch] = ch == 0 ? std::max(0.0f, -lag) : std::max(0.0f, lag);
        if (first)
            skewFrom_[ch] = skewTo_[ch];
        skewGain_[ch] = dbToGain((ch == 0 ? 1.0f : -1.0f) * 1.2f * skew * levelShape);
    }
    const float base = wearCutoffHz(loss) * (1.0f - 0.75f * dropDip_);
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const bool lagging = (ch == 1) == (lag > 0.0f);
        const float cutoff = base * (lagging ? 1.0f - 0.35f * skew * std::abs(shape) : 1.0f);
        if (std::abs(cutoff - designed_[ch]) > 0.002f * designed_[ch] || first)
        {
            designed_[ch] = cutoff;
            high_[ch].setLowPass(fs_, std::min(cutoff, static_cast<float>(fs_) * 0.45f), 0.75);
            low_[ch].setHighPass1(fs_, 10.0 + 70.0 * loss * loss);
            bump_[ch].setPeaking(fs_, 110.0, 1.0, 1.5 * loss);
        }
    }
    acc.wearDip = std::max(acc.wearDip, dropDepthDb_ * dropDip_);
    acc.wearCutoff = base;
    acc.wearSkew = lagMs;
}

} // namespace northlight::plugins::patina
