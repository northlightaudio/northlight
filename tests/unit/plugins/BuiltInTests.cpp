#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <numbers>
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
