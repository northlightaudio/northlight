#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;

// Feeds a sine of `hz` at `amplitude` through the processor and returns the output
// RMS over the last second (after settling), in dB relative to the input RMS.
float responseDb(engine::InsertProcessor& p, double hz, float amplitude = 0.5f, int seconds = 2)
{
    const int n = static_cast<int>(rate) * seconds;
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
    for (int done = 0; done < n; done += 256)
        p.process(l.data() + done, r.data() + done, std::min(256, n - done));
    double sum = 0.0;
    const int from = n - static_cast<int>(rate);
    for (int i = from; i < n; ++i)
        sum += static_cast<double>(l[static_cast<size_t>(i)]) * l[static_cast<size_t>(i)];
    const double rms = std::sqrt(sum / rate);
    const double inputRms = amplitude / std::numbers::sqrt2;
    return static_cast<float>(20.0 * std::log10(rms / inputRms));
}
} // namespace

TEST_CASE("The factory creates every built-in with its default parameters", "[plugins]")
{
    plugins::BuiltInFactory factory;
    for (const auto& type : factory.typeNames())
    {
        auto p = factory.create(type);
        REQUIRE(p != nullptr);
        CHECK(p->typeName() == type);
        CHECK_FALSE(p->parameters().empty());
        CHECK(plugins::defaultParameters(type).size() == p->parameters().size());
        CHECK_FALSE(plugins::BuiltInFactory::displayName(type).empty());
    }
    CHECK(factory.create("nope") == nullptr);
}

TEST_CASE("Gain applies its dB value, ramped, and bypass restores unity", "[plugins][gain]")
{
    plugins::BuiltInFactory factory;
    auto gain = factory.create(plugins::types::gain);
    gain->prepare(rate, 256);
    gain->setParameter(0, -6.0f);
    CHECK(responseDb(*gain, 1000.0) == Approx(-6.0f).margin(0.05f));
    gain->setBypassed(true);
    CHECK(responseDb(*gain, 1000.0) == Approx(0.0f).margin(0.05f));
    gain->setBypassed(false);
    gain->setParameter(0, 100.0f); // clamped to the maximum
    CHECK(gain->parameter(0) == 24.0f);
}

TEST_CASE("EQ bands boost and cut where they should and leave the rest alone", "[plugins][eq]")
{
    plugins::BuiltInFactory factory;
    auto eq = factory.create(plugins::types::eq);
    eq->prepare(rate, 256);

    // Flat by default.
    CHECK(responseDb(*eq, 1000.0) == Approx(0.0f).margin(0.1f));

    // Mid peak +6 dB at 1 kHz, Q 1: full boost at the centre, little at 100 Hz.
    eq->setParameter(3, 1000.0f);
    eq->setParameter(4, 6.0f);
    eq->setParameter(5, 1.0f);
    CHECK(responseDb(*eq, 1000.0) == Approx(6.0f).margin(0.2f));
    CHECK(responseDb(*eq, 100.0) == Approx(0.0f).margin(0.5f));
    eq->setParameter(4, 0.0f);

    // Low shelf -12 dB at 120 Hz: 40 Hz well down, 5 kHz untouched.
    eq->setParameter(1, 120.0f);
    eq->setParameter(2, -12.0f);
    CHECK(responseDb(*eq, 40.0) == Approx(-12.0f).margin(0.8f));
    CHECK(responseDb(*eq, 5000.0) == Approx(0.0f).margin(0.2f));
    eq->setParameter(2, 0.0f);

    // High shelf +6 dB at 8 kHz: 15 kHz up, 200 Hz untouched.
    eq->setParameter(6, 8000.0f);
    eq->setParameter(7, 6.0f);
    CHECK(responseDb(*eq, 15000.0) == Approx(6.0f).margin(1.0f));
    CHECK(responseDb(*eq, 200.0) == Approx(0.0f).margin(0.2f));
    eq->setParameter(7, 0.0f);

    // High-pass at 200 Hz: 50 Hz is down by more than 20 dB, 2 kHz untouched.
    eq->setParameter(0, 200.0f);
    CHECK(responseDb(*eq, 50.0) < -20.0f);
    CHECK(responseDb(*eq, 2000.0) == Approx(0.0f).margin(0.3f));
}

TEST_CASE("The compressor reduces gain above the threshold by the ratio", "[plugins][compressor]")
{
    plugins::BuiltInFactory factory;
    auto comp = factory.create(plugins::types::compressor);
    comp->prepare(rate, 256);
    comp->setParameter(0, -20.0f); // threshold
    comp->setParameter(1, 4.0f);   // ratio
    comp->setParameter(2, 1.0f);   // attack ms
    comp->setParameter(3, 50.0f);  // release ms
    comp->setParameter(4, 0.0f);   // hard knee
    comp->setParameter(5, 0.0f);   // no makeup

    // A 0.5 peak sine is -6 dBFS peak: 14 dB over the threshold -> 10.5 dB of static
    // reduction, less about a decibel because the peak detector's envelope dips
    // between the cycles of a 1 kHz tone with a 50 ms release.
    CHECK(responseDb(*comp, 1000.0, 0.5f) == Approx(-10.0f).margin(1.2f));
    // Below the threshold nothing happens.
    CHECK(responseDb(*comp, 1000.0, 0.05f) == Approx(0.0f).margin(0.1f));
    // Makeup adds back.
    comp->setParameter(5, 6.0f);
    CHECK(responseDb(*comp, 1000.0, 0.5f) == Approx(-4.0f).margin(1.2f));
}

TEST_CASE("The limiter never lets a sample over the ceiling", "[plugins][limiter]")
{
    plugins::BuiltInFactory factory;
    auto lim = factory.create(plugins::types::limiter);
    lim->prepare(rate, 256);
    lim->setParameter(0, -6.0f); // ceiling
    lim->setParameter(1, 20.0f);
    const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
    std::vector<float> l(4800), r(4800);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = 1.5f *
               static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / rate));
        r[i] = i % 1000 == 0 ? 3.0f : 0.1f; // spikes
    }
    lim->process(l.data(), r.data(), 4800);
    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i)
        peak = std::max({peak, std::abs(l[i]), std::abs(r[i])});
    CHECK(peak <= ceiling + 1e-6f);
    CHECK(peak > ceiling * 0.9f); // it is actually working near the ceiling, not just attenuating
}

TEST_CASE("The limiter's lookahead delays the signal exactly and reports it as latency", "[plugins][limiter]")
{
    plugins::BuiltInFactory factory;
    auto lim = factory.create(plugins::types::limiter);
    lim->setParameter(2, 1.5f); // ms
    lim->prepare(rate, 256);
    CHECK(lim->latencySamples() == 72); // 1.5 ms at 48 kHz
    // Quiet audio passes untouched, just late by the lookahead.
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    l[10] = 0.25f;
    r[10] = -0.25f;
    lim->process(l.data(), r.data(), 256);
    CHECK(l[10] == 0.0f);
    CHECK(l[82] == Approx(0.25f));
    CHECK(r[82] == Approx(-0.25f));
    lim->setParameter(2, 0.0f);
    CHECK(lim->latencySamples() == 0);
}

TEST_CASE("With lookahead a sudden peak is already attenuated when it arrives", "[plugins][limiter]")
{
    plugins::BuiltInFactory factory;
    auto lim = factory.create(plugins::types::limiter);
    lim->setParameter(0, -6.0f);
    lim->setParameter(1, 5.0f); // a short release so the tail recovers inside the buffer
    lim->setParameter(2, 2.0f); // 96 samples
    lim->prepare(rate, 512);
    const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
    std::vector<float> l(2048, 0.1f), r(2048, 0.1f);
    for (size_t i = 1000; i < 1200; ++i)
        l[i] = r[i] = 1.0f; // a loud burst
    lim->process(l.data(), r.data(), 2048);
    float peak = 0.0f;
    for (size_t i = 0; i < l.size(); ++i)
        peak = std::max({peak, std::abs(l[i]), std::abs(r[i])});
    CHECK(peak <= ceiling + 1e-6f);
    // The burst leaves the delay at 1096; the gain started falling before that,
    // so the sample just before the burst is already a little quieter than 0.1.
    CHECK(l[1095] < 0.1f);
    CHECK(l[1095] > 0.05f);
    // Inside the burst the output sits right at the ceiling, not far below it.
    CHECK(l[1150] == Approx(ceiling).margin(0.01f));
    // Well after it, the release has brought the quiet part back to 0.1.
    CHECK(l[2000] == Approx(0.1f).margin(0.01f));
}

#if defined(AURAL_RT_CHECKS)
namespace
{
std::atomic<int> pluginViolations{0};
void countPluginViolation(const rt::Violation&) noexcept
{
    pluginViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Built-in processing does not allocate on a real-time thread", "[plugins][rt]")
{
    plugins::BuiltInFactory factory;
    std::vector<std::unique_ptr<engine::InsertProcessor>> all;
    for (const auto& type : factory.typeNames())
    {
        all.push_back(factory.create(type));
        all.back()->prepare(rate, 256);
    }
    std::vector<float> l(256, 0.3f), r(256, -0.3f);
    const auto previous = rt::setViolationHandler(&countPluginViolation);
    pluginViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int block = 0; block < 50; ++block)
            for (auto& p : all)
            {
                if (block == 25)
                    p->setParameter(0, p->parameter(0) * 0.5f); // a parameter change mid-stream
                p->process(l.data(), r.data(), 256);
            }
    }
    rt::setViolationHandler(previous);
    CHECK(pluginViolations == 0);
}
#endif

namespace
{
// An impulse through a processor; the output (left, right) for `seconds`.
std::pair<std::vector<float>, std::vector<float>> impulseResponse(engine::InsertProcessor& p, double seconds)
{
    const int n = static_cast<int>(rate * seconds);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    l[0] = r[0] = 1.0f;
    for (int done = 0; done < n; done += 256)
        p.process(l.data() + done, r.data() + done, std::min(256, n - done));
    return {l, r};
}

double energy(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return sum;
}
} // namespace

TEST_CASE("Reverb: a tail that decays at the decay time, stays finite and is stereo", "[plugins][reverb]")
{
    plugins::BuiltInFactory factory;
    auto verb = factory.create(plugins::types::reverb);
    REQUIRE(verb != nullptr);
    verb->prepare(rate, 256);
    verb->setParameter(plugins::reverb::mix, 100.0f);
    verb->setParameter(plugins::reverb::preDelay, 0.0f);
    verb->setParameter(plugins::reverb::decay, 1.0f);
    verb->setParameter(plugins::reverb::damping, 20000.0f);
    const auto [l, r] = impulseResponse(*verb, 2.5);
    for (const float v : l)
        REQUIRE(std::isfinite(v));
    // Energy in 100 ms windows half a second apart falls by about 30 dB (60 dB per second).
    const auto window = static_cast<size_t>(rate * 0.1);
    const double early =
        energy(l, static_cast<size_t>(rate * 0.3), static_cast<size_t>(rate * 0.3) + window) +
        energy(r, static_cast<size_t>(rate * 0.3), static_cast<size_t>(rate * 0.3) + window);
    const double late = energy(l, static_cast<size_t>(rate * 0.8), static_cast<size_t>(rate * 0.8) + window) +
                        energy(r, static_cast<size_t>(rate * 0.8), static_cast<size_t>(rate * 0.8) + window);
    REQUIRE(late > 0.0);
    CHECK(10.0 * std::log10(early / late) == Approx(30.0).margin(8.0));
    // Left and right differ (a stereo tail).
    double difference = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
        difference += std::abs(l[i] - r[i]);
    CHECK(difference > 1.0);
    // The longest, brightest, largest setting still decays (no runaway).
    verb->setParameter(plugins::reverb::decay, 12.0f);
    verb->setParameter(plugins::reverb::size, 100.0f);
    const auto [l2, r2] = impulseResponse(*verb, 6.0);
    double peakLate = 0.0;
    for (size_t i = l2.size() - 4800; i < l2.size(); ++i)
        peakLate = std::max(peakLate, static_cast<double>(std::abs(l2[i])));
    CHECK(peakLate < 0.5);
    // Mix 0 is dry.
    verb->setParameter(plugins::reverb::mix, 0.0f);
    CHECK(responseDb(*verb, 1000.0) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Delay: repeats at the time, falling by the feedback, crossing sides in ping-pong",
          "[plugins][delay]")
{
    plugins::BuiltInFactory factory;
    auto delay = factory.create(plugins::types::delay);
    REQUIRE(delay != nullptr);
    delay->setParameter(plugins::delay::time, 100.0f);
    delay->setParameter(plugins::delay::feedback, 50.0f);
    delay->setParameter(plugins::delay::tone, 20000.0f);
    delay->setParameter(plugins::delay::mix, 100.0f);
    delay->prepare(rate, 256); // the glide starts at the time
    const auto [l, r] = impulseResponse(*delay, 0.5);
    const auto at = [&](const std::vector<float>& x, double ms)
    {
        float peak = 0.0f;
        const auto centre = static_cast<int>(rate * ms * 0.001);
        for (int i = centre - 8; i <= centre + 8; ++i)
            peak = std::max(peak, std::abs(x[static_cast<size_t>(i)]));
        return peak;
    };
    CHECK(at(l, 50.0) < 0.01f); // nothing before the time
    const float first = at(l, 100.0), second = at(l, 200.0);
    CHECK(first > 0.5f);
    CHECK(second / first == Approx(0.5f).margin(0.1f));

    delay->setParameter(plugins::delay::pingPong, 1.0f);
    delay->prepare(rate, 256);
    const auto [pl, pr] = impulseResponse(*delay, 0.5);
    CHECK(at(pl, 100.0) > 0.2f);
    CHECK(at(pr, 100.0) < 0.01f);
    CHECK(at(pr, 200.0) > 0.1f); // the second repeat on the other side
    CHECK(at(pl, 200.0) < 0.01f);
}

TEST_CASE("Every built-in names each parameter with a unique, non-empty key", "[plugins][parameters]")
{
    // Saved projects and automation find parameters by these keys: a duplicate
    // would send a value to the wrong control after a reorder.
    auto types = aural::plugins::BuiltInFactory().typeNames();
    for (const auto& t : aural::plugins::strip::moduleTypes())
        types.push_back(t);
    for (const auto& type : types)
    {
        INFO(type);
        const auto layout = aural::plugins::builtInParameterLayout(type);
        REQUIRE_FALSE(layout.keys.empty());
        CHECK(layout.keys.size() == layout.defaults.size());
        std::set<std::string> seen;
        for (const auto& key : layout.keys)
        {
            INFO(key);
            CHECK_FALSE(key.empty());
            CHECK(seen.insert(key).second);
        }
    }
}
