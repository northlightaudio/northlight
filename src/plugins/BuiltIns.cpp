#include "plugins/BuiltIns.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/ChannelStrip.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

namespace aural::plugins
{

namespace
{
// ----- gain -----------------------------------------------------------------------------------

class Gain final : public ProcessorBase
{
public:
    Gain() : ProcessorBase(types::gain, {{"Gain", "dB", -60.0f, 24.0f, 0.0f, false}}) {}

    void prepareAudio(double, int) override { current_ = dbToGain(parameter(0)); }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float target = dbToGain(parameter(0));
        // Ramp across the block so gain changes never click.
        const float step = (target - current_) / static_cast<float>(std::max(1, numSamples));
        float g = current_;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] *= g;
            right[i] *= g;
            g += step;
        }
        current_ = target;
    }

private:
    float current_ = 1.0f;
};

// ----- EQ -------------------------------------------------------------------------------------

class Eq final : public ProcessorBase
{
public:
    enum Param
    {
        highPassHz,
        lowHz,
        lowDb,
        midHz,
        midDb,
        midQ,
        highHz,
        highDb,
    };

    Eq()
        : ProcessorBase(types::eq,
                        {{"High-pass", "Hz", 0.0f, 1000.0f, 0.0f, true},
                         {"Low freq", "Hz", 30.0f, 1000.0f, 120.0f, true},
                         {"Low gain", "dB", -18.0f, 18.0f, 0.0f, false},
                         {"Mid freq", "Hz", 100.0f, 10000.0f, 1000.0f, true},
                         {"Mid gain", "dB", -18.0f, 18.0f, 0.0f, false},
                         {"Mid Q", "", 0.3f, 10.0f, 1.0f, true},
                         {"High freq", "Hz", 1500.0f, 18000.0f, 8000.0f, true},
                         {"High gain", "dB", -18.0f, 18.0f, 0.0f, false}})
    {
    }

    void prepareAudio(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        lastParams_.fill(std::nanf(""));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        // A coefficient change ramps across the block (linear interpolation of
        // the biquad coefficients) instead of switching between samples.
        const bool ramp = updateCoefficients();
        for (int i = 0; i < numSamples; ++i)
        {
            if (ramp)
            {
                const float t = static_cast<float>(i + 1) / static_cast<float>(numSamples);
                for (size_t f = 0; f < 8; ++f)
                    filters_[f].blend(previous_[f], targets_[f], t);
            }
            float l = left[i], r = right[i];
            for (int f = 0; f < 4; ++f)
            {
                l = filters_[static_cast<size_t>(f)].process(l);
                r = filters_[static_cast<size_t>(4 + f)].process(r);
            }
            left[i] = l;
            right[i] = r;
        }
    }

private:
    // Recomputes the target coefficients when a parameter changed. Returns true
    // when this block should ramp from the previous coefficients to the targets.
    bool updateCoefficients() noexcept
    {
        std::array<float, 8> params{};
        bool changed = false;
        for (int i = 0; i < 8; ++i)
        {
            params[static_cast<size_t>(i)] = parameter(i);
            changed = changed || params[static_cast<size_t>(i)] != lastParams_[static_cast<size_t>(i)];
        }
        if (!changed)
            return false;
        const bool first = std::isnan(lastParams_[0]);
        lastParams_ = params;
        for (size_t f = 0; f < 8; ++f)
            previous_[f] = filters_[f];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* f = targets_.data() + ch * 4;
            if (params[highPassHz] > 0.0f)
                f[0].setHighPass(sampleRate_, params[highPassHz]);
            else
                f[0].bypass();
            f[1].setLowShelf(sampleRate_, params[lowHz], params[lowDb]);
            f[2].setPeaking(sampleRate_, params[midHz], params[midQ], params[midDb]);
            f[3].setHighShelf(sampleRate_, params[highHz], params[highDb]);
        }
        if (first)
        {
            // Nothing to ramp from on the first block: jump.
            for (size_t f = 0; f < 8; ++f)
                filters_[f].blend(targets_[f], targets_[f], 1.0f);
            return false;
        }
        return true;
    }

    std::array<Biquad, 8> filters_; // 4 per channel, the live coefficients
    std::array<Biquad, 8> previous_, targets_;
    std::array<float, 8> lastParams_{};
};

// ----- compressor -------------------------------------------------------------------------------

class Compressor final : public ProcessorBase
{
public:
    enum Param
    {
        thresholdDb,
        ratio,
        attackMs,
        releaseMs,
        kneeDb,
        makeupDb,
    };

    Compressor()
        : ProcessorBase(types::compressor,
                        {{"Threshold", "dB", -60.0f, 0.0f, -18.0f, false},
                         {"Ratio", ":1", 1.0f, 20.0f, 4.0f, true},
                         {"Attack", "ms", 0.1f, 200.0f, 10.0f, true},
                         {"Release", "ms", 10.0f, 2000.0f, 100.0f, true},
                         {"Knee", "dB", 0.0f, 24.0f, 6.0f, false},
                         {"Makeup", "dB", 0.0f, 24.0f, 0.0f, false}})
    {
    }

    void prepareAudio(double, int) override { envelopeDb_ = minusInfinityDb; }

    // Peak detector in the log domain, stereo-linked, feed-forward. With a
    // sidechain the detector listens to the key signal instead of the audio.
    bool acceptsSidechain() const noexcept override { return true; }
    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        processKeyed(left,
                     right,
                     keyLeft_ != nullptr ? keyLeft_ : left,
                     keyRight_ != nullptr ? keyRight_ : right,
                     numSamples);
    }
    void processKeyed(
        float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
    {
        const float threshold = parameter(thresholdDb);
        const float r = parameter(ratio);
        const float knee = parameter(kneeDb);
        const float makeup = dbToGain(parameter(makeupDb));
        const auto coefficient = [this](float ms)
        { return std::exp(-1.0f / (static_cast<float>(sampleRate_) * ms * 0.001f)); };
        const float attack = coefficient(parameter(attackMs));
        const float release = coefficient(parameter(releaseMs));

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = std::max(std::abs(keyLeft[i]), std::abs(keyRight[i]));
            const float inputDb = gainToDb(input);
            const float a = inputDb > envelopeDb_ ? attack : release;
            envelopeDb_ = a * envelopeDb_ + (1.0f - a) * inputDb;

            // Soft-knee gain computer.
            const float over = envelopeDb_ - threshold;
            float reductionDb = 0.0f;
            if (knee > 0.0f && std::abs(over) <= knee * 0.5f)
            {
                const float x = over + knee * 0.5f;
                reductionDb = (1.0f / r - 1.0f) * x * x / (2.0f * knee);
            }
            else if (over > 0.0f)
                reductionDb = (1.0f / r - 1.0f) * over;

            const float g = dbToGain(reductionDb) * makeup;
            left[i] *= g;
            right[i] *= g;
        }
        reduction_.store(
            std::max(0.0f, -(envelopeDb_ > threshold ? (1.0f / r - 1.0f) * (envelopeDb_ - threshold) : 0.0f)),
            std::memory_order_relaxed);
    }

private:
    float envelopeDb_ = minusInfinityDb;
    std::atomic<float> reduction_{0.0f};
};

// ----- limiter ----------------------------------------------------------------------------------

// Brickwall by instantaneous gain computation with a fast release: no lookahead, so
// it adds no latency (delay compensation arrives in Phase 5). Transients that
// exceed the ceiling within one sample are clipped to it.
class Limiter final : public ProcessorBase
{
public:
    enum Param
    {
        ceilingDb,
        releaseMs,
        lookaheadMs,
    };

    static constexpr float maxLookaheadMs = 10.0f;

    Limiter()
        : ProcessorBase(types::limiter,
                        {{"Ceiling", "dB", -24.0f, 0.0f, -0.3f, false},
                         {"Release", "ms", 1.0f, 1000.0f, 50.0f, true},
                         {"Lookahead", "ms", 0.0f, maxLookaheadMs, 1.5f, false}})
    {
    }

    // Lookahead: the signal is delayed by L samples while the gain the next L
    // samples need is already known, so a peak is attenuated before it arrives and
    // nothing crosses the ceiling. The gain applied to the sample leaving the delay
    // at time t is the mean over the last L samples of the running minimum of the
    // needed gain over [t, t + L] (released), which is never above what sample t
    // needs and ramps smoothly instead of stepping.
    void prepareAudio(double, int) override
    {
        capacity_ = static_cast<int>(std::ceil(sampleRate_ * maxLookaheadMs * 0.001)) + 2;
        delayL_.assign(static_cast<size_t>(capacity_), 0.0f);
        delayR_.assign(static_cast<size_t>(capacity_), 0.0f);
        needed_.assign(static_cast<size_t>(capacity_), 1.0f);
        minima_.assign(static_cast<size_t>(capacity_), 0);
        smoothed_.assign(static_cast<size_t>(capacity_), 1.0f);
        reset(lookaheadSamples());
    }

    int latencySamples() const noexcept override { return lookaheadSamples(); }

    // Bypassed: the signal still goes through the lookahead delay (the latency
    // the graph was solved with) with unity gain.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        bypassedRun_ = true;
        processAudio(left, right, numSamples);
        bypassedRun_ = false;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float ceiling = dbToGain(parameter(ceilingDb));
        const float release =
            std::exp(-1.0f / (static_cast<float>(sampleRate_) * parameter(releaseMs) * 0.001f));
        const int lookahead = capacity_ > 0 ? lookaheadSamples() : 0; // nothing allocated before prepare
        if (lookahead != length_)
            reset(lookahead); // a changed lookahead restarts the pipeline (no allocation)

        if (length_ == 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                if (bypassedRun_)
                    continue;
                const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
                const float needed = peak > ceiling ? ceiling / peak : 1.0f;
                gain_ = needed < gain_ ? needed : release * gain_ + (1.0f - release);
                if (gain_ > 1.0f)
                    gain_ = 1.0f;
                left[i] = std::clamp(left[i] * gain_, -ceiling, ceiling);
                right[i] = std::clamp(right[i] * gain_, -ceiling, ceiling);
            }
            return;
        }

        const int L = length_;
        const int window = L + 1; // the sample itself and the next L
        for (int i = 0; i < numSamples; ++i)
        {
            // 1. The gain this input sample needs, into the window ring (modulo capacity).
            const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
            const float needed = bypassedRun_ ? 1.0f : (peak > ceiling ? ceiling / peak : 1.0f);
            needed_[static_cast<size_t>(head_)] = needed;
            // A monotonic deque of ring indices with increasing needed gain: the
            // front is the window minimum. Drop what left the window, then every
            // candidate this sample beats.
            while (dequeCount_ > 0 && age(minima_[static_cast<size_t>(dequeFront_)]) >= window)
                popFront();
            while (dequeCount_ > 0 &&
                   needed_[static_cast<size_t>(minima_[static_cast<size_t>(back())])] >= needed)
                --dequeCount_;
            minima_[static_cast<size_t>((dequeFront_ + dequeCount_) % capacity_)] = head_;
            ++dequeCount_;

            // 2. The window minimum applies to the sample leaving the delay; release
            //    lets it recover between peaks.
            const float minimum = needed_[static_cast<size_t>(minima_[static_cast<size_t>(dequeFront_)])];
            released_ = minimum < released_ ? minimum : release * released_ + (1.0f - release);
            if (released_ > 1.0f)
                released_ = 1.0f;

            // 3. Smooth: the mean of the last L released values (double sum, no drift).
            const auto slot = static_cast<size_t>(pos_); // rings of length L
            sum_ += static_cast<double>(released_) - static_cast<double>(smoothed_[slot]);
            smoothed_[slot] = released_;
            const float gain = std::min(1.0f, static_cast<float>(sum_ / L));

            // 4. The sample written L samples ago leaves through the gain; this one enters.
            const float outL = delayL_[slot] * gain;
            const float outR = delayR_[slot] * gain;
            delayL_[slot] = left[i];
            delayR_[slot] = right[i];
            left[i] = bypassedRun_ ? outL : std::clamp(outL, -ceiling, ceiling);
            right[i] = bypassedRun_ ? outR : std::clamp(outR, -ceiling, ceiling);
            if (++pos_ == L)
                pos_ = 0;
            if (++head_ == capacity_)
                head_ = 0;
        }
    }

private:
    int lookaheadSamples() const noexcept
    {
        const auto ms = std::clamp(parameter(lookaheadMs), 0.0f, maxLookaheadMs);
        return static_cast<int>(std::lround(sampleRate_ * ms * 0.001));
    }

    void reset(int length) noexcept
    {
        length_ = std::clamp(length, 0, std::max(0, capacity_ - 2));
        head_ = 0;
        pos_ = 0;
        dequeFront_ = 0;
        dequeCount_ = 0;
        released_ = 1.0f;
        sum_ = static_cast<double>(std::max(1, length_)); // L ones
        gain_ = 1.0f;
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        std::fill(needed_.begin(), needed_.end(), 1.0f);
        std::fill(smoothed_.begin(), smoothed_.end(), 1.0f);
    }

    int back() const noexcept { return (dequeFront_ + dequeCount_ - 1 + capacity_) % capacity_; }
    void popFront() noexcept
    {
        if (++dequeFront_ == capacity_)
            dequeFront_ = 0;
        --dequeCount_;
    }
    // How many samples ago the needed_ ring index was written (0 = this sample).
    int age(int index) const noexcept
    {
        int a = head_ - index;
        if (a < 0)
            a += capacity_;
        return a;
    }

    bool bypassedRun_ = false; // processBypassed: unity gain, no clamp
    int capacity_ = 0;
    int length_ = 0;
    int head_ = 0; // needed_ / minima_ ring position (modulo capacity_)
    int pos_ = 0;  // delay and smoothing ring position (modulo length_)
    int dequeFront_ = 0, dequeCount_ = 0;
    float released_ = 1.0f;
    double sum_ = 1.0;
    float gain_ = 1.0f;
    std::vector<float> delayL_, delayR_, needed_, smoothed_;
    std::vector<int> minima_;
};
// ----- synth ----------------------------------------------------------------------------------

// A small polyphonic subtractive synth: a polyBLEP saw/square per voice through a
// state-variable low-pass, with an ADSR on the amplitude. Sixteen voices, the
// oldest stolen. Mono, written to both channels. Enough to audition a part.
class Synth final : public ProcessorBase
{
public:
    static constexpr int maxVoices = 16;
    Synth()
        : ProcessorBase(types::synth,
                        {{"Level", "dB", -60.0f, 12.0f, -12.0f, false},
                         {"Shape", "", 0.0f, 1.0f, 0.0f, false}, // saw .. square
                         {"Cutoff", "Hz", 40.0f, 20000.0f, 6000.0f, true},
                         {"Resonance", "", 0.0f, 1.0f, 0.2f, false},
                         {"Attack", "ms", 1.0f, 2000.0f, 5.0f, true},
                         {"Decay", "ms", 1.0f, 2000.0f, 200.0f, true},
                         {"Sustain", "", 0.0f, 1.0f, 0.7f, false},
                         {"Release", "ms", 1.0f, 4000.0f, 200.0f, true}})
    {
    }

    bool acceptsMidi() const noexcept override { return true; }

private:
    enum class Stage : std::uint8_t
    {
        off,
        attack,
        decay,
        sustain,
        release
    };
    struct Voice
    {
        Stage stage = Stage::off;
        int note = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        double increment = 0.0;
        float env = 0.0f;
        float low = 0.0f, band = 0.0f; // filter state
        std::uint32_t age = 0;
    };

    void prepareAudio(double, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        age_ = 0;
    }

    void noteOn(int note, int velocity) noexcept
    {
        Voice* voice = nullptr;
        for (auto& v : voices_) // the same note retriggers its voice
            if (v.note == note && v.stage != Stage::off)
                voice = &v;
        if (voice == nullptr)
            for (auto& v : voices_)
                if (v.stage == Stage::off)
                {
                    voice = &v;
                    break;
                }
        if (voice == nullptr) // steal the oldest
        {
            voice = &voices_[0];
            for (auto& v : voices_)
                if (v.age < voice->age)
                    voice = &v;
        }
        const double frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        voice->note = note;
        voice->velocity = static_cast<float>(velocity) / 127.0f;
        voice->increment = frequency / sampleRate_;
        if (voice->stage == Stage::off)
        {
            voice->phase = 0.0;
            voice->env = 0.0f;
            voice->low = voice->band = 0.0f;
        }
        voice->stage = Stage::attack;
        voice->age = age_++;
    }

    void noteOff(int note) noexcept
    {
        for (auto& v : voices_)
            if (v.note == note && v.stage != Stage::off && v.stage != Stage::release)
                v.stage = Stage::release;
    }

    void allNotesOff() noexcept
    {
        for (auto& v : voices_)
            if (v.stage != Stage::off)
                v.stage = Stage::release;
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        switch (e.type)
        {
            case engine::MidiEvent::Type::noteOn:
                if (e.velocity > 0)
                    noteOn(e.note, e.velocity);
                else
                    noteOff(e.note);
                break;
            case engine::MidiEvent::Type::noteOff:
                noteOff(e.note);
                break;
            case engine::MidiEvent::Type::allNotesOff:
                allNotesOff();
                break;
            case engine::MidiEvent::Type::controller:
                if (e.note == 123 || e.note == 120) // all notes / all sound off
                    allNotesOff();
                break;
        }
    }

    // PolyBLEP: the band-limited step correction around a discontinuity.
    static float polyBlep(double t, double dt) noexcept
    {
        if (t < dt)
        {
            const double x = t / dt;
            return static_cast<float>(x + x - x * x - 1.0);
        }
        if (t > 1.0 - dt)
        {
            const double x = (t - 1.0) / dt;
            return static_cast<float>(x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float level = dbToGain(parameter(0));
        const float shape = parameter(1);
        const float cutoff = parameter(2);
        const float resonance = parameter(3);
        const float sr = static_cast<float>(sampleRate_);
        const float attackStep = 1.0f / std::max(1.0f, parameter(4) * 0.001f * sr);
        const float decayStep = 1.0f / std::max(1.0f, parameter(5) * 0.001f * sr);
        const float sustain = parameter(6);
        const float releaseStep = 1.0f / std::max(1.0f, parameter(7) * 0.001f * sr);
        // Chamberlin SVF coefficients (stable below about a quarter of the rate).
        const float f = 2.0f * std::sin(3.14159265f * std::min(cutoff, sr * 0.2f) / sr);
        const float q = 2.0f * (1.0f - resonance * 0.95f);

        // Events partition the chunk; the voices run between them.
        size_t next = 0;
        const engine::MidiEventList* events = midi_;
        int at = 0;
        while (at < numSamples)
        {
            int until = numSamples;
            while (events != nullptr && next < events->size())
            {
                const auto& e = (*events)[next];
                const int offset = std::clamp(e.sampleOffset - chunkStart_, 0, numSamples);
                if (offset > at)
                {
                    until = std::min(until, offset);
                    break;
                }
                handle(e);
                ++next;
            }
            for (int i = at; i < until; ++i)
            {
                float sum = 0.0f;
                for (auto& v : voices_)
                {
                    if (v.stage == Stage::off)
                        continue;
                    switch (v.stage)
                    {
                        case Stage::attack:
                            v.env += attackStep;
                            if (v.env >= 1.0f)
                            {
                                v.env = 1.0f;
                                v.stage = Stage::decay;
                            }
                            break;
                        case Stage::decay:
                            v.env -= decayStep;
                            if (v.env <= sustain)
                            {
                                v.env = sustain;
                                v.stage = Stage::sustain;
                            }
                            break;
                        case Stage::sustain:
                            v.env = sustain;
                            break;
                        case Stage::release:
                            v.env -= releaseStep;
                            if (v.env <= 0.0f)
                            {
                                v.env = 0.0f;
                                v.stage = Stage::off;
                                v.note = -1;
                                continue;
                            }
                            break;
                        case Stage::off:
                            continue;
                    }
                    const double dt = v.increment;
                    float saw = static_cast<float>(2.0 * v.phase - 1.0) - polyBlep(v.phase, dt);
                    float osc = saw;
                    if (shape > 0.0f)
                    {
                        double t2 = v.phase + 0.5;
                        if (t2 >= 1.0)
                            t2 -= 1.0;
                        const float saw2 = static_cast<float>(2.0 * t2 - 1.0) - polyBlep(t2, dt);
                        osc = saw * (1.0f - shape) + (saw - saw2) * shape;
                    }
                    v.phase += dt;
                    if (v.phase >= 1.0)
                        v.phase -= 1.0;
                    // SVF low-pass.
                    v.low += f * v.band;
                    const float high = osc - v.low - q * v.band;
                    v.band += f * high;
                    sum += v.low * v.env * v.velocity;
                }
                left[i] = right[i] = sum * level * 0.5f;
            }
            at = until;
        }
    }

    Voice voices_[maxVoices];
    std::uint32_t age_ = 0;
};
} // namespace

std::vector<std::string> BuiltInFactory::typeNames() const
{
    return {types::gain, types::eq, types::compressor, types::limiter, types::synth};
}

bool BuiltInFactory::isInstrument(const std::string& typeName)
{
    return typeName == types::synth;
}

std::unique_ptr<engine::InsertProcessor> BuiltInFactory::create(const std::string& typeName) const
{
    AURAL_ASSERT_NOT_REALTIME("BuiltInFactory::create");
    if (strip::isStripType(typeName))
        return strip::create(typeName);
    if (typeName == types::gain)
        return std::make_unique<Gain>();
    if (typeName == types::eq)
        return std::make_unique<Eq>();
    if (typeName == types::compressor)
        return std::make_unique<Compressor>();
    if (typeName == types::limiter)
        return std::make_unique<Limiter>();
    if (typeName == types::synth)
        return std::make_unique<Synth>();
    return nullptr;
}

std::string BuiltInFactory::displayName(const std::string& typeName)
{
    if (strip::isStripType(typeName))
        return strip::displayName(typeName);
    if (typeName == types::gain)
        return "Gain";
    if (typeName == types::eq)
        return "EQ";
    if (typeName == types::compressor)
        return "Compressor";
    if (typeName == types::limiter)
        return "Limiter";
    if (typeName == types::synth)
        return "Synth";
    return typeName;
}

const std::vector<engine::ParameterInfo>& parameterInfo(const std::string& typeName)
{
    AURAL_ASSERT_NOT_REALTIME("plugins::parameterInfo");
    static std::map<std::string, std::vector<engine::ParameterInfo>> cache;
    auto it = cache.find(typeName);
    if (it == cache.end())
    {
        std::vector<engine::ParameterInfo> infos;
        if (const auto p = BuiltInFactory().create(typeName))
            infos = p->parameters();
        it = cache.emplace(typeName, std::move(infos)).first;
    }
    return it->second;
}

std::vector<float> defaultParameters(const std::string& typeName)
{
    const auto processor = BuiltInFactory().create(typeName);
    std::vector<float> values;
    if (processor != nullptr)
        for (const auto& p : processor->parameters())
            values.push_back(p.defaultValue);
    return values;
}

} // namespace aural::plugins
