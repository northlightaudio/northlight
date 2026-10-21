#include "core/Command.h"
#include "engine/Insert.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Sampler.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace aural;
using Catch::Approx;
namespace sp = plugins::sampler;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::shared_ptr<const sp::Sample>
sineSample(double hz, double seconds, double sampleRate = rate, float amp = 0.5f)
{
    auto s = std::make_shared<sp::Sample>();
    s->path = "sine.wav";
    s->sampleRate = sampleRate;
    const auto n = static_cast<size_t>(seconds * sampleRate);
    s->left.resize(n);
    for (size_t i = 0; i < n; ++i)
        s->left[i] = amp * static_cast<float>(
                               std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate));
    return s;
}

std::shared_ptr<const sp::Sample> constantSample(float value, double seconds)
{
    auto s = std::make_shared<sp::Sample>();
    s->path = "dc.wav";
    s->sampleRate = rate;
    s->left.assign(static_cast<size_t>(seconds * rate), value);
    return s;
}

// A sampler with the sample, its velocity off (full level) and the given values.
std::unique_ptr<engine::InsertProcessor> makeSampler(std::shared_ptr<const sp::Sample> sample)
{
    auto p = sp::create();
    p->prepare(rate, block);
    p->setParameter(sp::velocity, 0.0f);
    sp::setSample(*p, std::move(sample));
    return p;
}

struct Note
{
    size_t at; // sample
    int note;
    bool on;
};

// Renders `seconds` of the processor's output (left) with the notes at their samples.
std::vector<float> render(engine::InsertProcessor& p,
                          double seconds,
                          std::vector<Note> notes,
                          std::vector<float>* right = nullptr)
{
    const int n = static_cast<int>(seconds * rate);
    std::vector<float> out(static_cast<size_t>(n)), outR(static_cast<size_t>(n));
    engine::MidiEventList events(64);
    for (int done = 0; done < n; done += block)
    {
        const int count = std::min(block, n - done);
        events.clear();
        for (const auto& note : notes)
        {
            const auto when = static_cast<int>(note.at);
            if (when >= done && when < done + count)
            {
                if (note.on)
                    events.noteOn(when - done, note.note, 100);
                else
                    events.noteOff(when - done, note.note);
            }
        }
        p.processMidi(events, out.data() + done, outR.data() + done, count);
    }
    if (right != nullptr)
        *right = outR;
    return out;
}

// The frequency of a signal over [from, to) by its rising zero crossings.
double frequency(const std::vector<float>& x, size_t from, size_t to)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double exact = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = exact;
            else
                ++crossings;
            last = exact;
        }
    return crossings > 0 ? crossings * rate / (last - first) : 0.0;
}

float peak(const std::vector<float>& x, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < to && i < x.size(); ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

float maxStep(const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t>(from, 1); i < to && i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

size_t at(double seconds)
{
    return static_cast<size_t>(seconds * rate);
}
} // namespace

TEST_CASE("Sampler: a sine at the root plays an octave up at the octave above", "[plugins][sampler]")
{
    const double c4 = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
    auto p = makeSampler(sineSample(c4, 2.0));
    p->setParameter(sp::root, 60.0f);

    const auto root = render(*p, 0.5, {{0, 60, true}, {at(0.4), 60, false}});
    CHECK(frequency(root, at(0.05), at(0.35)) == Approx(c4).epsilon(0.002));

    auto q = makeSampler(sineSample(c4, 2.0));
    const auto octave = render(*q, 0.5, {{0, 72, true}});
    CHECK(frequency(octave, at(0.05), at(0.45)) == Approx(2.0 * c4).epsilon(0.002));

    // Tune and fine move it: +7 st and +50 ct.
    auto t = makeSampler(sineSample(c4, 2.0));
    t->setParameter(sp::tune, 7.0f);
    t->setParameter(sp::fine, 50.0f);
    const auto tuned = render(*t, 0.5, {{0, 60, true}});
    CHECK(frequency(tuned, at(0.05), at(0.45)) == Approx(c4 * std::pow(2.0, 7.5 / 12.0)).epsilon(0.002));
}

TEST_CASE("Sampler: a sample at another rate plays at its own pitch", "[plugins][sampler]")
{
    auto p = makeSampler(sineSample(440.0, 2.0, 44100.0));
    p->setParameter(sp::root, 69.0f);
    const auto out = render(*p, 0.5, {{0, 69, true}});
    CHECK(frequency(out, at(0.05), at(0.45)) == Approx(440.0).epsilon(0.002));
}

TEST_CASE("Sampler: the envelope attacks, decays to the sustain and releases", "[plugins][sampler]")
{
    auto p = makeSampler(constantSample(0.5f, 3.0));
    p->setParameter(sp::attack, 100.0f);
    p->setParameter(sp::decay, 50.0f);
    p->setParameter(sp::sustain, 0.5f);
    p->setParameter(sp::release, 100.0f);
    const auto out = render(*p, 1.0, {{0, 60, true}, {at(0.5), 60, false}});
    CHECK(out[at(0.05)] == Approx(0.25f).margin(0.01f));  // half way up the attack
    CHECK(out[at(0.0995)] == Approx(0.5f).margin(0.01f)); // the top
    CHECK(out[at(0.4)] == Approx(0.25f).margin(0.005f));  // the sustain
    // The release falls 60 dB in its time, then the voice ends.
    CHECK(out[at(0.55)] < 0.25f * 0.1f);
    CHECK(peak(out, at(0.62), at(1.0)) < 0.25f * 0.001f);
    CHECK(sp::soundingVoices(*p).empty());
}

TEST_CASE("Sampler: a loop wraps without a click and plays on past the sample's end", "[plugins][sampler]")
{
    // 440 Hz with a loop whose length is not a whole number of cycles: the
    // wrap lands mid-wave, which only the crossfade hides.
    const auto sample = sineSample(440.0, 1.0);
    const auto loopRun = [&](float fadeMs)
    {
        auto p = makeSampler(sample);
        p->setParameter(sp::loop, 1.0f);
        p->setParameter(sp::loopStart, 0.3f);
        p->setParameter(sp::loopEnd, 0.3f + 0.2037f);
        p->setParameter(sp::loopFade, fadeMs);
        return render(*p, 2.0, {{0, 60, true}});
    };
    const float sineStep = 0.5f * static_cast<float>(2.0 * std::numbers::pi * 440.0 / rate);
    const auto faded = loopRun(20.0f);
    CHECK(maxStep(faded, 1, faded.size()) < sineStep * 1.2f);
    CHECK(peak(faded, at(1.5), at(2.0)) > 0.3f); // still playing after the sample's end
    // Without the fade the same wrap jumps.
    const auto hard = loopRun(0.0f);
    CHECK(maxStep(hard, 1, hard.size()) > sineStep * 3.0f);
}

TEST_CASE("Sampler: without a loop a note stops at the end point", "[plugins][sampler]")
{
    auto p = makeSampler(constantSample(0.5f, 1.0));
    p->setParameter(sp::start, 0.25f);
    p->setParameter(sp::end, 0.5f);
    const auto out = render(*p, 0.5, {{0, 60, true}});
    CHECK(out[at(0.1)] == Approx(0.5f).margin(0.01f));
    CHECK(peak(out, at(0.251), at(0.5)) == 0.0f); // a quarter of a second of sample
    CHECK(sp::soundingVoices(*p).empty());
}

TEST_CASE("Sampler: one-shot plays to the end and ignores the note-off", "[plugins][sampler]")
{
    auto p = makeSampler(constantSample(0.5f, 0.5));
    p->setParameter(sp::oneShot, 1.0f);
    p->setParameter(sp::release, 10.0f);
    const auto out = render(*p, 1.0, {{0, 60, true}, {at(0.05), 60, false}});
    CHECK(out[at(0.3)] == Approx(0.5f).margin(0.01f));
    CHECK(peak(out, at(0.51), at(1.0)) == 0.0f);

    // Without one-shot the same note-off releases it.
    auto q = makeSampler(constantSample(0.5f, 0.5));
    q->setParameter(sp::release, 10.0f);
    const auto released = render(*q, 0.5, {{0, 60, true}, {at(0.05), 60, false}});
    CHECK(peak(released, at(0.15), at(0.5)) < 0.001f);
}

TEST_CASE("Sampler: over the polyphony limit the oldest voice is stolen", "[plugins][sampler]")
{
    auto p = makeSampler(constantSample(0.5f, 3.0));
    p->setParameter(sp::voices, 2.0f);
    render(*p, 0.1, {{0, 60, true}, {100, 62, true}, {200, 64, true}});
    std::vector<int> notes;
    for (const auto& v : sp::soundingVoices(*p))
        notes.push_back(v.note);
    std::sort(notes.begin(), notes.end());
    CHECK(notes == std::vector<int>{62, 64});
}

TEST_CASE("Sampler: notes outside the key range are ignored", "[plugins][sampler]")
{
    auto p = makeSampler(constantSample(0.5f, 1.0));
    p->setParameter(sp::keyLow, 48.0f);
    p->setParameter(sp::keyHigh, 72.0f);
    const auto out = render(*p, 0.2, {{0, 40, true}, {0, 80, true}});
    CHECK(peak(out, 0, out.size()) == 0.0f);
    const auto in = render(*p, 0.2, {{0, 50, true}});
    CHECK(peak(in, 0, in.size()) > 0.4f);
}

TEST_CASE("Sampler: the low-pass takes off the highs", "[plugins][sampler]")
{
    const auto sample = sineSample(5000.0, 1.0);
    auto open = makeSampler(sample);
    auto closed = makeSampler(sample);
    closed->setParameter(sp::cutoff, 300.0f);
    const auto a = render(*open, 0.5, {{0, 60, true}});
    const auto b = render(*closed, 0.5, {{0, 60, true}});
    CHECK(peak(b, at(0.2), at(0.5)) < peak(a, at(0.2), at(0.5)) * 0.05f);
}

TEST_CASE("Sampler: glide slides from the last note's pitch", "[plugins][sampler]")
{
    const double c4 = 440.0 * std::pow(2.0, (60 - 69) / 12.0);
    auto p = makeSampler(sineSample(c4, 3.0));
    p->setParameter(sp::glide, 200.0f);
    const auto out = render(*p, 1.0, {{0, 60, true}, {at(0.3), 60, false}, {at(0.3), 72, true}});
    CHECK(frequency(out, at(0.32), at(0.4)) < 2.0 * c4 * 0.95); // still on its way
    CHECK(frequency(out, at(0.6), at(0.9)) == Approx(2.0 * c4).epsilon(0.003));
}

TEST_CASE("Sampler: the state holds the file and loads it through the loader", "[plugins][sampler]")
{
    const std::string path = "C:\\Samples\\Kick \xc3\xa9.wav";
    const auto state = sp::encodeState(path);
    CHECK(sp::decodeState(state) == path);
    CHECK(sp::encodeState({}).empty());

    std::vector<std::string> asked;
    plugins::BuiltInFactory factory(
        [&](const std::string& file, std::string& error) -> std::shared_ptr<const sp::Sample>
        {
            asked.push_back(file);
            if (file == "missing.wav")
            {
                error = "file not found";
                return nullptr;
            }
            return constantSample(0.5f, 0.5);
        });
    auto p = factory.create(sp::type);
    REQUIRE(p != nullptr);
    CHECK(plugins::BuiltInFactory::isInstrument(sp::type));
    p->prepare(rate, block);
    p->setState(state);
    CHECK(asked == std::vector<std::string>{path});
    CHECK(p->state() == state);
    REQUIRE(sp::currentSample(*p) != nullptr);
    CHECK(sp::loadError(*p).empty());

    p->setState(sp::encodeState("missing.wav"));
    CHECK(sp::currentSample(*p) == nullptr);
    CHECK(sp::loadError(*p) == "file not found");
    CHECK(sp::decodeState(p->state()) == "missing.wav"); // kept, so a re-save loses nothing
    const auto out = render(*p, 0.1, {{0, 60, true}});
    CHECK(peak(out, 0, out.size()) == 0.0f);
}

TEST_CASE("A built-in's model state reaches its live instance when a command changes it",
          "[plugins][sampler]")
{
    std::vector<std::string> asked;
    plugins::BuiltInFactory factory(
        [&](const std::string& file, std::string&) -> std::shared_ptr<const sp::Sample>
        {
            asked.push_back(file);
            return constantSample(0.5f, 0.5);
        });
    engine::InsertCache cache(factory);
    cache.prepare(rate, block);

    core::Project project;
    core::Track channel;
    channel.kind = core::TrackKind::instrument;
    channel.instrument.type = sp::type;
    channel.instrument.params = plugins::defaultParameters(sp::type);
    channel.instrument.state = sp::encodeState("a.wav");
    core::AddTrack add(channel);
    add.apply(project);
    const auto& made = project.tracks.back();
    auto instance = cache.get(made.instrument);
    REQUIRE(instance != nullptr);
    CHECK(asked == std::vector<std::string>{"a.wav"});
    // An unchanged state is not reloaded on the next compile.
    cache.get(project.tracks.back().instrument);
    CHECK(asked.size() == 1);

    core::SetInsertState set(made.id, made.instrument.id, sp::encodeState("b.wav"), "Load sample");
    set.apply(project);
    CHECK(cache.get(project.tracks.back().instrument) == instance);
    CHECK(asked == std::vector<std::string>{"a.wav", "b.wav"});
    set.revert(project);
    cache.get(project.tracks.back().instrument);
    CHECK(asked.back() == "a.wav");
    CHECK(sp::decodeState(instance->state()) == "a.wav");
}

#ifdef AURAL_RT_CHECKS
namespace
{
std::atomic<int> samplerViolations{0};
void countSamplerViolation(const rt::Violation&) noexcept
{
    samplerViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Sampler: playing, looping, filtering and stealing do not allocate on a real-time thread",
          "[plugins][sampler][rt]")
{
    auto p = makeSampler(sineSample(220.0, 1.0));
    p->setParameter(sp::loop, 1.0f);
    p->setParameter(sp::loopStart, 0.2f);
    p->setParameter(sp::loopEnd, 0.7f);
    p->setParameter(sp::cutoff, 2000.0f);
    p->setParameter(sp::filterEnv, 2.0f);
    p->setParameter(sp::glide, 50.0f);
    p->setParameter(sp::voices, 4.0f);
    engine::MidiEventList events(64);
    std::vector<float> l(block), r(block);
    const auto previous = rt::setViolationHandler(&countSamplerViolation);
    samplerViolations = 0;
    for (int b = 0; b < 200; ++b)
    {
        if (b == 100)
            sp::setSample(*p, sineSample(330.0, 0.5)); // a new sample, swapped in between blocks
        events.clear();
        if (b % 10 == 0)
            events.noteOn(b % block, 48 + b % 24, 90);
        if (b % 10 == 5)
            events.noteOff(3, 48 + (b - 5) % 24);
        rt::ScopedRealtimeSection section;
        p->processMidi(events, l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(samplerViolations == 0);
}
#endif
