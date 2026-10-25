#include "plugins/Sampler.h"

#include "engine/Resampling.h"
#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/SnapshotPublisher.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/SynthVoices.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace northlight::plugins::sampler
{

namespace
{
constexpr int controlInterval = 16; // samples between pitch and filter updates
constexpr double stealSeconds = 0.005;
constexpr double declickSeconds = 0.002; // the fade before a sample's end

// What the real-time thread reads: the sample, behind the publisher's epochs.
struct Slot
{
    std::shared_ptr<const Sample> sample;
    std::uint64_t epoch = 0;
};

// Where the sample plays for the current parameters, in frames.
struct Region
{
    double start = 0.0, end = 0.0;
    bool loop = false;
    double loopStart = 0.0, loopEnd = 0.0, fade = 0.0;
    double loopLength() const noexcept { return loopEnd - loopStart; }
};

Region regionFor(const float* p, const Sample& sample) noexcept
{
    const auto frames = static_cast<double>(sample.frames());
    Region r;
    const double a = std::clamp(static_cast<double>(p[start]), 0.0, 1.0) * frames;
    const double b = std::clamp(static_cast<double>(p[end]), 0.0, 1.0) * frames;
    r.start = std::min(a, b);
    r.end = std::max(a, b);
    if (p[loop] >= 0.5f && p[oneShot] < 0.5f)
    {
        const double ls = std::clamp(static_cast<double>(p[loopStart]) * frames, r.start, r.end);
        const double le = std::clamp(static_cast<double>(p[loopEnd]) * frames, r.start, r.end);
        r.loopStart = std::min(ls, le);
        r.loopEnd = std::max(ls, le);
        r.loop = r.loopEnd - r.loopStart >= 8.0;
        // The crossfade blends the loop's end into the material before its
        // start, so it is as long as there is material for (and at most half
        // the loop).
        if (r.loop)
            r.fade = std::min({static_cast<double>(p[loopFade]) * 0.001 * sample.sampleRate,
                               r.loopLength() * 0.5,
                               r.loopStart});
    }
    return r;
}

struct Voice
{
    int note = -1;
    std::uint32_t age = 0;
    double position = 0.0;  // frames into the sample
    double increment = 0.0; // frames a sample
    double semis = 60.0;    // the pitch now (gliding to the note)
    double glideStep = 0.0; // semitones a sample
    float gain = 1.0f;
    bool stolen = false; // fading out fast to make room
    int countdown = 0;
    synth::Envelope amp;
    // The filter's state (TPT state variable) and coefficients.
    float s1L = 0.0f, s2L = 0.0f, s1R = 0.0f, s2R = 0.0f;
    float g = 0.0f, k = 2.0f, h = 1.0f;
    bool active() const noexcept { return amp.active(); }
};

class Sampler final : public MeteredBuiltIn
{
public:
    explicit Sampler(Loader loader)
        : MeteredBuiltIn(type, sampler::parameterInfo(), true), loader_(std::move(loader))
    {
        for (auto& v : published_)
            v.store(-1, std::memory_order_relaxed);
    }

    bool acceptsMidi() const noexcept override { return true; }

    std::string state() const override { return state_; }

    void setState(const std::string& state) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Sampler::setState");
        state_ = state;
        error_.clear();
        std::shared_ptr<const Sample> sample;
        if (const auto path = decodeState(state); !path.empty())
        {
            if (loader_)
                sample = loader_(path, error_);
            else
                error_ = "no sample loader";
            if (sample == nullptr && error_.empty())
                error_ = "could not read the sample";
        }
        publish(std::move(sample));
    }

    void publish(std::shared_ptr<const Sample> sample)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Sampler::publish");
        controllerSample_ = sample;
        auto slot = std::make_unique<Slot>();
        slot->sample = std::move(sample);
        samples_.publish(std::move(slot));
        // A retired sample goes once the real-time thread has taken a newer one;
        // until then it stays (one per swap at most, and freed with the processor).
        samples_.collect(true);
    }

    std::shared_ptr<const Sample> sample() const { return controllerSample_; }
    const std::string& error() const noexcept { return error_; }

    std::vector<VoiceInfo> sounding() const
    {
        std::vector<VoiceInfo> out;
        for (int i = 0; i < voicePool; ++i)
        {
            const int packed = published_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            if (packed < 0)
                continue;
            out.push_back({positions_[static_cast<size_t>(i)].load(std::memory_order_relaxed),
                           packed & 0xff,
                           (packed & 0x100) != 0});
        }
        return out;
    }

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        age_ = 0;
        lastNote_ = -1;
        for (auto& v : published_)
            v.store(-1, std::memory_order_relaxed);
    }

    // ----- notes ----------------------------------------------------------------------------

    void noteOn(int note, int vel) noexcept
    {
        if (playing_ == nullptr || note < std::lround(p_[keyLow]) || note > std::lround(p_[keyHigh]))
            return;
        // A note already sounding fades out under the new one (no click).
        for (auto& v : voices_)
            if (v.active() && !v.stolen && v.note == note)
                steal(v);
        // Over the limit: the oldest voices make room.
        const int limit = std::clamp(static_cast<int>(std::lround(p_[voices])), 1, maxVoices);
        for (;;)
        {
            int sounding = 0;
            Voice* oldest = nullptr;
            for (auto& v : voices_)
                if (v.active() && !v.stolen)
                {
                    ++sounding;
                    if (oldest == nullptr || v.age < oldest->age)
                        oldest = &v;
                }
            if (sounding < limit || oldest == nullptr)
                break;
            steal(*oldest);
        }
        // A free voice, else the oldest of those still fading.
        Voice* voice = nullptr;
        for (auto& v : voices_)
            if (!v.active())
            {
                voice = &v;
                break;
            }
        if (voice == nullptr)
        {
            voice = &voices_[0];
            for (auto& v : voices_)
                if (v.age < voice->age)
                    voice = &v;
        }
        *voice = Voice{};
        const float amount = std::clamp(p_[velocity], 0.0f, 1.0f);
        voice->gain = 1.0f - amount + amount * static_cast<float>(vel) / 127.0f;
        voice->note = note;
        voice->age = ++age_;
        voice->position = region_.start;
        const double glideMs = p_[glide];
        voice->semis = note;
        if (glideMs > 0.5 && lastNote_ >= 0 && lastNote_ != note)
        {
            voice->semis = lastNote_;
            voice->glideStep = std::abs(note - lastNote_) / (glideMs * 0.001 * sampleRate_);
        }
        voice->amp.start();
        lastNote_ = note;
    }

    static void steal(Voice& v) noexcept
    {
        v.stolen = true;
        v.amp.release();
    }

    void noteOff(int note) noexcept
    {
        if (p_[oneShot] >= 0.5f)
            return; // a one-shot plays to its end
        for (auto& v : voices_)
            if (v.active() && !v.stolen && v.note == note)
                v.amp.release();
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        synth::dispatch(
            e,
            [this](int note, int vel) { noteOn(note, vel); },
            [this](int note) { noteOff(note); },
            [this]
            {
                for (auto& v : voices_)
                    v.amp.release();
            });
    }

    // ----- audio ----------------------------------------------------------------------------

    // The pitch and the filter for the next control interval.
    void control(Voice& v) noexcept
    {
        if (v.semis != v.note)
        {
            const double step = v.glideStep * controlInterval;
            v.semis = v.semis < v.note ? std::min<double>(v.note, v.semis + step)
                                       : std::max<double>(v.note, v.semis - step);
        }
        const double offset = v.semis - std::round(p_[root]) + std::round(p_[tune]) + p_[fine] * 0.01;
        v.increment = std::exp2(offset / 12.0) * playing_->sampleRate / sampleRate_;
        if (filterOn_)
        {
            const double nyquistish = sampleRate_ * 0.45;
            const double hz = std::clamp(
                static_cast<double>(p_[cutoff]) * std::exp2(p_[filterEnv] * v.amp.value), 20.0, nyquistish);
            v.g = static_cast<float>(std::tan(std::numbers::pi * hz / sampleRate_));
            v.k = 2.0f - 1.96f * std::clamp(p_[resonance], 0.0f, 1.0f);
            v.h = 1.0f / (1.0f + v.k * v.g + v.g * v.g);
        }
    }

    // One channel's sample at a position, the loop's far side standing in past its end.
    static float at(const float* data, std::int64_t frames, const Region& r, std::int64_t index) noexcept
    {
        if (r.loop && index >= static_cast<std::int64_t>(r.loopEnd))
            index -= static_cast<std::int64_t>(r.loopLength());
        return index >= 0 && index < frames ? data[index] : 0.0f;
    }

    static float read(const float* data, std::int64_t frames, const Region& r, double position) noexcept
    {
        const auto n = static_cast<std::int64_t>(std::floor(position));
        const auto t = static_cast<float>(position - static_cast<double>(n));
        return engine::hermite(at(data, frames, r, n - 1),
                               at(data, frames, r, n),
                               at(data, frames, r, n + 1),
                               at(data, frames, r, n + 2),
                               t);
    }

    // The voice's sample at its position: inside the loop's crossfade, blended
    // with the same distance before the loop's start, so the wrap is seamless.
    void readVoice(const Voice& v, float& l, float& r) const noexcept
    {
        const auto& s = *playing_;
        const auto frames = s.frames();
        const float* right = s.stereo() ? s.right.data() : s.left.data();
        l = read(s.left.data(), frames, region_, v.position);
        r = s.stereo() ? read(right, frames, region_, v.position) : l;
        if (region_.loop && region_.fade > 0.0 && v.position >= region_.loopEnd - region_.fade)
        {
            const auto t = static_cast<float>((v.position - (region_.loopEnd - region_.fade)) / region_.fade);
            const double earlier = v.position - region_.loopLength();
            const float el = read(s.left.data(), frames, region_, earlier);
            const float er = s.stereo() ? read(right, frames, region_, earlier) : el;
            l = l * (1.0f - t) + el * t;
            r = r * (1.0f - t) + er * t;
        }
    }

    void render(float* left, float* right, int from, int to) noexcept
    {
        const auto sr = static_cast<float>(sampleRate_);
        const float attackStep = 1.0f / std::max(1.0f, p_[attack] * 0.001f * sr);
        const auto coef = [sr](float ms) { return dsp::decay60ForMs(ms, sr); };
        const float decayCoef = coef(p_[decay]);
        const float releaseCoef = coef(p_[release]);
        const float stealCoef = coef(static_cast<float>(stealSeconds * 1000.0));
        const float sustainLevel = std::clamp(p_[sustain], 0.0f, 1.0f);
        const auto declick = static_cast<float>(declickSeconds * sampleRate_);
        for (auto& v : voices_)
        {
            if (!v.active())
                continue;
            for (int i = from; i < to; ++i)
            {
                if (--v.countdown <= 0)
                {
                    control(v);
                    v.countdown = controlInterval;
                }
                const float env =
                    v.amp.step(attackStep, decayCoef, sustainLevel, v.stolen ? stealCoef : releaseCoef);
                if (!v.amp.active())
                    break;
                float l = 0.0f, r = 0.0f;
                readVoice(v, l, r);
                float g = env * v.gain;
                if (!region_.loop)
                {
                    // A short fade into the end point, so a sample cut mid-wave does not click.
                    const auto remaining = static_cast<float>((region_.end - v.position) / v.increment);
                    if (remaining < declick)
                        g *= std::max(0.0f, remaining / declick);
                }
                if (filterOn_)
                {
                    const auto svf = [&v](float x, float& s1, float& s2)
                    {
                        const float hp = (x - (v.k + v.g) * s1 - s2) * v.h;
                        const float bp = v.g * hp + s1;
                        const float lp = v.g * bp + s2;
                        s1 = v.g * hp + bp;
                        s2 = v.g * bp + lp;
                        return lp;
                    };
                    l = svf(l, v.s1L, v.s2L);
                    r = svf(r, v.s1R, v.s2R);
                }
                left[i] += l * g;
                right[i] += r * g;
                v.position += v.increment;
                if (region_.loop)
                {
                    if (v.position >= region_.loopEnd)
                        v.position = region_.loopStart +
                                     std::fmod(v.position - region_.loopStart, region_.loopLength());
                }
                else if (v.position >= region_.end)
                {
                    v.amp = synth::Envelope{}; // the end of the sample
                    break;
                }
            }
            if (!v.active())
                v.note = -1;
        }
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const Slot* slot = samples_.acquire();
        const Sample* sample = slot != nullptr ? slot->sample.get() : nullptr;
        if (sample != playing_)
        {
            // Another sample: what played the old one stops.
            for (auto& v : voices_)
                v = Voice{};
            playing_ = sample;
        }
        for (int i = 0; i < count; ++i)
            p_[static_cast<size_t>(i)] = parameter(i);
        std::fill(left, left + numSamples, 0.0f);
        std::fill(right, right + numSamples, 0.0f);
        const bool ready = playing_ != nullptr && playing_->frames() > 0;
        if (ready)
            region_ = regionFor(p_.data(), *playing_);
        filterOn_ = p_[cutoff] < 19500.0f || p_[filterEnv] != 0.0f;

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        int cursor = 0;
        while (cursor < numSamples)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= cursor)
                handle((*events)[next++]);
            int until = numSamples;
            if (events != nullptr && next < events->size())
                until = std::clamp((*events)[next].sampleOffset - chunkStart_, cursor + 1, numSamples);
            if (ready)
                render(left, right, cursor, until);
            cursor = until;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);

        const float outGain = dbToGain(p_[level]);
        if (outGain != 1.0f)
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] *= outGain;
                right[i] *= outGain;
            }
        int sounding = 0;
        for (int i = 0; i < voicePool; ++i)
        {
            const auto& v = voices_[static_cast<size_t>(i)];
            const bool on = v.active() && v.note >= 0;
            sounding += on && !v.stolen ? 1 : 0;
            positions_[static_cast<size_t>(i)].store(v.position, std::memory_order_relaxed);
            published_[static_cast<size_t>(i)].store(
                on ? (v.note | (v.stolen || v.amp.stage == synth::Stage::release ? 0x100 : 0)) : -1,
                std::memory_order_relaxed);
        }
        blockReduction_ = static_cast<float>(sounding); // for the editor: the voices sounding
    }

    Loader loader_;
    std::string state_, error_;
    std::shared_ptr<const Sample> controllerSample_;
    rt::SnapshotPublisher<Slot> samples_;

    // Real-time thread only.
    const Sample* playing_ = nullptr;
    std::array<Voice, voicePool> voices_{};
    std::array<float, count> p_{};
    Region region_;
    bool filterOn_ = false;
    std::uint32_t age_ = 0;
    int lastNote_ = -1;

    // Published for the editor.
    std::array<std::atomic<double>, voicePool> positions_{};
    std::array<std::atomic<int>, voicePool> published_{};
};

const Sampler* asSampler(const engine::InsertProcessor& p)
{
    return p.typeName() == type ? dynamic_cast<const Sampler*>(&p) : nullptr;
}

Sampler* asSampler(engine::InsertProcessor& p)
{
    return p.typeName() == type ? dynamic_cast<Sampler*>(&p) : nullptr;
}

constexpr char base64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::unique_ptr<engine::InsertProcessor> create(Loader loader)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("sampler::create");
    return std::make_unique<Sampler>(std::move(loader));
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    std::vector<engine::ParameterInfo> p(count);
    p[root] = {"Root", "", 0.0f, 127.0f, 60.0f, false, {}};
    p[keyLow] = {"Key low", "", 0.0f, 127.0f, 0.0f, false, {}};
    p[keyHigh] = {"Key high", "", 0.0f, 127.0f, 127.0f, false, {}};
    p[tune] = {"Tune", "st", -24.0f, 24.0f, 0.0f, false, {}};
    p[fine] = {"Fine", "ct", -100.0f, 100.0f, 0.0f, false, {}};
    p[velocity] = {"Velocity", "", 0.0f, 1.0f, 0.7f, false, {}};
    p[attack] = {"Attack", "ms", 0.5f, 5000.0f, 1.0f, true, {}};
    p[decay] = {"Decay", "ms", 1.0f, 10000.0f, 500.0f, true, {}};
    p[sustain] = {"Sustain", "", 0.0f, 1.0f, 1.0f, false, {}};
    p[release] = {"Release", "ms", 1.0f, 10000.0f, 200.0f, true, {}};
    p[start] = {"Start", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[end] = {"End", "", 0.0f, 1.0f, 1.0f, false, {}};
    p[loop] = {"Loop", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}};
    p[loopStart] = {"Loop start", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[loopEnd] = {"Loop end", "", 0.0f, 1.0f, 1.0f, false, {}};
    p[loopFade] = {"Loop fade", "ms", 0.0f, 500.0f, 10.0f, false, {}};
    p[oneShot] = {"One-shot", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}};
    p[cutoff] = {"Cutoff", "Hz", 20.0f, 20000.0f, 20000.0f, true, {}};
    p[resonance] = {"Resonance", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[filterEnv] = {"Filter env", "oct", -8.0f, 8.0f, 0.0f, false, {}};
    p[voices] = {"Voices", "", 1.0f, static_cast<float>(maxVoices), 16.0f, false, {}};
    p[glide] = {"Glide", "ms", 0.0f, 2000.0f, 0.0f, false, {}};
    p[level] = {"Level", "dB", -60.0f, 6.0f, 0.0f, false, {}};
    return p;
}

std::string encodeState(const std::string& path)
{
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : path)
    {
        buffer = (buffer << 8) | static_cast<unsigned char>(ch);
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            out += base64Alphabet[(buffer >> bits) & 0x3f];
        }
    }
    if (bits > 0)
        out += base64Alphabet[(buffer << (6 - bits)) & 0x3f];
    while (out.size() % 4 != 0)
        out += '=';
    return out;
}

std::string decodeState(const std::string& state)
{
    std::string out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : state)
    {
        const char* found = std::char_traits<char>::find(base64Alphabet, 64, ch);
        if (found == nullptr)
            continue; // padding (or anything else) carries no bits
        buffer = (buffer << 6) | static_cast<std::uint32_t>(found - base64Alphabet);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    return out;
}

std::shared_ptr<const Sample> currentSample(const engine::InsertProcessor& processor)
{
    const auto* s = asSampler(processor);
    return s != nullptr ? s->sample() : nullptr;
}

std::string loadError(const engine::InsertProcessor& processor)
{
    const auto* s = asSampler(processor);
    return s != nullptr ? s->error() : std::string{};
}

void setSample(engine::InsertProcessor& processor, std::shared_ptr<const Sample> sample)
{
    if (auto* s = asSampler(processor))
        s->publish(std::move(sample));
}

std::vector<VoiceInfo> soundingVoices(const engine::InsertProcessor& processor)
{
    const auto* s = asSampler(processor);
    return s != nullptr ? s->sounding() : std::vector<VoiceInfo>{};
}

} // namespace northlight::plugins::sampler
