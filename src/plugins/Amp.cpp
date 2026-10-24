#include "plugins/Amp.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/SnapshotPublisher.h"
#include "plugins/BackgroundJobs.h"
#include "plugins/Biquad.h"
#include "plugins/Cabinet.h"
#include "plugins/Convolver.h"
#include "plugins/Oversampler.h"
#include "plugins/ProcessorBase.h"
#include "plugins/ToneStack.h"
#include "plugins/Triode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::amp
{

namespace
{
// ----- the circuit's values ----------------------------------------------------------------

// The three preamp stages. V1 and V2 are 12AX7s with a 100k plate load and a
// bypassed 1.5k cathode; V1 is driven by the guitar through its 68k grid
// stopper, V2 by the volume pot. V3 is the mixer stage after the reverb's
// return: unbypassed, so less gain and a softer edge.
TriodeCircuit firstStage()
{
    return {};
}

TriodeCircuit secondStage()
{
    TriodeCircuit c;
    c.sourceResistance = 100.0e3;
    return c;
}

TriodeCircuit mixerStage()
{
    TriodeCircuit c;
    c.bypassed = false;
    c.sourceResistance = 220.0e3;
    return c;
}

// Solved once for the whole program (thousands of bisections each).
struct Curves
{
    TriodeCurve v1 = solveTriode(firstStage());
    TriodeCurve v2 = solveTriode(secondStage());
    TriodeCurve v3 = solveTriode(mixerStage());
};

const Curves& curves()
{
    static const Curves c;
    return c;
}

// The volume pot (1M audio) with the bright switch's 120 pF across its top.
constexpr double volumePot = 1.0e6;
constexpr double brightCap = 120.0e-12;
// The channels' mixing resistors in front of V3 lose this much of V2's swing.
constexpr double mixLoss = 0.043;
// The pull boost: more of V2 in front of the second stage.
constexpr float boostDb = 10.0f;
// The phase inverter and the power amp, on a scale where 1 is where the power
// tubes begin to clip: V3's output (volts) over this reaches 1.
constexpr double powerVolts = 25.0;
// The phase inverter clips later than the power tubes, and softer.
constexpr double inverterHeadroom = 1.6;
// Negative feedback from the output transformer's secondary.
constexpr double feedbackAmount = 0.5;
// The amp's output (1 = full power) to the plug-in's output.
constexpr float outputScale = 0.5f;
// The reverb: V2's output into the driver, the tank's return back out.
constexpr double driverVolts = 40.0;
constexpr double reverbReturn = 1.4;

// The first-order section of the volume pot and bright cap, by the bilinear
// transform, in double.
struct FirstOrder
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, z = 0.0;

    // H(s) = (n0 + n1 s) / (d0 + d1 s)
    void design(double n0, double n1, double d0, double d1, double sampleRate) noexcept
    {
        const double k = 2.0 * sampleRate;
        const double norm = 1.0 / (d0 + d1 * k);
        b0 = (n0 + n1 * k) * norm;
        b1 = (n0 - n1 * k) * norm;
        a1 = (d0 - d1 * k) * norm;
    }
    double process(double x) noexcept
    {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
};

void volumeFilter(FirstOrder& f, double share, bool brightOn, double sampleRate) noexcept
{
    const double lower = std::max(1.0e-5, share) * volumePot;
    const double upper = (1.0 - std::min(1.0, share)) * volumePot;
    if (!brightOn || upper <= 1.0)
        f.design(lower, 0.0, lower + upper, 0.0, sampleRate);
    else
        f.design(lower, brightCap * upper * lower, lower + upper, brightCap * upper * lower, sampleRate);
}

// tanh with first-order antiderivative anti-aliasing (log cosh is its integral).
struct SoftClip
{
    double previous = 0.0, previousIntegral = 0.0;

    static double logCosh(double x) noexcept
    {
        const double a = std::abs(x);
        return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
    }
    void reset() noexcept { previous = previousIntegral = 0.0; }
    double process(double x) noexcept
    {
        const double integral = logCosh(x);
        const double dx = x - previous;
        const double y =
            std::abs(dx) > 1.0e-6 ? (integral - previousIntegral) / dx : std::tanh(0.5 * (x + previous));
        previous = x;
        previousIntegral = integral;
        return y;
    }
};

// The reverb tank: two springs of different lengths, each a stretched all-pass
// cascade (the chirp: highs arrive after lows) inside a delay loop that sends
// it round again, darkened, until it dies away over about three seconds.
// Band-limited in and out as the driver and recovery transformers are.
class Spring
{
public:
    void prepare(double sampleRate, double loopSeconds, double transitionHz)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("amp Spring::prepare");
        k_ = std::max(1, static_cast<int>(std::lround(sampleRate / (2.0 * transitionHz))));
        memory_.assign(static_cast<size_t>(stages * k_), 0.0f);
        const double cascadeSamples = stages * k_ * (1.0 - coefficient) / (1.0 + coefficient);
        length_ = std::max(8, static_cast<int>(sampleRate * loopSeconds - cascadeSamples));
        loop_.assign(static_cast<size_t>(length_), 0.0f);
        position_ = write_ = 0;
        low_ = 0.0f;
        lowStep_ =
            1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3800.0f / static_cast<float>(sampleRate));
        gain_ = static_cast<float>(std::pow(10.0, -3.0 * loopSeconds / decaySeconds));
    }

    void reset() noexcept
    {
        std::fill(memory_.begin(), memory_.end(), 0.0f);
        std::fill(loop_.begin(), loop_.end(), 0.0f);
        low_ = 0.0f;
    }

    float process(float x) noexcept
    {
        const float back = loop_[static_cast<size_t>(write_)];
        low_ += lowStep_ * (back - low_);
        float u = x + gain_ * low_;
        auto* m = memory_.data() + position_;
        for (int s = 0; s < stages; ++s, m += k_)
        {
            const float old = *m;
            const float w = u - coefficient * old;
            u = coefficient * w + old;
            *m = w;
        }
        if (++position_ == k_)
            position_ = 0;
        loop_[static_cast<size_t>(write_)] = u;
        if (++write_ == length_)
            write_ = 0;
        if (std::abs(low_) < 1.0e-15f)
            low_ = 0.0f;
        return u;
    }

private:
    static constexpr int stages = 60;
    static constexpr float coefficient = 0.65f;
    static constexpr double decaySeconds = 2.8;
    std::vector<float> memory_, loop_;
    int k_ = 1, position_ = 0, length_ = 8, write_ = 0;
    float low_ = 0.0f, lowStep_ = 0.5f, gain_ = 0.9f;
};

class Tank
{
public:
    void prepare(double sampleRate)
    {
        springs_[0].prepare(sampleRate, 0.0331, 4500.0);
        springs_[1].prepare(sampleRate, 0.0413, 4200.0);
        in_.setHighPass(sampleRate, 250.0, 0.7);
        outHigh_.setHighPass(sampleRate, 150.0, 0.7);
        outLow_.setLowPass(sampleRate, 4500.0, 0.7);
        reset();
    }
    void reset() noexcept
    {
        for (auto& s : springs_)
            s.reset();
        for (auto* b : {&in_, &outHigh_, &outLow_})
            b->reset();
        driver_.reset();
    }
    // `drive`: the driver tube's input (1 = where it starts to clip).
    float process(float drive) noexcept
    {
        const auto x = static_cast<float>(driver_.process(drive));
        const float u = in_.process(x);
        const float y = 0.5f * (springs_[0].process(u) + springs_[1].process(u));
        return outLow_.process(outHigh_.process(y));
    }

private:
    std::array<Spring, 2> springs_;
    Biquad in_, outHigh_, outLow_;
    SoftClip driver_;
};

// ----- parameters ------------------------------------------------------------------------------

engine::ParameterInfo choice(std::string name, std::vector<std::string> choices, float defaultValue)
{
    engine::ParameterInfo info;
    info.name = std::move(name);
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = defaultValue;
    info.choices = std::move(choices);
    return info;
}

engine::ParameterInfo knob(std::string name, float defaultValue)
{
    return {std::move(name), "", 1.0f, 10.0f, defaultValue, false};
}

std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<std::string> models, types;
    for (int i = 0; i < cabinet::modelCount; ++i)
        models.emplace_back(cabinet::modelName(i));
    for (int i = 0; i < cabinet::micTypeCount; ++i)
        types.emplace_back(cabinet::micTypeName(i));
    std::vector<engine::ParameterInfo> p{choice("Bright", {"Off", "On"}, 1.0f),
                                         knob("Volume", 4.0f),
                                         knob("Treble", 6.0f),
                                         knob("Middle", 5.0f),
                                         knob("Bass", 4.0f),
                                         knob("Reverb", 3.0f),
                                         knob("Speed", 5.0f),
                                         knob("Intensity", 6.0f),
                                         choice("Reverb On", {"Off", "On"}, 1.0f),
                                         choice("Tremolo On", {"Off", "On"}, 0.0f),
                                         knob("Master", 10.0f),
                                         choice("Boost", {"Off", "On"}, 0.0f),
                                         {"Input", "dB", -24.0f, 24.0f, 0.0f, false},
                                         {"Gate", "dB", gateOffDb, -20.0f, gateOffDb, false},
                                         {"Sag", "%", 0.0f, 100.0f, 30.0f, false},
                                         choice("Quality", {"2x", "4x", "8x"}, 1.0f),
                                         {"Output", "dB", -24.0f, 12.0f, 0.0f, false},
                                         choice("Cabinet On", {"Off", "On"}, 1.0f),
                                         choice("Cabinet", models, static_cast<float>(cabinet::open2x12))};
    const cabinet::Mic a; // the defaults: a dynamic just off the cap, up close
    for (int mic = 0; mic < 2; ++mic)
    {
        const std::string n = mic == 0 ? "Mic A " : "Mic B ";
        const bool first = mic == 0;
        p.push_back(choice(n + "On", {"Off", "On"}, first ? 1.0f : 0.0f));
        p.push_back(
            choice(n + "Type", types, static_cast<float>(first ? cabinet::dynamic : cabinet::ribbon)));
        p.push_back({n + "Position", "%", 0.0f, 100.0f, first ? a.position * 100.0f : 50.0f, false});
        p.push_back({n + "Distance", "cm", 1.0f, 100.0f, first ? a.distanceCm : 15.0f, true});
        p.push_back({n + "Angle", "deg", 0.0f, 45.0f, 0.0f, false});
        p.push_back({n + "Level", "dB", -24.0f, 6.0f, first ? 0.0f : -3.0f, false});
        p.push_back({n + "Pan", "%", -100.0f, 100.0f, 0.0f, false});
        p.push_back(choice(n + "Polarity", {"Normal", "Inverted"}, 0.0f));
    }
    p.push_back({"Room", "%", 0.0f, 100.0f, 10.0f, false});
    return p;
}

// ----- the processor ------------------------------------------------------------------------

class Amp final : public ProcessorBase, public background::Job
{
public:
    explicit Amp(sampler::Loader loader) : ProcessorBase(type, parameterInfos()), loader_(std::move(loader))
    {
    }

    ~Amp() override
    {
        if (registered_)
            background::remove(this);
    }

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

    std::string state() const override
    {
        const std::scoped_lock lock(buildMutex_);
        return state_;
    }

    void setState(const std::string& state) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Amp::setState");
        std::shared_ptr<const cabinet::UserResponse> user;
        std::string error;
        if (const auto path = sampler::decodeState(state); !path.empty())
        {
            std::shared_ptr<const sampler::Sample> sample;
            if (loader_)
                sample = loader_(path, error);
            else
                error = "no file loader";
            if (sample != nullptr)
            {
                auto r = std::make_shared<cabinet::UserResponse>();
                r->sampleRate = sample->sampleRate;
                r->left = sample->left;
                r->right = sample->right;
                user = std::move(r);
            }
            else if (error.empty())
                error = "could not read the file";
        }
        const std::scoped_lock lock(buildMutex_);
        state_ = state;
        error_ = error;
        user_ = std::move(user);
        ++userGeneration_;
        if (prepared_)
            rebuild();
    }

    // ----- the cabinet, off the real-time thread ---------------------------------------------

    void poll() override
    {
        const std::unique_lock lock(buildMutex_, std::try_to_lock);
        if (lock.owns_lock())
            rebuild();
    }

    void settle()
    {
        const std::scoped_lock lock(buildMutex_);
        rebuild();
    }

    std::string path() const
    {
        const std::scoped_lock lock(buildMutex_);
        return sampler::decodeState(state_);
    }
    std::string error() const
    {
        const std::scoped_lock lock(buildMutex_);
        return error_;
    }

private:
    struct KernelSlot
    {
        std::uint64_t epoch = 0;
        std::shared_ptr<const ConvolverKernel> kernel, previous;
    };

    cabinet::Settings cabinetSettings() const noexcept
    {
        cabinet::Settings s;
        s.model = std::clamp(static_cast<int>(std::lround(parameter(cabinet))), 0, cabinet::modelCount - 1);
        for (int m = 0; m < 2; ++m)
        {
            auto& mic = s.mics[static_cast<size_t>(m)];
            mic.on = parameter(micParam(m, micOn)) >= 0.5f;
            mic.type = std::clamp(static_cast<int>(std::lround(parameter(micParam(m, micType)))), 0, 2);
            mic.position = parameter(micParam(m, micPosition)) * 0.01f;
            mic.distanceCm = parameter(micParam(m, micDistance));
            mic.angleDeg = parameter(micParam(m, micAngle));
            mic.levelDb = parameter(micParam(m, micLevel));
            mic.pan = parameter(micParam(m, micPan)) * 0.01f;
            mic.invert = parameter(micParam(m, micPolarity)) >= 0.5f;
        }
        s.room = parameter(room) * 0.01f;
        return s;
    }

    // With buildMutex_ held.
    void rebuild()
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Amp::rebuild");
        const auto settings = cabinetSettings();
        if (built_ && settings == builtSettings_ && builtRate_ == sampleRate_ &&
            builtUser_ == userGeneration_)
            return;
        const auto response = cabinet::build(settings, sampleRate_, user_.get());
        std::shared_ptr<const ConvolverKernel> kernel = makeConvolverKernel(
            response.left.data(), response.right.data(), static_cast<int>(response.left.size()), blockSize);
        auto slot = std::make_unique<KernelSlot>();
        slot->kernel = kernel;
        slot->previous = builtRate_ == sampleRate_ ? current_ : nullptr;
        kernels_.publish(std::move(slot));
        kernels_.collect(true);
        current_ = std::move(kernel);
        builtSettings_ = settings;
        builtRate_ = sampleRate_;
        builtUser_ = userGeneration_;
        built_ = true;
    }

    // ----- preparing ---------------------------------------------------------------------------

    int factorFor(float choiceValue) const noexcept
    {
        const int index = std::clamp(static_cast<int>(std::lround(choiceValue)), 0, 2);
        int factor = qualityFactors[index];
        // Past about 400 kHz the oversampling buys nothing.
        while (factor > 1 && sampleRate_ * factor > 400000.0)
            factor /= 2;
        return factor;
    }

    void prepareAudio(double sampleRate, int maxBlockSize) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Amp::prepareAudio");
        const auto& c = curves();
        feedback_.prepare(sampleRate);
        const auto size = static_cast<size_t>(std::max(1, maxBlockSize));
        for (auto* v : {&mono_, &wetLeft_, &wetRight_, &gains_, &levels_, &drives_, &lamps_, &reductions_})
            v->assign(size, 0.0f);
        tank_.prepare(sampleRate);
        const int maxPartitions =
            static_cast<int>(std::ceil(cabinet::maxUserSeconds * sampleRate / blockSize)) + 2;
        convolver_.prepare(blockSize,
                           std::max(maxPartitions, cabinet::responseLength(sampleRate) / blockSize + 1));
        convolver_.setCrossfadeLength(static_cast<int>(0.012 * sampleRate));
        speakerBump_.setPeaking(sampleRate, 95.0, 1.4, 2.5);
        speakerRise_.setHighShelf(sampleRate, 3000.0, 2.0);
        speakerBump_.reset();
        speakerRise_.reset();
        stage1_.prepare(c.v1, sampleRate, 10.0, 20000.0);
        stage2_.prepare(c.v2, sampleRate, 10.0, 20000.0);
        stage3_.prepare(c.v3, sampleRate, 12.0, 20000.0);
        factor_ = factorFor(parameter(quality));
        configure();

        const auto step = [&](double seconds)
        { return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate))); };
        smoothStep_ = step(0.02);
        gateAttack_ = step(0.0005);
        gateRelease_ = step(0.02);
        gateOpen_ = step(0.001);
        gateClose_ = step(0.08);
        gateHold_ = static_cast<int>(0.05 * sampleRate);
        sagAttack_ = step(0.02);
        sagRelease_ = step(0.2);
        ldrAttack_ = step(0.004);
        ldrRelease_ = step(0.04);
        fadeStep_ = static_cast<float>(1.0 / (0.002 * sampleRate));
        primed_ = false;
        gateEnvelope_ = 0.0f;
        gateGain_ = 1.0f;
        gateHeld_ = 0;
        gateIsOpen_ = true;
        sagEnvelope_ = 0.0f;
        lfoPhase_ = 0.0;
        ldr_ = 0.0f;
        fade_ = 1.0f;
        designed_ = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};

        const std::scoped_lock lock(buildMutex_);
        prepared_ = true;
        rebuild();
        if (!registered_)
        {
            background::add(this);
            registered_ = true;
        }
    }

    // The coefficients and state of everything that runs at the raised rate.
    // Real-time safe (a Quality change re-runs it between fades).
    void configure() noexcept
    {
        const double raised = sampleRate_ * factor_;
        for (auto* o : {&upFront_, &downFront_, &upBack_, &downBack_})
            o->setFactor(factor_);
        stage1_.retune(raised);
        stage2_.retune(raised);
        stage3_.retune(raised);
        transformerHigh_.setHighPass1(raised, 35.0);
        transformerLow_.setLowPass1(raised, 12000.0);
        transformerHigh_.reset();
        transformerLow_.reset();
        toneStack_.reset();
        volume_.z = 0.0;
        inverter_.reset();
        power_.reset();
        feedbackSample_ = 0.0;
        designed_ = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
        configuredFactor_ = factor_;
    }

    // ----- processing --------------------------------------------------------------------------

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        feedback_.addBypassed(left, right, numSamples);
    }

    // The smoothed controls, per sample.
    enum Smooth
    {
        sInput,
        sVolume,
        sBoost,
        sMaster,
        sReverb,
        sDepth,
        sOutput,
        sCabinet,
        smoothCount
    };

    std::array<float, smoothCount> targets() const noexcept
    {
        std::array<float, smoothCount> t{};
        t[sInput] = dbToGain(parameter(input));
        t[sVolume] = static_cast<float>(tonestack::audioTaper(travel(parameter(volume))));
        t[sBoost] = parameter(boost) >= 0.5f ? dbToGain(boostDb) : 1.0f;
        t[sMaster] = static_cast<float>(tonestack::audioTaper(travel(parameter(master))));
        t[sReverb] = parameter(reverbOn) >= 0.5f
                         ? static_cast<float>(tonestack::audioTaper(travel(parameter(reverb))) * reverbReturn)
                         : 0.0f;
        t[sDepth] = parameter(tremoloOn) >= 0.5f
                        ? static_cast<float>(std::pow(travel(parameter(intensity)), 1.3))
                        : 0.0f;
        t[sOutput] = dbToGain(parameter(output)) * outputScale;
        t[sCabinet] = parameter(cabinetOn) >= 0.5f ? 1.0f : 0.0f;
        return t;
    }

    // The tone stack and the volume pot follow their knobs (smoothed per
    // control interval), redesigned only when something moved.
    void designFilters(int numSamples) noexcept
    {
        const float k =
            1.0f - std::exp(-static_cast<float>(numSamples) / (0.02f * static_cast<float>(sampleRate_)));
        const std::array<float, 5> wanted{static_cast<float>(travel(parameter(treble))),
                                          static_cast<float>(travel(parameter(middle))),
                                          static_cast<float>(travel(parameter(bass))),
                                          smoothed_[sVolume],
                                          parameter(bright) >= 0.5f ? 1.0f : 0.0f};
        if (designed_[0] < 0.0f)
            knobs_ = {wanted[0], wanted[1], wanted[2]};
        for (size_t i = 0; i < 3; ++i)
            knobs_[i] += (wanted[i] - knobs_[i]) * k;
        const std::array<float, 5> now{knobs_[0], knobs_[1], knobs_[2], wanted[3], wanted[4]};
        bool changed = false;
        for (size_t i = 0; i < now.size(); ++i)
            changed = changed || std::abs(now[i] - designed_[i]) > 1.0e-4f;
        if (!changed)
            return;
        const double raised = sampleRate_ * factor_;
        const auto h =
            tonestack::analog(stack_, tonestack::audioTaper(now[0]), now[1], tonestack::audioTaper(now[2]));
        toneStack_.design(h, raised);
        volumeFilter(volume_, now[3], now[4] >= 0.5f, raised);
        designed_ = now;
    }

    static constexpr int controlInterval = 32;

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const auto& t = targets();
        if (!primed_)
        {
            smoothed_ = t;
            primed_ = true;
        }
        // A Quality change fades out, switches and fades back in.
        const int wantedFactor = factorFor(parameter(quality));
        const float gateThreshold = parameter(gate) <= gateOffDb + 0.01f ? 0.0f : dbToGain(parameter(gate));
        const float sagAmount = std::clamp(parameter(sag) * 0.01f, 0.0f, 1.0f);
        const double lfoStep =
            2.0 * std::numbers::pi * 1.5 * std::pow(8.0 / 1.5, travel(parameter(speed))) / sampleRate_;
        const auto* slot = kernels_.acquire();
        const int factor = factor_;
        float raised[Oversampler::maxFactor];

        for (int start = 0; start < numSamples; start += controlInterval)
        {
            const int count = std::min(controlInterval, numSamples - start);
            designFilters(count);
            for (int n = start; n < start + count; ++n)
            {
                for (size_t i = 0; i < smoothed_.size(); ++i)
                    smoothed_[i] += (t[i] - smoothed_[i]) * smoothStep_;
                if (wantedFactor != configuredFactor_)
                {
                    fade_ -= fadeStep_;
                    if (fade_ <= 0.0f)
                    {
                        fade_ = 0.0f;
                        factor_ = wantedFactor;
                        configure();
                    }
                }
                else if (fade_ < 1.0f)
                    fade_ = std::min(1.0f, fade_ + fadeStep_);

                // The input, its trim and the gate. A track pans before its inserts
                // (constant power), so the sum is scaled to bring a centred mono
                // track back to its own level.
                float x = 0.5f * std::numbers::sqrt2_v<float> * (left[n] + right[n]) * smoothed_[sInput];
                const float level = std::abs(x);
                levels_[static_cast<size_t>(n)] = level;
                gateEnvelope_ +=
                    (level - gateEnvelope_) * (level > gateEnvelope_ ? gateAttack_ : gateRelease_);
                if (gateThreshold > 0.0f)
                {
                    if (gateEnvelope_ > gateThreshold)
                    {
                        gateIsOpen_ = true;
                        gateHeld_ = gateHold_;
                    }
                    else if (gateEnvelope_ < 0.5f * gateThreshold && --gateHeld_ <= 0)
                        gateIsOpen_ = false;
                }
                else
                    gateIsOpen_ = true;
                const float gateTarget = gateIsOpen_ ? 1.0f : 0.0f;
                gateGain_ += (gateTarget - gateGain_) * (gateTarget > gateGain_ ? gateOpen_ : gateClose_);
                x *= gateGain_;
                reductions_[static_cast<size_t>(n)] =
                    gateGain_ < 1.0f ? std::min(60.0f, -gainToDb(std::max(gateGain_, 1.0e-3f))) : 0.0f;

                // V1, the tone stack, the volume and bright, the boost and V2,
                // at the raised rate.
                upFront_.up(x, raised);
                const float boostGain = smoothed_[sBoost];
                for (int i = 0; i < factor; ++i)
                {
                    double v = stage1_.process(raised[i]);
                    v = toneStack_.process(v);
                    v = volume_.process(v) * boostGain;
                    raised[i] = stage2_.process(static_cast<float>(v));
                }
                const float preamp = downFront_.down(raised);

                // The reverb send and return, at the base rate, mixed in front of V3.
                const float wet = tank_.process(static_cast<float>(preamp / driverVolts));
                const auto mixed =
                    static_cast<float>(mixLoss * (preamp + driverVolts * smoothed_[sReverb] * wet));

                // The tremolo's lamp and the light cell that shunts the signal.
                lfoPhase_ += lfoStep;
                if (lfoPhase_ > 2.0 * std::numbers::pi)
                    lfoPhase_ -= 2.0 * std::numbers::pi;
                const auto lamp = static_cast<float>(std::max(0.0, std::sin(lfoPhase_)));
                ldr_ += (lamp - ldr_) * (lamp > ldr_ ? ldrAttack_ : ldrRelease_);
                const float tremolo = 1.0f - 0.95f * smoothed_[sDepth] * ldr_;

                // V3, the master, the phase inverter, the power amp and its
                // feedback, the transformer, at the raised rate.
                upBack_.up(mixed, raised);
                const double drive = tremolo * smoothed_[sMaster] / powerVolts;
                const double headroom = 1.0 / (1.0 + 0.5 * sagAmount * sagEnvelope_);
                float hottest = 0.0f;
                for (int i = 0; i < factor; ++i)
                {
                    const double v3 = -stage3_.process(raised[i]); // the inverter's phases restore it
                    const double split = inverterHeadroom * inverter_.process(v3 * drive / inverterHeadroom);
                    const double u = (1.0 + feedbackAmount) * split - feedbackAmount * feedbackSample_;
                    hottest = std::max(hottest, static_cast<float>(std::abs(u)));
                    const double y = headroom * power_.process(u / headroom);
                    const double secondary =
                        transformerLow_.process(transformerHigh_.process(static_cast<float>(y)));
                    feedbackSample_ = secondary;
                    raised[i] = static_cast<float>(secondary);
                }
                float y = downBack_.down(raised);
                drives_[static_cast<size_t>(n)] = hottest;
                lamps_[static_cast<size_t>(n)] = ldr_;
                gains_[static_cast<size_t>(n)] = smoothed_[sOutput] * fade_;
                const float load = std::min(1.5f, std::abs(y));
                sagEnvelope_ += (load - sagEnvelope_) * (load > sagEnvelope_ ? sagAttack_ : sagRelease_);

                // The speaker's load on the output.
                y = speakerRise_.process(speakerBump_.process(y));
                mono_[static_cast<size_t>(n)] = y;
            }
        }

        // The cabinet, crossfaded in and out with its switch.
        const ConvolverKernel* kernel = slot != nullptr ? slot->kernel.get() : nullptr;
        const ConvolverKernel* previous = slot != nullptr ? slot->previous.get() : nullptr;
        const bool cabinetHeard = smoothed_[sCabinet] > 1.0e-4f || t[sCabinet] > 0.0f;
        if (cabinetHeard)
            convolver_.process(kernel, previous, mono_.data(), wetLeft_.data(), wetRight_.data(), numSamples);
        float cab = cabinetMix_;
        const float cabTarget = t[sCabinet];
        for (int n = 0; n < numSamples; ++n)
        {
            const auto i = static_cast<size_t>(n);
            cab += (cabTarget - cab) * smoothStep_;
            const float dry = mono_[i];
            float l = dry, r = dry;
            if (cabinetHeard)
            {
                l = dry + (wetLeft_[i] - dry) * cab;
                r = dry + (wetRight_[i] - dry) * cab;
            }
            left[n] = l * gains_[i];
            right[n] = r * gains_[i];
            feedback_.addSample(levels_[i],
                                std::max(std::abs(left[n]), std::abs(right[n])),
                                reductions_[i],
                                drives_[i],
                                lamps_[i]);
        }
        cabinetMix_ = cab;
        feedback_.setTempoBpm(playHead().tempoBpm);
        flush();
    }

    void flush() noexcept
    {
        stage1_.flush();
        stage2_.flush();
        stage3_.flush();
        toneStack_.flush();
        for (auto* o : {&upFront_, &downFront_, &upBack_, &downBack_})
            o->flush();
        if (std::abs(volume_.z) < 1.0e-30)
            volume_.z = 0.0;
        if (std::abs(feedbackSample_) < 1.0e-30)
            feedbackSample_ = 0.0;
        for (auto* b : {&transformerHigh_, &transformerLow_, &speakerBump_, &speakerRise_})
        {
            if (std::abs(b->z1) < 1.0e-20f)
                b->z1 = 0.0f;
            if (std::abs(b->z2) < 1.0e-20f)
                b->z2 = 0.0f;
        }
        if (ldr_ < 1.0e-20f)
            ldr_ = 0.0f;
        if (sagEnvelope_ < 1.0e-20f)
            sagEnvelope_ = 0.0f;
        if (gateEnvelope_ < 1.0e-20f)
            gateEnvelope_ = 0.0f;
    }

    static constexpr int blockSize = 64; // the convolver's partitions

    sampler::Loader loader_;
    engine::ProcessorFeedback feedback_;

    // The cabinet's builder (buildMutex_ guards these).
    mutable std::mutex buildMutex_;
    std::string state_, error_;
    std::shared_ptr<const cabinet::UserResponse> user_;
    std::uint64_t userGeneration_ = 0, builtUser_ = 0;
    cabinet::Settings builtSettings_;
    double builtRate_ = 0.0;
    bool built_ = false, prepared_ = false, registered_ = false;
    std::shared_ptr<const ConvolverKernel> current_;
    rt::SnapshotPublisher<KernelSlot> kernels_;

    // Real-time side.
    TriodeStage stage1_, stage2_, stage3_;
    tonestack::Components stack_;
    tonestack::Filter toneStack_;
    FirstOrder volume_;
    SoftClip inverter_, power_;
    Biquad transformerHigh_, transformerLow_, speakerBump_, speakerRise_;
    Oversampler upFront_, downFront_, upBack_, downBack_;
    Tank tank_;
    Convolver convolver_;
    std::vector<float> mono_, wetLeft_, wetRight_;
    // Per sample, for the output and the feedback: the output gain, the input's
    // level, the gate's reduction, the power amp's drive and the tremolo's lamp.
    std::vector<float> gains_, levels_, reductions_, drives_, lamps_;
    std::array<float, smoothCount> smoothed_{};
    std::array<float, 3> knobs_{};
    std::array<float, 5> designed_{};
    int factor_ = 4, configuredFactor_ = 4;
    double feedbackSample_ = 0.0, lfoPhase_ = 0.0;
    float smoothStep_ = 0.01f, gateAttack_ = 0.1f, gateRelease_ = 0.01f, gateOpen_ = 0.1f, gateClose_ = 0.01f;
    float sagAttack_ = 0.01f, sagRelease_ = 0.001f, ldrAttack_ = 0.01f, ldrRelease_ = 0.001f,
          fadeStep_ = 0.01f;
    float gateEnvelope_ = 0.0f, gateGain_ = 1.0f, sagEnvelope_ = 0.0f, ldr_ = 0.0f, fade_ = 1.0f;
    float cabinetMix_ = 1.0f;
    int gateHeld_ = 0, gateHold_ = 2400;
    bool gateIsOpen_ = true, primed_ = false;
};

Amp* asAmp(engine::InsertProcessor& p)
{
    return p.typeName() == type ? dynamic_cast<Amp*>(&p) : nullptr;
}
const Amp* asAmp(const engine::InsertProcessor& p)
{
    return p.typeName() == type ? dynamic_cast<const Amp*>(&p) : nullptr;
}
} // namespace

std::unique_ptr<engine::InsertProcessor> create(sampler::Loader loader)
{
    return std::make_unique<Amp>(std::move(loader));
}

void settleCabinet(engine::InsertProcessor& processor)
{
    if (auto* a = asAmp(processor))
        a->settle();
}

std::string userResponsePath(const engine::InsertProcessor& processor)
{
    const auto* a = asAmp(processor);
    return a != nullptr ? a->path() : std::string{};
}

std::string userResponseError(const engine::InsertProcessor& processor)
{
    const auto* a = asAmp(processor);
    return a != nullptr ? a->error() : std::string{};
}

} // namespace northlight::plugins::amp
