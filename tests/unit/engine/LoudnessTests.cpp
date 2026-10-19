#include "engine/Loudness.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <numbers>
#include <vector>

using aural::engine::LoudnessMeter;
using Catch::Approx;

namespace
{
void feedSine(LoudnessMeter& meter, double hz, float amplitude, double seconds, double rate = 48000.0)
{
    const int n = static_cast<int>(rate * seconds);
    std::vector<float> l(256), r(256);
    for (int done = 0; done < n; done += 256)
    {
        const int count = std::min(256, n - done);
        for (int i = 0; i < count; ++i)
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * (done + i) / rate));
        meter.processBlock(l.data(), r.data(), count);
    }
}
} // namespace

TEST_CASE("A 997 Hz stereo sine at -23 dBFS reads -23 LUFS within 0.1 LU", "[engine][loudness]")
{
    // BS.1770 is calibrated so a full-scale 997 Hz sine in one channel reads
    // -3.01 LUFS; on both channels that is 0 LUFS, so -23 dBFS on both is -23 LUFS.
    LoudnessMeter meter;
    meter.prepare(48000.0);
    const float amplitude = std::pow(10.0f, -23.0f / 20.0f);
    feedSine(meter, 997.0, amplitude, 6.0);
    const auto r = meter.reading();
    CHECK(r.momentaryLufs == Approx(-23.0f).margin(0.1f));
    CHECK(r.shortTermLufs == Approx(-23.0f).margin(0.1f));
    CHECK(r.integratedLufs == Approx(-23.0f).margin(0.1f));
}

TEST_CASE("Integrated loudness gates out silence and quiet passages", "[engine][loudness]")
{
    LoudnessMeter meter;
    meter.prepare(48000.0);
    const float loud = std::pow(10.0f, -23.0f / 20.0f);
    feedSine(meter, 997.0, loud, 3.0);
    feedSine(meter, 997.0, 0.0f, 3.0);        // digital silence: under the absolute gate
    feedSine(meter, 997.0, loud * 0.1f, 3.0); // -43 LUFS: more than 10 LU under, gated out
    feedSine(meter, 997.0, loud, 3.0);
    // Windows straddling the loud-to-quiet edges pass the gates and pull the mean a
    // little under the loud level, as the standard intends.
    CHECK(meter.reading().integratedLufs == Approx(-23.0f).margin(0.3f));
    meter.resetIntegrated();
    feedSine(meter, 997.0, loud * 0.5f, 3.0); // the next hop clears the blocks
    CHECK(meter.reading().integratedLufs == Approx(-29.0f).margin(0.2f));
}

TEST_CASE("K-weighting lifts high frequencies and drops lows", "[engine][loudness]")
{
    LoudnessMeter high;
    high.prepare(48000.0);
    feedSine(high, 8000.0, 0.1f, 4.0);
    LoudnessMeter low;
    low.prepare(48000.0);
    feedSine(low, 40.0, 0.1f, 4.0);
    CHECK(high.reading().momentaryLufs > -18.0f); // shelf adds ~+4 dB
    CHECK(low.reading().momentaryLufs < -22.0f);  // the RLB high-pass takes some away
}

TEST_CASE("Silence reads as minus infinity and short-term lags momentary", "[engine][loudness]")
{
    LoudnessMeter meter;
    meter.prepare(48000.0);
    CHECK(meter.reading().momentaryLufs == -100.0f);
    feedSine(meter, 1000.0, 0.1f, 0.5);
    const auto after = meter.reading();
    CHECK(after.momentaryLufs == Approx(-20.0f).margin(0.5f));
    CHECK(after.shortTermLufs < after.momentaryLufs - 3.0f); // only 0.5 s of its 3 s window is loud
}

#if defined(AURAL_RT_CHECKS)
namespace
{
std::atomic<int> loudnessViolations{0};
void countLoudnessViolation(const aural::rt::Violation&) noexcept
{
    loudnessViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Loudness processing does not allocate on a real-time thread", "[engine][loudness][rt]")
{
    LoudnessMeter meter;
    meter.prepare(48000.0);
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 256; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / 48000.0));
    const auto previous = aural::rt::setViolationHandler(&countLoudnessViolation);
    loudnessViolations = 0;
    {
        aural::rt::ScopedRealtimeSection section;
        for (int block = 0; block < 200; ++block)
            meter.processBlock(l.data(), r.data(), 256);
    }
    aural::rt::setViolationHandler(previous);
    CHECK(loudnessViolations == 0);
}
#endif
