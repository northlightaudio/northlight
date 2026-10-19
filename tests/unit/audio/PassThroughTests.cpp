#include "audio/PassThrough.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>

using namespace aural::audio;

TEST_CASE("Pass-through copies each input to the matching output with gain", "[audio][passthrough]")
{
    std::array<float, 4> in0{1.0f, 2.0f, 3.0f, 4.0f};
    std::array<float, 4> in1{-1.0f, -2.0f, -3.0f, -4.0f};
    std::array<float, 4> out0{}, out1{};
    const float* ins[] = {in0.data(), in1.data()};
    float* outs[] = {out0.data(), out1.data()};

    renderPassThrough(ins, 2, outs, 2, 4, 0.5f, 0.5f);

    CHECK(out0 == std::array<float, 4>{0.5f, 1.0f, 1.5f, 2.0f});
    CHECK(out1 == std::array<float, 4>{-0.5f, -1.0f, -1.5f, -2.0f});
}

TEST_CASE("A single input fans out to every output", "[audio][passthrough]")
{
    std::array<float, 2> in0{0.25f, 0.5f};
    std::array<float, 2> out0{}, out1{}, out2{};
    const float* ins[] = {in0.data()};
    float* outs[] = {out0.data(), out1.data(), out2.data()};

    renderPassThrough(ins, 1, outs, 3, 2, 1.0f, 1.0f);

    CHECK(out0 == in0);
    CHECK(out1 == in0);
    CHECK(out2 == in0);
}

TEST_CASE("Gain ramps linearly across the block", "[audio][passthrough]")
{
    std::array<float, 5> in0{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 5> out0{};
    const float* ins[] = {in0.data()};
    float* outs[] = {out0.data()};

    renderPassThrough(ins, 1, outs, 1, 5, 0.0f, 1.0f);

    for (int i = 0; i < 5; ++i)
        CHECK(std::abs(out0[static_cast<size_t>(i)] - 0.25f * static_cast<float>(i)) < 1e-6f);
}

TEST_CASE("No inputs or null channels produce silence", "[audio][passthrough]")
{
    std::array<float, 3> out0{9.0f, 9.0f, 9.0f}, out1{9.0f, 9.0f, 9.0f};
    float* outs[] = {out0.data(), out1.data()};

    renderPassThrough(nullptr, 0, outs, 2, 3, 1.0f, 1.0f);
    CHECK(out0 == std::array<float, 3>{0.0f, 0.0f, 0.0f});
    CHECK(out1 == std::array<float, 3>{0.0f, 0.0f, 0.0f});

    std::array<float, 3> in0{1.0f, 1.0f, 1.0f};
    const float* ins[] = {in0.data(), nullptr};
    out0.fill(9.0f);
    out1.fill(9.0f);
    float* outsWithNull[] = {out0.data(), nullptr, out1.data()};
    renderPassThrough(ins, 2, outsWithNull, 3, 3, 1.0f, 1.0f);
    CHECK(out0 == in0); // output 0 <- input 0
    CHECK(out1 == in0); // output 2 <- input (2 % 2) == 0; the null output 1 is skipped

    // A null input channel feeds silence to its output.
    out0.fill(9.0f);
    out1.fill(9.0f);
    renderPassThrough(ins, 2, outs, 2, 3, 1.0f, 1.0f);
    CHECK(out0 == in0);
    CHECK(out1 == std::array<float, 3>{0.0f, 0.0f, 0.0f});
}

#if defined(AURAL_RT_CHECKS)
namespace
{
std::atomic<int> violations{0};
void countViolation(const aural::rt::Violation&) noexcept
{
    violations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Pass-through does not allocate on a real-time thread", "[audio][passthrough][rt]")
{
    const auto previous = aural::rt::setViolationHandler(&countViolation);
    violations = 0;

    std::array<float, 256> in0{}, in1{};
    std::array<float, 256> out0{}, out1{};
    const float* ins[] = {in0.data(), in1.data()};
    float* outs[] = {out0.data(), out1.data()};
    {
        aural::rt::ScopedRealtimeSection section;
        renderPassThrough(ins, 2, outs, 2, 256, 0.9f, 1.0f);
        renderSilence(outs, 2, 256);
    }

    aural::rt::setViolationHandler(previous);
    CHECK(violations == 0);
}
#endif
