#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Glue.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace glue = plugins::glue;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::unique_ptr<engine::InsertProcessor> makeGlue()
{
    auto p = plugins::BuiltInFactory().create(glue::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    return p;
}

// `seconds` of a sine (hz > 0) or a constant (hz == 0) at `amplitude` through the
// processor; the last block's output peak.
float play(engine::InsertProcessor& p, double hz, float amplitude, double seconds, double* phase = nullptr)
{
    std::vector<float> l(block), r(block);
    double local = 0.0;
    double& ph = phase != nullptr ? *phase : local;
    float peak = 0.0f;
    for (int done = 0; done < static_cast<int>(seconds * rate); done += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float v = hz > 0.0 ? amplitude * static_cast<float>(std::sin(ph)) : amplitude;
            ph += 2.0 * std::numbers::pi * hz / rate;
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = v;
        }
        p.process(l.data(), r.data(), block);
        peak = 0.0f;
        for (float v : l)
            peak = std::max(peak, std::abs(v));
    }
    return peak;
}
} // namespace

TEST_CASE("Glue's curve: nothing below the knee, the ratio above it, and a smooth knee between",
          "[plugins][glue]")
{
    CHECK(glue::gainReductionDb(-30.0f, -18.0f, 4.0f) == 0.0f);
    CHECK(glue::gainReductionDb(-6.0f, -18.0f, 4.0f) == Approx(12.0f * 0.75f));
    CHECK(glue::gainReductionDb(-6.0f, -18.0f, 2.0f) == Approx(6.0f));
    // The knee meets both straight parts.
    const float half = glue::kneeDb * 0.5f;
    CHECK(glue::gainReductionDb(-18.0f - half, -18.0f, 4.0f) == Approx(0.0f).margin(1e-5f));
    CHECK(glue::gainReductionDb(-18.0f + half, -18.0f, 4.0f) == Approx(half * 0.75f).margin(1e-4f));
    CHECK(glue::gainReductionDb(-18.0f, -18.0f, 4.0f) > 0.0f);

    // The switch positions.
    CHECK(glue::ratioFor(0.0f) == 1.5f);
    CHECK(glue::ratioFor(3.0f) == 10.0f);
    CHECK(glue::attackMsFor(4.0f) == 10.0f);
    CHECK(glue::releaseSecondsFor(1.0f) == 0.3f);
    CHECK(glue::isAutoRelease(static_cast<float>(glue::autoRelease)));
    CHECK_FALSE(glue::isAutoRelease(3.0f));
}

TEST_CASE("Glue settles where its curve says, and make-up and mix act after it", "[plugins][glue]")
{
    auto p = makeGlue();
    p->setParameter(glue::threshold, -18.0f);
    p->setParameter(glue::ratio, 2.0f); // 4:1
    p->setParameter(glue::attack, 0.0f);
    p->setParameter(glue::release, 0.0f);
    const float out = play(*p, 0.0, 0.5f, 0.5); // -6 dBFS steady
    const float expected = 12.0f * 0.75f;       // 9 dB
    CHECK(p->feedback()->latest().reduction == Approx(expected).margin(0.05f));
    CHECK(out == Approx(0.5f * std::pow(10.0f, -expected / 20.0f)).margin(1e-3f));
    // The detectors the card reads: RMS of a constant is the constant.
    CHECK(p->feedback()->latest().extra == Approx(0.5f).margin(0.01f));
    CHECK(p->feedback()->latest().extra2 == Approx(out).margin(0.01f));

    p->setParameter(glue::makeup, 9.0f);
    CHECK(play(*p, 0.0, 0.5f, 0.1) == Approx(0.5f).margin(2e-3f));

    // All dry: the input passes untouched.
    p->setParameter(glue::mix, 0.0f);
    CHECK(play(*p, 0.0, 0.5f, 0.1) == Approx(0.5f).margin(1e-6f));
}

TEST_CASE("Glue's Auto release recovers fast from a hit and slowly from held compression", "[plugins][glue]")
{
    // The reduction 250 ms after the signal drops below the threshold.
    const auto after = [](double holdSeconds)
    {
        auto p = makeGlue();
        p->setParameter(glue::threshold, -30.0f);
        p->setParameter(glue::ratio, 3.0f);
        p->setParameter(glue::attack, 0.0f);
        p->setParameter(glue::release, static_cast<float>(glue::autoRelease));
        play(*p, 0.0, 0.5f, holdSeconds);
        const float during = p->feedback()->latest().reduction;
        play(*p, 0.0, 0.001f, 0.25);
        return std::pair{during, p->feedback()->latest().reduction};
    };
    const auto [hitDuring, hitAfter] = after(0.03);
    const auto [heldDuring, heldAfter] = after(3.0);
    CHECK(hitDuring > 15.0f);
    CHECK(heldDuring > 15.0f);
    CHECK(hitAfter < 0.15f * hitDuring);
    CHECK(heldAfter > 0.5f * heldDuring);
}

TEST_CASE("Glue's key high-pass keeps the low end from driving it", "[plugins][glue]")
{
    const auto reductionAt = [](float keyHz)
    {
        auto p = makeGlue();
        p->setParameter(glue::threshold, -24.0f);
        p->setParameter(glue::keyHighPass, keyHz);
        play(*p, 40.0, 0.5f, 1.0);
        return p->feedback()->latest().reduction;
    };
    const float open = reductionAt(glue::keyHighPassOffHz);
    const float filtered = reductionAt(300.0f);
    CHECK(open > 6.0f);
    CHECK(filtered < 0.3f * open);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> glueViolations{0};
void countGlueViolation(const rt::Violation&) noexcept
{
    glueViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Glue processes without allocating on the real-time thread", "[plugins][glue][rt]")
{
    auto p = makeGlue();
    p->setParameter(glue::keyHighPass, 120.0f);
    p->setParameter(glue::release, static_cast<float>(glue::autoRelease));
    p->setParameter(glue::mix, 60.0f);
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countGlueViolation);
    glueViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 100; ++i)
            p->process(l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(glueViolations == 0);
}
#endif
