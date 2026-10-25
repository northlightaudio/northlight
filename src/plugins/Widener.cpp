#include "plugins/Widener.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/ParamHelpers.h"
#include "plugins/dsp/Denormals.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::widener
{

// The guard narrows to this share of the set side at most: below it the image
// is a polarity problem, which the correlation meter shows in its unsafe zone,
// and narrowing further would only make the sound vanish.
constexpr float minGuardGain = 0.25f;

float guardGainFor(float midEnergy, float sideEnergy, float threshold) noexcept
{
    if (!(sideEnergy > 1.0e-12f))
        return 1.0f;
    const float c = std::clamp(threshold, -0.95f, 0.95f);
    const float squared = std::max(0.0f, midEnergy) * (1.0f - c) / (sideEnergy * (1.0f + c));
    return std::clamp(std::sqrt(squared), minGuardGain, 1.0f);
}

namespace
{
// A biquad section in double precision (transposed direct form II): the
// crossovers reach down to 10 Hz, where float coefficients lose the response.
struct Section
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0; }
    void flush() noexcept
    {
        z1 = dsp::flush(z1, 1.0e-25);
        z2 = dsp::flush(z2, 1.0e-25);
    }
    // Takes the coefficients, keeps the memory.
    void take(const Section& design) noexcept
    {
        b0 = design.b0;
        b1 = design.b1;
        b2 = design.b2;
        a1 = design.a1;
        a2 = design.a2;
    }
};

enum class Shape
{
    lowPass,
    highPass,
    allPass
};

// The RBJ designs. Two Butterworth (Q = 1/sqrt 2) low-passes make an LR4
// low-pass, two high-passes the LR4 high-pass; the allpass at the same Q is
// their sum, in phase with both.
Section design(Shape shape, double sampleRate, double hz, double q = std::numbers::sqrt2 / 2.0) noexcept
{
    const double w0 = 2.0 * std::numbers::pi * std::clamp(hz, 5.0, 0.45 * sampleRate) / sampleRate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Section s;
    switch (shape)
    {
        case Shape::lowPass:
            s.b0 = s.b2 = (1.0 - c) * 0.5 / a0;
            s.b1 = (1.0 - c) / a0;
            break;
        case Shape::highPass:
            s.b0 = s.b2 = (1.0 + c) * 0.5 / a0;
            s.b1 = -(1.0 + c) / a0;
            break;
        case Shape::allPass:
            s.b0 = (1.0 - alpha) / a0;
            s.b1 = -2.0 * c / a0;
            s.b2 = 1.0;
            break;
    }
    s.a1 = -2.0 * c / a0;
    s.a2 = (1.0 - alpha) / a0;
    return s;
}

std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[lowWidth] = {"Low width", "%", 0.0f, maxWidthPercent, 100.0f, false};
    p[midWidth] = {"Mid width", "%", 0.0f, maxWidthPercent, 100.0f, false};
    p[highWidth] = {"High width", "%", 0.0f, maxWidthPercent, 100.0f, false};
    p[lowSplit] = {"Low split", "Hz", minLowSplitHz, maxLowSplitHz, 250.0f, true};
    p[highSplit] = {"High split", "Hz", minHighSplitHz, maxHighSplitHz, 4000.0f, true};
    p[monoBelow] = {"Mono below", "Hz", monoOffHz, maxMonoBelowHz, monoOffHz, true};
    p[spread] = {"Spread", "%", 0.0f, 100.0f, 0.0f, false};
    p[spreadMode] = choice("Spread mode", {"Diffuse", "Haas"}, 0.0f);
    p[haasTime] = {"Haas time", "ms", minHaasMs, maxHaasMs, 12.0f, true};
    p[rotation] = {"Rotation", "\xc2\xb0", -45.0f, 45.0f, 0.0f, false};
    p[balance] = {"Balance", "%", -100.0f, 100.0f, 0.0f, false};
    p[guard] = {"Guard", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}};
    p[guardAt] = {"Guard at", "", minGuardAt, maxGuardAt, 0.0f, false};
    p[output] = {"Output", "dB", -24.0f, 12.0f, 0.0f, false};
    return p;
}

// A one-pole step toward `target` that lands on it, so a control returned to
// its value reads exactly that value again.
void glide(float& value, float target, float step) noexcept
{
    value += (target - value) * step;
    if (std::abs(target - value) < 1.0e-5f)
        value = target;
}

// A one-pole average of a correlation: the products and the two energies.
struct Correlator
{
    float lr = 0.0f, ll = 0.0f, rr = 0.0f;
    float recent = 0.0f; // the power over the last 50 ms or so: is anything playing

    void add(float l, float r, float k, float fast) noexcept
    {
        lr += (l * r - lr) * k;
        ll += (l * l - ll) * k;
        rr += (r * r - rr) * k;
        recent += (l * l + r * r - recent) * fast;
    }
    bool signal() const noexcept { return recent > 1.0e-9f; } // above about -90 dBFS
    float value() const noexcept
    {
        const float denominator = std::sqrt(ll * rr);
        // One side silent (a hard-panned source): uncorrelated.
        return denominator > 1.0e-12f ? std::clamp(lr / denominator, -1.0f, 1.0f) : 0.0f;
    }
    void flush() noexcept
    {
        if (ll + rr < 1.0e-20f)
            lr = ll = rr = 0.0f;
        if (recent < 1.0e-20f)
            recent = 0.0f;
    }
};

class Widener final : public FeedbackBuiltIn
{
public:
    Widener() : FeedbackBuiltIn(type, parameterInfos(), true) {}

private:
    static constexpr int subBlock = 32; // the crossovers move this often while smoothing

    void prepareBuiltIn(double sampleRate, int) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Widener::prepareBuiltIn");
        const auto rate = static_cast<float>(sampleRate);
        haasLine_.assign(static_cast<size_t>(std::ceil(maxHaasMs * 0.001f * rate)) + 4, 0.0f);
        haasWrite_ = 0;
        for (size_t i = 0; i < decorrelator_.size(); ++i)
        {
            decorrelator_[i] = design(Shape::allPass, sampleRate, decorrelatorHz[i], decorrelatorQ);
            decorrelator_[i].reset();
        }
        spreadHighPass_ = design(Shape::highPass, sampleRate, spreadHighPassHz);
        spreadHighPass_.reset();
        for (auto* s : sections())
            s->reset();
        // Everything starts where its parameter is: no glide on load.
        monoLog_ = std::log2(monoTargetHz());
        lowLog_ = std::log2(lowTargetHz());
        highLog_ = std::log2(highTargetHz());
        designCrossovers();
        widths_ = {
            sideGain(parameter(lowWidth)), sideGain(parameter(midWidth)), sideGain(parameter(highWidth))};
        spread_ = parameter(spread) * 0.01f * spreadMaxGain;
        haasMix_ = std::lround(parameter(spreadMode)) == haas ? 1.0f : 0.0f;
        haasDelay_ = parameter(haasTime) * 0.001f * rate;
        rotation_ = parameter(rotation);
        balance_ = parameter(balance) * 0.01f;
        output_ = dbToGain(parameter(output));
        guardGain_ = 1.0f;
        compensation_ = compensationTarget_ = 1.0f;
        midIn_ = sideIn_ = sideOut_ = 0.0f;
        guardMid_ = guardSide_ = 0.0f;
        inputCorrelation_ = outputCorrelation_ = {};
        const auto step = [&](float seconds) { return dsp::stepForSeconds(seconds, rate); };
        smooth_ = step(0.02f);
        slowSmooth_ = step(0.06f);
        correlationStep_ = step(correlationSeconds);
        guardEnergyStep_ = step(guardEnergySeconds);
        guardAttack_ = step(guardAttackSeconds);
        guardRelease_ = step(guardReleaseSeconds);
        compensationStep_ = step(compensationSeconds);
        // The crossovers glide over about 30 ms, stepped once a sub-block.
        crossoverStep_ = dsp::blockStepForSeconds(0.03f, rate, static_cast<float>(subBlock));
    }

    float monoTargetHz() const noexcept
    {
        const float hz = parameter(monoBelow);
        return hz <= monoOffHz + 0.5f ? parkedMonoHz : hz;
    }
    float lowTargetHz() const noexcept
    {
        return std::clamp(parameter(lowSplit), minLowSplitHz, maxLowSplitHz);
    }
    float highTargetHz() const noexcept
    {
        return std::clamp(parameter(highSplit), std::max(minHighSplitHz, lowTargetHz()), maxHighSplitHz);
    }

    // A fixed array, so it never allocates wherever it is called from.
    std::array<Section*, 14> sections() noexcept
    {
        return {&sideMonoA_,
                &sideMonoB_,
                &lowA_,
                &lowB_,
                &lowAllPass_,
                &restA_,
                &restB_,
                &midA_,
                &midB_,
                &highA_,
                &highB_,
                &midMono_,
                &midLow_,
                &midHigh_};
    }

    void designCrossovers() noexcept
    {
        const double rate = sampleRate_;
        const double mono = std::exp2(static_cast<double>(monoLog_));
        const double low = std::exp2(static_cast<double>(lowLog_));
        const double high = std::exp2(static_cast<double>(highLog_));
        const auto monoHp = design(Shape::highPass, rate, mono);
        sideMonoA_.take(monoHp);
        sideMonoB_.take(monoHp);
        midMono_.take(design(Shape::allPass, rate, mono));
        const auto lowLp = design(Shape::lowPass, rate, low);
        const auto lowHp = design(Shape::highPass, rate, low);
        lowA_.take(lowLp);
        lowB_.take(lowLp);
        restA_.take(lowHp);
        restB_.take(lowHp);
        midLow_.take(design(Shape::allPass, rate, low));
        const auto highLp = design(Shape::lowPass, rate, high);
        const auto highHp = design(Shape::highPass, rate, high);
        midA_.take(highLp);
        midB_.take(highLp);
        highA_.take(highHp);
        highB_.take(highHp);
        const auto highAp = design(Shape::allPass, rate, high);
        lowAllPass_.take(highAp);
        midHigh_.take(highAp);
    }

    // Moves the crossovers toward their parameters; true when any moved.
    bool glideCrossovers() noexcept
    {
        bool moved = false;
        const auto glide = [&](float& current, float targetHz)
        {
            const float target = std::log2(targetHz);
            if (std::abs(target - current) < 1.0e-4f)
            {
                moved = moved || current != target;
                current = target;
                return;
            }
            current += (target - current) * crossoverStep_;
            moved = true;
        };
        glide(monoLog_, monoTargetHz());
        glide(lowLog_, lowTargetHz());
        glide(highLog_, highTargetHz());
        return moved;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const auto rate = static_cast<float>(sampleRate_);
        const std::array<float, 3> widthTargets{
            sideGain(parameter(lowWidth)), sideGain(parameter(midWidth)), sideGain(parameter(highWidth))};
        const float spreadTarget = parameter(spread) * 0.01f * spreadMaxGain;
        const float haasTarget = std::lround(parameter(spreadMode)) == haas ? 1.0f : 0.0f;
        const float delayTarget = std::clamp(parameter(haasTime), minHaasMs, maxHaasMs) * 0.001f * rate;
        const float rotationTarget = parameter(rotation);
        const float balanceTarget = parameter(balance) * 0.01f;
        const float outputTarget = dbToGain(parameter(output));
        const bool guarding = parameter(guard) >= 0.5f;
        const float guardThreshold = parameter(guardAt);
        // Untouched width: no compensation at all (the allpassed side's energy
        // only differs from the input's by the averaging).
        const bool neutral = widthTargets[0] == 1.0f && widthTargets[1] == 1.0f && widthTargets[2] == 1.0f &&
                             spreadTarget == 0.0f && widths_[0] == 1.0f && widths_[1] == 1.0f &&
                             widths_[2] == 1.0f && spread_ == 0.0f;
        const auto lineSize = static_cast<int>(haasLine_.size());

        for (int start = 0; start < numSamples; start += subBlock)
        {
            if (glideCrossovers())
                designCrossovers();
            const int end = std::min(numSamples, start + subBlock);
            for (int i = start; i < end; ++i)
            {
                // The smoothed controls: widths, spread and levels per sample.
                for (size_t b = 0; b < 3; ++b)
                    glide(widths_[b], widthTargets[b], smooth_);
                glide(spread_, spreadTarget, smooth_);
                glide(haasMix_, haasTarget, smooth_);
                glide(haasDelay_, delayTarget, slowSmooth_);
                glide(balance_, balanceTarget, smooth_);
                glide(output_, outputTarget, smooth_);
                glide(rotation_, rotationTarget, smooth_);
                if (rotation_ != shownRotation_)
                {
                    shownRotation_ = rotation_;
                    const float radians = rotation_ * std::numbers::pi_v<float> / 180.0f;
                    cos_ = std::cos(radians);
                    sin_ = std::sin(radians);
                }

                const float inL = left[i], inR = right[i];
                const float mid = 0.5f * (inL + inR);
                const float side = 0.5f * (inL - inR);

                // The spread signal from the mid: the decorrelated mid, or the
                // mid a few milliseconds late, crossfaded by the mode.
                haasLine_[static_cast<size_t>(haasWrite_)] = mid;
                float readAt = static_cast<float>(haasWrite_) - haasDelay_;
                if (readAt < 0.0f)
                    readAt += static_cast<float>(lineSize);
                const int i0 = static_cast<int>(readAt);
                const float frac = readAt - static_cast<float>(i0);
                const int i1 = i0 + 1 < lineSize ? i0 + 1 : 0;
                const float delayed = haasLine_[static_cast<size_t>(i0)] * (1.0f - frac) +
                                      haasLine_[static_cast<size_t>(i1)] * frac;
                if (++haasWrite_ >= lineSize)
                    haasWrite_ = 0;
                double diffused = mid;
                for (auto& s : decorrelator_)
                    diffused = s.process(diffused);
                const double spreadSignal = spreadHighPass_.process(diffused * (1.0 - haasMix_) +
                                                                    static_cast<double>(delayed) * haasMix_);

                // The side: spread in, mono below out, then the three bands.
                double s = side + static_cast<double>(spread_) * spreadSignal;
                s = sideMonoB_.process(sideMonoA_.process(s));
                const double low = lowAllPass_.process(lowB_.process(lowA_.process(s)));
                const double rest = restB_.process(restA_.process(s));
                const double midBand = midB_.process(midA_.process(rest));
                const double high = highB_.process(highA_.process(rest));
                const auto widened =
                    static_cast<float>(low * widths_[0] + midBand * widths_[1] + high * widths_[2]);
                // The mid through the same phase as every side band.
                const auto m = static_cast<float>(midHigh_.process(midLow_.process(midMono_.process(mid))));

                // The guard: the largest side gain that keeps the correlation at
                // its threshold, quick to narrow and slow to open again.
                guardMid_ += (m * m - guardMid_) * guardEnergyStep_;
                guardSide_ += (widened * widened - guardSide_) * guardEnergyStep_;
                const float guardTarget =
                    guarding ? guardGainFor(guardMid_, guardSide_, guardThreshold) : 1.0f;
                guardGain_ +=
                    (guardTarget - guardGain_) * (guardTarget < guardGain_ ? guardAttack_ : guardRelease_);
                float sw = widened * guardGain_;

                // Level compensation: the power going in over the power now.
                midIn_ += (mid * mid - midIn_) * compensationStep_;
                sideIn_ += (side * side - sideIn_) * compensationStep_;
                sideOut_ += (widened * widened - sideOut_) * compensationStep_;
                if (neutral)
                    compensationTarget_ = 1.0f;
                else
                {
                    const float before = midIn_ + sideIn_;
                    const float after = midIn_ + guardGain_ * guardGain_ * sideOut_;
                    if (before > 1.0e-10f && after > 1.0e-10f)
                        compensationTarget_ =
                            std::clamp(std::sqrt(before / after), minCompensation_, maxCompensation_);
                }
                compensation_ += (compensationTarget_ - compensation_) * compensationStep_;
                float mm = m * compensation_;
                sw *= compensation_;

                // Rotation (positive turns the image to the right), then balance.
                const float rm = mm * cos_ + sw * sin_;
                const float rs = sw * cos_ - mm * sin_;
                const float gainL = balance_ > 0.0f ? 1.0f - balance_ : 1.0f;
                const float gainR = balance_ < 0.0f ? 1.0f + balance_ : 1.0f;
                left[i] = (rm + rs) * gainL * output_;
                right[i] = (rm - rs) * gainR * output_;
                measure(inL, inR, left[i], right[i], guardGain_);
            }
        }
        tapOutput(left, right, numSamples);
        flush();
    }

    // The correlation in and out, and the frame for the card.
    void measure(float inL, float inR, float outL, float outR, float guardGain) noexcept
    {
        inputCorrelation_.add(inL, inR, correlationStep_, guardEnergyStep_);
        outputCorrelation_.add(outL, outR, correlationStep_, guardEnergyStep_);
        feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                            std::max(std::abs(outL), std::abs(outR)),
                            100.0f * (1.0f - guardGain),
                            encodeCorrelation(outputCorrelation_.value(), outputCorrelation_.signal()),
                            encodeCorrelation(inputCorrelation_.value(), inputCorrelation_.signal()));
    }

    void tapOutput(const float* left, const float* right, int numSamples) noexcept
    {
        // Each tap carries one channel: the mono sum of a channel with itself.
        feedback_.tap(0, left, left, numSamples);
        feedback_.tap(1, right, right, numSamples);
    }

    void flush() noexcept
    {
        for (auto* s : {&sideMonoA_,
                        &sideMonoB_,
                        &lowA_,
                        &lowB_,
                        &lowAllPass_,
                        &restA_,
                        &restB_,
                        &midA_,
                        &midB_,
                        &highA_,
                        &highB_,
                        &midMono_,
                        &midLow_,
                        &midHigh_})
            s->flush();
        for (auto& s : decorrelator_)
            s.flush();
        spreadHighPass_.flush();
        inputCorrelation_.flush();
        outputCorrelation_.flush();
        for (auto* e : {&midIn_, &sideIn_, &sideOut_, &guardMid_, &guardSide_})
            if (*e < 1.0e-20f)
                *e = 0.0f;
    }

    // Settled bypass: the input passes; while an editor watches, it still shows
    // the image and the correlation going through.
    bool bypassedBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        if (!feedback_.watched())
            return true; // nothing to measure for
        for (int i = 0; i < numSamples; ++i)
            measure(left[i], right[i], left[i], right[i], 1.0f);
        tapOutput(left, right, numSamples);
        inputCorrelation_.flush();
        outputCorrelation_.flush();
        return true;
    }

    // The side's mono-below high-pass, the three-band split and the mid's allpasses.
    Section sideMonoA_, sideMonoB_, lowA_, lowB_, lowAllPass_, restA_, restB_, midA_, midB_, highA_, highB_;
    Section midMono_, midLow_, midHigh_;
    std::array<Section, decorrelatorHz.size()> decorrelator_;
    Section spreadHighPass_;
    std::vector<float> haasLine_;
    int haasWrite_ = 0;
    float monoLog_ = 0.0f, lowLog_ = 0.0f, highLog_ = 0.0f; // log2 Hz
    std::array<float, 3> widths_{1.0f, 1.0f, 1.0f};
    float spread_ = 0.0f, haasMix_ = 0.0f, haasDelay_ = 0.0f;
    float rotation_ = 0.0f, shownRotation_ = 0.0f, cos_ = 1.0f, sin_ = 0.0f;
    float balance_ = 0.0f, output_ = 1.0f;
    float guardGain_ = 1.0f, guardMid_ = 0.0f, guardSide_ = 0.0f;
    float compensation_ = 1.0f, compensationTarget_ = 1.0f;
    float midIn_ = 0.0f, sideIn_ = 0.0f, sideOut_ = 0.0f;
    Correlator inputCorrelation_, outputCorrelation_;
    float smooth_ = 0.0f, slowSmooth_ = 0.0f, correlationStep_ = 0.0f, guardEnergyStep_ = 0.0f;
    float guardAttack_ = 0.0f, guardRelease_ = 0.0f, compensationStep_ = 0.0f, crossoverStep_ = 0.0f;
    const float minCompensation_ = dbToGain(minCompensationDb);
    const float maxCompensation_ = dbToGain(maxCompensationDb);
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Widener>();
}

} // namespace northlight::plugins::widener
