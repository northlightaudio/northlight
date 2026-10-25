#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Pedals.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::gravity
{

namespace
{
constexpr float kneeDb = 10.0f;
// Make-up is the reduction a firmly played note gets: at this level a note comes
// out where it went in, and everything quieter comes up toward it.
constexpr float playedDb = -14.0f;
// The detector's bass cut, and the OTA stage's treble roll-off.
constexpr double detectorHighPassHz = 90.0;
constexpr double rollOffHz = 7000.0;
// The release: fast back from a pick, slow back from held compression.
constexpr float fastReleaseMs = 70.0f;
constexpr float slowAttackMs = 250.0f, slowReleaseMs = 900.0f;
// Below this the expander turns the gain down 1:2, so the make-up does not
// lift the hiss between notes.
constexpr float floorDb = -64.0f;

float reductionDb(const Curve& c, float inputDb) noexcept
{
    const float over = inputDb - c.thresholdDb;
    const float slope = 1.0f - 1.0f / c.ratio;
    if (over <= -kneeDb * 0.5f)
        return 0.0f;
    if (over >= kneeDb * 0.5f)
        return over * slope;
    const float x = over + kneeDb * 0.5f;
    return slope * x * x / (2.0f * kneeDb);
}

float coefficient(float ms, double sampleRate) noexcept
{
    return dsp::poleForMs(std::max(0.01f, ms), static_cast<float>(sampleRate));
}
} // namespace

Curve curveFor(float sustain) noexcept
{
    const float t = std::clamp(sustain, 0.0f, 10.0f) * 0.1f;
    Curve c{-10.0f - 32.0f * t, 2.0f + 18.0f * std::pow(t, 1.5f), 0.0f};
    c.makeupDb = reductionDb(c, playedDb);
    return c;
}

float attackMs(float attack) noexcept
{
    return 0.5f * std::pow(80.0f, std::clamp(attack, 0.0f, 10.0f) * 0.1f); // 0.5 .. 40 ms
}

float staticGainDb(float sustain, float inputDb) noexcept
{
    const auto c = curveFor(sustain);
    return c.makeupDb - reductionDb(c, inputDb);
}

namespace
{
class Gravity final : public MeteredBuiltIn
{
public:
    Gravity()
        : MeteredBuiltIn(type,
                         {{"Sustain", "", 0.0f, 10.0f, 5.0f, false},
                          {"Attack", "", 0.0f, 10.0f, 4.0f, false},
                          {"Blend", "", 0.0f, 10.0f, 10.0f, false},
                          {"Level", "dB", -18.0f, 18.0f, 0.0f, false}})
    {
    }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& f : detector_)
        {
            f.setHighPass1(sampleRate, detectorHighPassHz);
            f.reset();
        }
        for (auto& f : rollOff_)
        {
            f.setLowPass1(sampleRate, rollOffHz);
            f.reset();
        }
        fast_ = slow_ = 0.0f;
        fastRelease_ = coefficient(fastReleaseMs, sampleRate);
        slowAttack_ = coefficient(slowAttackMs, sampleRate);
        slowRelease_ = coefficient(slowReleaseMs, sampleRate);
        primed_ = false; // the first block takes the settings as they are then
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const auto curve = curveFor(parameter(sustain));
        const float attack = coefficient(attackMs(parameter(gravity::attack)), sampleRate_);
        const float levelTarget = dbToGain(parameter(gravity::level));
        const float blendTarget = parameter(gravity::blend) * 0.1f;
        if (!primed_)
        {
            level_ = levelTarget;
            blend_ = blendTarget;
            primed_ = true;
        }
        const float levelStep = (levelTarget - level_) / static_cast<float>(std::max(1, numSamples));
        const float blendStep = (blendTarget - blend_) / static_cast<float>(std::max(1, numSamples));
        float peakEnvelope = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i], r = right[i];
            // Linked: the louder side of the bass-cut signal drives both.
            const float key = std::max(std::abs(detector_[0].process(l)), std::abs(detector_[1].process(r)));
            fast_ = key > fast_ ? attack * fast_ + (1.0f - attack) * key : fastRelease_ * fast_;
            slow_ = key > slow_ ? slowAttack_ * slow_ + (1.0f - slowAttack_) * key : slowRelease_ * slow_;
            const float envelope = std::max(fast_, slow_);
            peakEnvelope = std::max(peakEnvelope, envelope);
            const float envelopeDb = gainToDb(envelope);
            const float reduction = reductionDb(curve, envelopeDb);
            float gainDb = curve.makeupDb - reduction;
            if (envelopeDb < floorDb)
                gainDb -= floorDb - envelopeDb;
            const float gain = dbToGain(std::max(gainDb, -90.0f));
            blockReduction_ = std::max(blockReduction_, reduction);

            level_ += levelStep;
            blend_ += blendStep;
            const float wetL = rollOff_[0].process(ota(l * gain));
            const float wetR = rollOff_[1].process(ota(r * gain));
            left[i] = (l + (wetL - l) * blend_) * level_;
            right[i] = (r + (wetR - r) * blend_) * level_;
        }
        level_ = levelTarget;
        blend_ = blendTarget;
        blockExtra_ = peakEnvelope;
        for (auto& f : detector_)
            flush(f);
        for (auto& f : rollOff_)
            flush(f);
    }

    // The OTA's soft saturation: clean at a played level, rounding the loudest peaks.
    static float ota(float x) noexcept
    {
        constexpr float k = 1.4f;
        return std::tanh(k * x) / k;
    }

    static void flush(Biquad& f) noexcept
    {
        if (std::abs(f.z1) < 1.0e-20f)
            f.z1 = 0.0f;
        if (std::abs(f.z2) < 1.0e-20f)
            f.z2 = 0.0f;
    }

    Biquad detector_[2], rollOff_[2];
    float fast_ = 0.0f, slow_ = 0.0f;
    float fastRelease_ = 0.0f, slowAttack_ = 0.0f, slowRelease_ = 0.0f;
    float level_ = 1.0f, blend_ = 1.0f;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("gravity::create");
    return std::make_unique<Gravity>();
}

} // namespace northlight::plugins::gravity
