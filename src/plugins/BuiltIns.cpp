#include "plugins/BuiltIns.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Amp.h"
#include "plugins/Aurora.h"
#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/Bloom.h"
#include "plugins/ChannelStrip.h"
#include "plugins/Exciter.h"
#include "plugins/Glue.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Nebula.h"
#include "plugins/Patina.h"
#include "plugins/Pedals.h"
#include "plugins/ProcessorBase.h"
#include "plugins/Pulsar.h"
#include "plugins/Sampler.h"
#include "plugins/Starbit.h"
#include "plugins/TapeEcho.h"
#include "plugins/Widener.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <numbers>
#include <vector>

namespace northlight::plugins
{

namespace
{
// ----- gain -----------------------------------------------------------------------------------

class Gain final : public MeteredBuiltIn
{
public:
    Gain() : MeteredBuiltIn(types::gain, {{"Gain", "dB", -60.0f, 24.0f, 0.0f, false}}) {}

    void prepareBuiltIn(double, int) override { current_ = dbToGain(parameter(0)); }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
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

class Eq final : public MeteredBuiltIn
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
        : MeteredBuiltIn(types::eq,
                         {{"High-pass", "Hz", 0.0f, 1000.0f, 0.0f, true},
                          {"Low freq", "Hz", 30.0f, 1000.0f, 120.0f, true},
                          {"Low gain", "dB", -18.0f, 18.0f, 0.0f, false},
                          {"Mid freq", "Hz", 100.0f, 10000.0f, 1000.0f, true},
                          {"Mid gain", "dB", -18.0f, 18.0f, 0.0f, false},
                          {"Mid Q", "", 0.3f, 10.0f, 1.0f, true},
                          {"High freq", "Hz", 1500.0f, 18000.0f, 8000.0f, true},
                          {"High gain", "dB", -18.0f, 18.0f, 0.0f, false}},
                         true) // taps for the editor's analyser
    {
    }

    void prepareBuiltIn(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        lastParams_.fill(std::nanf(""));
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
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

// The compressor is the channel strip's (strip::createKeyedCompressor).

// ----- limiter ----------------------------------------------------------------------------------

// Brickwall with lookahead: the signal is delayed by the lookahead while the gain
// ramps down ahead of a peak, so the ceiling holds without clipping; the delay is
// reported as latency and compensated across the graph.
class Limiter final : public MeteredBuiltIn
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
        : MeteredBuiltIn(types::limiter,
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
    void prepareBuiltIn(double, int) override
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

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
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
                blockReduction_ = std::max(blockReduction_, -gainToDb(gain_));
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
            if (!bypassedRun_)
                blockReduction_ = std::max(blockReduction_, -gainToDb(gain));

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
// ----- reverb ---------------------------------------------------------------------------------

// A feedback delay network: the input (pre-delayed, low-cut) through four
// series allpasses for diffusion, then eight delay lines mixed by a Householder
// matrix, each with the gain that makes it fall 60 dB in the decay time and a
// one-pole low-pass for damping. Even lines feed the left output, odd the
// right. Memory is allocated in prepare for the largest size.
class Reverb final : public MeteredBuiltIn
{
public:
    static constexpr int lines = 8;
    static constexpr float maxPreDelayMs = 250.0f;

    Reverb()
        : MeteredBuiltIn(types::reverb,
                         {{"Pre-delay", "ms", 0.0f, maxPreDelayMs, 20.0f, false},
                          {"Decay", "s", 0.2f, 12.0f, 2.0f, true},
                          {"Size", "%", 0.0f, 100.0f, 60.0f, false},
                          {"Damping", "Hz", 1000.0f, 20000.0f, 6000.0f, true},
                          {"Low cut", "Hz", 20.0f, 1000.0f, 120.0f, true},
                          {"Width", "%", 0.0f, 100.0f, 100.0f, false},
                          {"Mix", "%", 0.0f, 100.0f, 30.0f, false}})
    {
    }

private:
    // Line lengths at 44.1 kHz and size 1 (mutually prime), and the diffusers'.
    static constexpr int baseLengths[lines] = {1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
    static constexpr int diffuserLengths[4] = {142, 107, 379, 277};
    static constexpr float maxSizeScale = 1.6f;

    static float sizeScale(float percent) noexcept { return 0.4f + percent * 0.012f; } // 0.4 .. 1.6

    void prepareBuiltIn(double sampleRate, int) override
    {
        const double ratio = sampleRate / 44100.0;
        for (int i = 0; i < lines; ++i)
        {
            auto& line = lines_[static_cast<size_t>(i)];
            line.buffer.assign(static_cast<size_t>(baseLengths[i] * ratio * maxSizeScale) + 4, 0.0f);
            line.write = 0;
            line.low = 0.0f;
        }
        for (int i = 0; i < 4; ++i)
        {
            auto& d = diffusers_[static_cast<size_t>(i)];
            d.buffer.assign(static_cast<size_t>(diffuserLengths[i] * ratio) + 1, 0.0f);
            d.pos = 0;
        }
        preDelay_.assign(static_cast<size_t>(sampleRate * maxPreDelayMs * 0.001) + 2, 0.0f);
        preWrite_ = 0;
        lastLowCut_ = -1.0f;
        lowCut_.reset();
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const auto rate = static_cast<float>(sampleRate_);
        const float wet = parameter(reverb::mix) * 0.01f;
        const float width = parameter(reverb::width) * 0.01f;
        const float decay = std::max(0.05f, parameter(reverb::decay));
        const float scale = sizeScale(parameter(reverb::size)) * rate / 44100.0f;
        const int preSamples = std::clamp(static_cast<int>(parameter(reverb::preDelay) * 0.001f * rate),
                                          0,
                                          static_cast<int>(preDelay_.size()) - 2);
        const float damp =
            1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * parameter(reverb::damping) / rate);
        if (const float lowCut = parameter(reverb::lowCut); lowCut != lastLowCut_)
        {
            lastLowCut_ = lowCut;
            lowCut_.setHighPass(sampleRate_, lowCut);
        }
        int lengths[lines];
        float gains[lines];
        for (int i = 0; i < lines; ++i)
        {
            const auto& line = lines_[static_cast<size_t>(i)];
            lengths[i] = std::clamp(
                static_cast<int>(baseLengths[i] * scale), 1, static_cast<int>(line.buffer.size()) - 1);
            gains[i] = std::pow(10.0f, -3.0f * static_cast<float>(lengths[i]) / (rate * decay));
        }
        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n], dryR = right[n];
            // Pre-delay, low cut, diffusion.
            preDelay_[static_cast<size_t>(preWrite_)] = 0.5f * (dryL + dryR);
            int read = preWrite_ - preSamples;
            if (read < 0)
                read += static_cast<int>(preDelay_.size());
            float x = lowCut_.process(preDelay_[static_cast<size_t>(read)]);
            if (++preWrite_ == static_cast<int>(preDelay_.size()))
                preWrite_ = 0;
            for (auto& d : diffusers_)
            {
                const float delayed = d.buffer[static_cast<size_t>(d.pos)];
                const float v = x + 0.7f * delayed;
                d.buffer[static_cast<size_t>(d.pos)] = v;
                x = delayed - 0.7f * v;
                if (++d.pos == static_cast<int>(d.buffer.size()))
                    d.pos = 0;
            }
            // The network: read, damp, mix, feed back with the input.
            float outs[lines];
            float sum = 0.0f;
            for (int i = 0; i < lines; ++i)
            {
                auto& line = lines_[static_cast<size_t>(i)];
                int r = line.write - lengths[i];
                if (r < 0)
                    r += static_cast<int>(line.buffer.size());
                line.low += damp * (line.buffer[static_cast<size_t>(r)] - line.low);
                outs[i] = line.low * gains[i];
                sum += outs[i];
            }
            const float householder = sum * (2.0f / lines);
            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < lines; ++i)
            {
                auto& line = lines_[static_cast<size_t>(i)];
                line.buffer[static_cast<size_t>(line.write)] = outs[i] - householder + x;
                if (++line.write == static_cast<int>(line.buffer.size()))
                    line.write = 0;
                (i % 2 == 0 ? wetL : wetR) += outs[i] * ((i / 2) % 2 == 0 ? 1.0f : -1.0f);
            }
            wetL *= 0.35f;
            wetR *= 0.35f;
            // Width: from mono (0) to the full spread (1).
            const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * width;
            left[n] = dryL * (1.0f - wet) + (mid + side) * wet;
            right[n] = dryR * (1.0f - wet) + (mid - side) * wet;
        }
    }

    struct Line
    {
        std::vector<float> buffer;
        int write = 0;
        float low = 0.0f;
    };
    struct Diffuser
    {
        std::vector<float> buffer;
        int pos = 0;
    };
    std::array<Line, lines> lines_;
    std::array<Diffuser, 4> diffusers_;
    std::vector<float> preDelay_;
    int preWrite_ = 0;
    Biquad lowCut_;
    float lastLowCut_ = -1.0f;
};

// ----- delay ----------------------------------------------------------------------------------

// A stereo delay: the time glides to a new value (a short slew, like tape, so a
// change never clicks), the repeats darken through a one-pole low-pass and
// feed back; ping-pong sends the input to the left line and crosses the
// feedback so the repeats alternate sides. Synced, the time is a note value at
// the project tempo (gliding to a new tempo too). Four seconds of buffer (a
// whole note at 60 bpm), allocated in prepare.
class Delay final : public MeteredBuiltIn
{
public:
    static constexpr float maxTimeMs = 2000.0f;
    static constexpr double bufferSeconds = 4.0;

    Delay()
        : MeteredBuiltIn(
              types::delay,
              {{"Time", "ms", 1.0f, maxTimeMs, 375.0f, true},
               {"Feedback", "%", 0.0f, 95.0f, 35.0f, false},
               {"Tone", "Hz", 500.0f, 20000.0f, 8000.0f, true},
               {"Ping-pong", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}},
               {"Mix", "%", 0.0f, 100.0f, 30.0f, false},
               {"Sync", "", 0.0f, static_cast<float>(sync::choiceCount - 1), 0.0f, false, sync::choices()}})
    {
    }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        const auto size = static_cast<size_t>(sampleRate * bufferSeconds) + 4;
        for (auto& c : channels_)
        {
            c.buffer.assign(size, 0.0f);
            c.low = 0.0f;
        }
        write_ = 0;
        primed_ = false; // the first block takes the time as it is then (parameters arrive after prepare)
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const auto rate = static_cast<float>(sampleRate_);
        const int size = static_cast<int>(channels_[0].buffer.size());
        const float synced = parameter(delay::sync);
        const double seconds = sync::quarters(synced) > 0.0 ? sync::seconds(synced, playHead().tempoBpm)
                                                            : parameter(delay::time) * 0.001;
        const float target =
            std::clamp(static_cast<float>(seconds) * rate, 1.0f, static_cast<float>(size - 3));
        const float slew = 1.0f - std::exp(-1.0f / (0.05f * rate)); // 50 ms glide
        const float feedback = parameter(delay::feedback) * 0.01f;
        const float tone = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * parameter(delay::tone) / rate);
        const bool pingPong = parameter(delay::pingPong) >= 0.5f;
        const float wet = parameter(delay::mix) * 0.01f;
        if (!primed_)
        {
            current_ = target;
            primed_ = true;
        }
        for (int n = 0; n < numSamples; ++n)
        {
            current_ += (target - current_) * slew;
            float read = static_cast<float>(write_) - current_;
            if (read < 0.0f)
                read += static_cast<float>(size);
            const int i0 = std::clamp(static_cast<int>(read), 0, size - 1); // float rounding can land on size
            const int i1 = i0 + 1 == size ? 0 : i0 + 1;
            const float t = read - static_cast<float>(i0);
            float echo[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = channels_[static_cast<size_t>(ch)];
                const float v =
                    c.buffer[static_cast<size_t>(i0)] * (1.0f - t) + c.buffer[static_cast<size_t>(i1)] * t;
                c.low += tone * (v - c.low);
                echo[ch] = c.low;
            }
            const float inL = left[n], inR = right[n];
            if (pingPong)
            {
                channels_[0].buffer[static_cast<size_t>(write_)] = 0.5f * (inL + inR) + echo[1] * feedback;
                channels_[1].buffer[static_cast<size_t>(write_)] = echo[0] * feedback;
            }
            else
            {
                channels_[0].buffer[static_cast<size_t>(write_)] = inL + echo[0] * feedback;
                channels_[1].buffer[static_cast<size_t>(write_)] = inR + echo[1] * feedback;
            }
            if (++write_ == size)
                write_ = 0;
            left[n] = inL * (1.0f - wet) + echo[0] * wet;
            right[n] = inR * (1.0f - wet) + echo[1] * wet;
        }
    }

    struct Channel
    {
        std::vector<float> buffer;
        float low = 0.0f;
    };
    std::array<Channel, 2> channels_;
    int write_ = 0;
    float current_ = 0.0f; // the delay in samples, gliding to the parameter
    bool primed_ = false;
};

} // namespace

std::vector<std::string> BuiltInFactory::typeNames() const
{
    return {types::gain,   types::eq,     types::compressor, types::opto,   glue::type,    widener::type,
            patina::type,  exciter::type, types::limiter,    types::reverb, types::delay,  tapeecho::type,
            bloom::type,   amp::type,     gravity::type,     flare::type,   comet::type,   tide::type,
            polaris::type, aurora::type,  pulsar::type,      nebula::type,  starbit::type, sampler::type};
}

bool BuiltInFactory::isInstrument(const std::string& typeName)
{
    return typeName == aurora::type || typeName == pulsar::type || typeName == nebula::type ||
           typeName == starbit::type || typeName == sampler::type;
}

std::unique_ptr<engine::InsertProcessor> BuiltInFactory::create(const std::string& typeName) const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("BuiltInFactory::create");
    if (strip::isStripType(typeName))
        return strip::create(typeName);
    if (typeName == types::gain)
        return std::make_unique<Gain>();
    if (typeName == types::eq)
        return std::make_unique<Eq>();
    if (typeName == types::compressor)
        return strip::createKeyedCompressor(types::compressor); // the channel strip's, with a key
    if (typeName == types::opto)
        return strip::createOpto(types::opto);
    if (typeName == glue::type)
        return glue::create();
    if (typeName == widener::type)
        return widener::create();
    if (typeName == patina::type)
        return patina::create();
    if (typeName == exciter::type)
        return exciter::create();
    if (typeName == types::limiter)
        return std::make_unique<Limiter>();
    if (typeName == aurora::type)
        return aurora::create();
    if (typeName == pulsar::type)
        return pulsar::create();
    if (typeName == nebula::type)
        return nebula::create();
    if (typeName == starbit::type)
        return starbit::create();
    if (typeName == sampler::type)
        return sampler::create(sampleLoader_);
    if (typeName == types::reverb)
        return std::make_unique<Reverb>();
    if (typeName == types::delay)
        return std::make_unique<Delay>();
    if (typeName == tapeecho::type)
        return tapeecho::create();
    if (typeName == bloom::type)
        return bloom::create();
    if (typeName == amp::type)
        return amp::create(sampleLoader_);
    if (typeName == gravity::type)
        return gravity::create();
    if (typeName == flare::type)
        return flare::create();
    if (typeName == comet::type)
        return comet::create();
    if (typeName == tide::type)
        return tide::create();
    if (typeName == polaris::type)
        return polaris::create();
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
    if (typeName == types::opto)
        return "Opto Compressor";
    if (typeName == glue::type)
        return "Glue Compressor";
    if (typeName == widener::type)
        return "Stereo Widener";
    if (typeName == patina::type)
        return "Patina";
    if (typeName == exciter::type)
        return "Exciter";
    if (typeName == types::limiter)
        return "Limiter";
    if (typeName == aurora::type)
        return "Aurora";
    if (typeName == pulsar::type)
        return "Pulsar";
    if (typeName == nebula::type)
        return "Nebula";
    if (typeName == starbit::type)
        return "Starbit";
    if (typeName == sampler::type)
        return "Sampler";
    if (typeName == types::reverb)
        return "Reverb";
    if (typeName == types::delay)
        return "Delay";
    if (typeName == tapeecho::type)
        return "Tape Echo";
    if (typeName == bloom::type)
        return "Bloom";
    if (typeName == amp::type)
        return "Corona";
    if (typeName == gravity::type)
        return "Gravity";
    if (typeName == flare::type)
        return "Flare";
    if (typeName == comet::type)
        return "Comet";
    if (typeName == tide::type)
        return "Tide";
    if (typeName == polaris::type)
        return "Polaris";
    return typeName;
}

const std::vector<engine::ParameterInfo>& parameterInfo(const std::string& typeName)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("plugins::parameterInfo");
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

core::ParameterLayout builtInParameterLayout(const std::string& typeName)
{
    static std::mutex mutex;
    static std::map<std::string, core::ParameterLayout> cache;
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(typeName); it != cache.end())
        return it->second;
    core::ParameterLayout layout;
    if (const auto processor = BuiltInFactory().create(typeName))
        for (int i = 0; i < static_cast<int>(processor->parameters().size()); ++i)
        {
            layout.keys.push_back(processor->parameterKey(i));
            layout.defaults.push_back(processor->parameters()[static_cast<size_t>(i)].defaultValue);
        }
    cache[typeName] = layout;
    return layout;
}

} // namespace northlight::plugins
